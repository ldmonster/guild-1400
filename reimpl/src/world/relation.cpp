#include "world/relation.h"

#include <cstring>

// Faithful 1:1 port of VIBE_Relation_LookupMatrixEntry (gilde.exe 0x5942fc).
// The matrix is the flat 768x768 signed-byte grid A @0x123D6D0 (row stride 768
// bytes). The original reads cell (a,b) as the signed high byte of the
// unaligned dword whose low byte sits at (0x123D6CD + 768*a + b) — i.e.
// `dword_123D6CD[192*a]` is a DWORD index, 4*192 == 768 bytes per row — which
// is provably the signed byte at (0x123D6D0 + 768*a + b). We read/write that
// byte directly; the arithmetic `>> 24` of the dword and the signed byte are
// bit-identical.

namespace guild::world {

u8 g_relationMatrix[kRelationBytes];

void RelationReset() {
    std::memset(g_relationMatrix, 0, sizeof(g_relationMatrix));
}

namespace {
struct RelationInit {
    RelationInit() { RelationReset(); }
} g_relationInit;
} // namespace

// gilde.exe 0x5942fc — VIBE_Relation_LookupMatrixEntry.
int RelationGet(int a, int b) {
    if (a == b)
        return kRelationSelf; // self = 127
    // (char*)&dword_123D6CD[192*a] + b, dword >> 24  ==  signed byte at
    // gridA + 768*a + b (see file header).
    int byteOffset = kRelationRowStride * a + b;
    return static_cast<i8>(g_relationMatrix[byteOffset]);
}

void RelationSet(int a, int b, int value) {
    if (a == b)
        return; // self is the fixed 127 sentinel; never stored
    int byteOffset = kRelationRowStride * a + b;
    g_relationMatrix[byteOffset] = static_cast<u8>(value);
}

} // namespace guild::world
