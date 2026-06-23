// Boundary hardening tests for the inter-character relation matrix:
//   world/relation.{h,cpp} — RelationGet / RelationSet (gilde.exe 0x5942fc).
//
// The grid is the flat 768x768 signed-byte image @0x123D6D0, row stride 768
// bytes, addressed as (768*a + b). Self (a==b) reads back as the fixed 127
// sentinel and is never stored. This file pins the in-bounds boundary cells
// (0,0), (767,767), (767,766), (0,767) and the signed round-trip / asymmetry.
//
// NOTE (engine envelope): RelationGet/RelationSet are a 1:1 port of the binary's
// raw pointer arithmetic (dword_123D6CD[192*a] + b) and carry NO range guard —
// the original does not bound a/b either. Out-of-range indices (a or b outside
// [0,767]) are faulting in BOTH the binary and this reconstruction, so we do NOT
// add a guard (that would change observable behaviour on valid input is not at
// issue, but adding a bound where the binary has none is a 1:1 question). These
// tests therefore stay strictly inside the 768x768 grid; the max valid byte
// offset 768*767+767 == 589823 is the last byte of the 589824-byte image.
#include "test.h"

#include "world/relation.h"

using namespace guild;
using namespace guild::world;

// --- 1:1 GRID GEOMETRY: the 768x768 / 768-byte-row addressing (wave-14 pin) ---
// VIBE_Relation_LookupMatrixEntry (0x5942fc) reads the signed byte at
// (gridA + 768*a + b); fixups-wave2 #2 derives that from dword_123D6CD[192*a]+b
// (dword index x4 == 768 bytes/row) >> 24. Pin the recovered constants and the
// EXACT byte offset RelationSet/RelationGet target in g_relationMatrix, so the
// 768*a+b arithmetic (not merely a round-trip) is asserted.
TEST(RelationHarden, GridGeometryConstants) {
    CHECK_EQ(kRelationRowStride, 768);            // bytes per row
    CHECK_EQ(kRelationDim, 768);                  // rows == columns
    CHECK_EQ(kRelationSelf, 127);                 // self sentinel
    CHECK_EQ(kRelationBytes, 768 * 768);          // 589824-byte image
    CHECK_EQ(kRelationBytes, 589824);
}

TEST(RelationHarden, SetTargetsByteOffset768aPlusB) {
    RelationReset();
    // A handful of off-diagonal cells, each checked against the raw flat image at
    // the original's exact (768*a + b) byte offset.
    struct { int a, b; int v; } cases[] = {
        {0, 1, 5},          // offset 1
        {1, 0, -9},         // offset 768
        {2, 3, 64},         // offset 768*2 + 3 = 1539
        {10, 20, 33},       // offset 7700
        {766, 767, -128},   // offset 768*766 + 767 = 589055
        {767, 0, 11},       // offset 768*767 = 589056
        {767, 766, 100},    // offset 589822 (last off-diagonal)
    };
    for (auto& c : cases) {
        RelationSet(c.a, c.b, c.v);
        int off = 768 * c.a + c.b;
        // Raw image holds the low 8 bits; RelationGet reinterprets it signed.
        CHECK_EQ(static_cast<int>(static_cast<i8>(g_relationMatrix[off])), c.v);
        CHECK_EQ(RelationGet(c.a, c.b), c.v);
    }
    // The last byte of the image (589823) is the (767,767) self diagonal — never
    // stored (sentinel), so it stays 0 in the raw image after the writes above.
    CHECK_EQ(static_cast<int>(g_relationMatrix[589823]), 0);
    CHECK_EQ(RelationGet(767, 767), kRelationSelf);
}

// --- Self entries always read 127 regardless of storage ----------------------
TEST(RelationHarden, SelfIsAlwaysSentinel) {
    RelationReset();
    CHECK_EQ(RelationGet(0, 0), kRelationSelf);
    CHECK_EQ(RelationGet(767, 767), kRelationSelf);
    CHECK_EQ(RelationGet(400, 400), kRelationSelf);
    // Setting a self cell is a no-op (self never stored).
    RelationSet(123, 123, -50);
    CHECK_EQ(RelationGet(123, 123), kRelationSelf);
}

// --- The four corner / edge cells of the in-bounds grid round-trip ------------
TEST(RelationHarden, BoundaryCellsRoundTrip) {
    RelationReset();
    // (0,1): first row, first off-diagonal cell.
    RelationSet(0, 1, 42);
    CHECK_EQ(RelationGet(0, 1), 42);

    // (0,767): last column of the first row (byte offset 767).
    RelationSet(0, kRelationDim - 1, -7);
    CHECK_EQ(RelationGet(0, kRelationDim - 1), -7);

    // (767,766): last row, second-to-last column (byte offset 589822).
    RelationSet(kRelationDim - 1, kRelationDim - 2, 100);
    CHECK_EQ(RelationGet(kRelationDim - 1, kRelationDim - 2), 100);

    // (767,0): last row, first column (byte offset 589056).
    RelationSet(kRelationDim - 1, 0, -1);
    CHECK_EQ(RelationGet(kRelationDim - 1, 0), -1);
}

// --- The very last in-bounds byte (768*766 + 767 == 589055... ) --------------
// The maximum off-diagonal cell that is NOT a self cell on the last row is
// (767,766); the absolute last byte (767,767) is the self diagonal (sentinel),
// so to touch byte 589823 via an off-diagonal we use (766,1023)? — out of range.
// Instead verify the largest stored off-diagonal cell on the penultimate row.
TEST(RelationHarden, LastRowLargeColumn) {
    RelationReset();
    RelationSet(766, 767, -128);                 // byte 768*766 + 767 = 589055+... in range
    CHECK_EQ(RelationGet(766, 767), -128);       // INT8_MIN survives the signed cast
    RelationSet(766, 767, 127);
    CHECK_EQ(RelationGet(766, 767), 127);
}

// --- Signed-byte semantics: high bit set reads back negative ------------------
TEST(RelationHarden, SignedByteSemantics) {
    RelationReset();
    RelationSet(5, 6, 0xFF);     // -1 as a signed byte
    CHECK_EQ(RelationGet(5, 6), -1);
    RelationSet(5, 6, 0x80);     // -128
    CHECK_EQ(RelationGet(5, 6), -128);
    RelationSet(5, 6, 0x7F);     // 127
    CHECK_EQ(RelationGet(5, 6), 127);
    // Only the low 8 bits are stored (value truncation matches the binary).
    RelationSet(5, 6, 0x1234);   // low byte 0x34 == 52
    CHECK_EQ(RelationGet(5, 6), 52);
}

// --- Asymmetry: get(a,b) and get(b,a) are distinct cells ----------------------
TEST(RelationHarden, MatrixIsAsymmetric) {
    RelationReset();
    RelationSet(10, 20, 33);
    RelationSet(20, 10, -33);
    CHECK_EQ(RelationGet(10, 20), 33);
    CHECK_EQ(RelationGet(20, 10), -33);
    // Untouched neighbours stay 0 (neutral, not self).
    CHECK_EQ(RelationGet(10, 21), 0);
}

// --- Reset clears the whole image --------------------------------------------
TEST(RelationHarden, ResetClears) {
    RelationSet(0, 1, 10);
    RelationSet(767, 766, 20);
    RelationReset();
    CHECK_EQ(RelationGet(0, 1), 0);
    CHECK_EQ(RelationGet(767, 766), 0);
}
