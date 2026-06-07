#include "world/relation.h"

#include <cstring>

// Faithful 1:1 port of VIBE_Relation_LookupMatrixEntry (gilde.exe 0x5942fc).
// The matrix is a flat byte buffer addressed exactly as the original: the cell
// for (a,b) is the dword whose low byte is at (192*a + b), and the value is that
// dword's signed high byte (>> 24). We back it with a real byte array and do the
// same unaligned dword read via memcpy so behaviour is identical without UB.

namespace guild::world {

u8 g_relationMatrix[kRelationBytes];

void RelationReset() {
    std::memset(g_relationMatrix, 0, sizeof(g_relationMatrix));
}

namespace {
struct RelationInit {
    RelationInit() { RelationReset(); }
} g_relationInit;

// Read the 32-bit dword whose low byte is at byteOffset (unaligned), as i32.
i32 ReadDword(int byteOffset) {
    i32 v;
    std::memcpy(&v, &g_relationMatrix[byteOffset], sizeof(v));
    return v;
}
} // namespace

// gilde.exe 0x5942fc — VIBE_Relation_LookupMatrixEntry.
int RelationGet(int a, int b) {
    if (a == b)
        return kRelationSelf; // self = 127
    int byteOffset = kRelationRowStride * a + b;     // (char*)&base[192*a] + b
    return ReadDword(byteOffset) >> 24;              // signed arithmetic shift
}

void RelationSet(int a, int b, int value) {
    if (a == b)
        return; // self is the fixed 127 sentinel; never stored
    int byteOffset = kRelationRowStride * a + b;
    // The reader extracts bits 24..31 of the dword at byteOffset; that high byte
    // lives at byteOffset + 3.
    g_relationMatrix[byteOffset + 3] = static_cast<u8>(value);
}

} // namespace guild::world
