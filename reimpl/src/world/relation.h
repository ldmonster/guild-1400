#pragma once
// Relation matrix: the N x N inter-character relation table. Faithful 1:1 port of
// VIBE_Relation_LookupMatrixEntry (gilde.exe 0x5942fc) plus a matching setter
// derived from the same byte-addressed packing the reader/AI use.
//
// Original storage: dword_123D6CD @0x123D6CD, row stride 192 bytes. The reader
// computes *(int*)((char*)&dword_123D6CD[192*a] + b) >> 24, i.e. it reads the
// 32-bit dword whose LOW byte sits at (base + 192*a + b) and extracts the SIGNED
// high byte. Self (a==b) is hard-coded to 127. So the per-(a,b) value lives in
// the byte at (base + 192*a + b + 3); a row holds up to 48 packed dword cells.
#include "guild/common/types.h"

namespace guild::world {

// Capacity chosen to cover the engine's character-id space while keeping the
// 192-byte row stride exact. The matrix is byte-addressed; we back it with a
// flat byte buffer so the (192*a + b) arithmetic is identical to the original.
constexpr int kRelationRowStride = 192;          // bytes per row (orig)
constexpr int kRelationDim       = 48;           // valid column count per row
constexpr int kRelationSelf      = 127;          // self-relation sentinel

// Flat byte backing store (rows x stride). +3 trailing bytes so the high-byte
// dword read for the last column stays in bounds.
constexpr int kRelationBytes = kRelationDim * kRelationRowStride + 3;
extern u8 g_relationMatrix[kRelationBytes];

// Resets the whole matrix to 0 (neutral-but-not-self). Self entries always read
// back as 127 regardless of storage.
void RelationReset();

// gilde.exe 0x5942fc — VIBE_Relation_LookupMatrixEntry  (eax=a, edx=b).
//   if (a == b) return 127;
//   return *(int*)((char*)&dword_123D6CD[192*a] + b) >> 24;
// Returns the SIGNED high byte of the dword at (base + 192*a + b). Note the
// asymmetry: get(a,b) reads byte offset (192*a + b + 3), while get(b,a) reads
// (192*b + a + 3) — distinct cells, so the matrix is asymmetric in general.
int RelationGet(int a, int b);

// Setter consistent with the reader's packing: writes `value` (low 8 bits) into
// the high byte of the dword the reader extracts, i.e. byte (192*a + b + 3).
// No-op when a == b (self is the fixed 127 sentinel and is never stored).
void RelationSet(int a, int b, int value);

} // namespace guild::world
