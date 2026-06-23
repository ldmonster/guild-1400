#pragma once
// Relation matrix: the N x N inter-character relation table. Faithful 1:1 port of
// VIBE_Relation_LookupMatrixEntry (gilde.exe 0x5942fc) plus a matching setter
// derived from the same byte-addressed packing the reader/AI use.
//
// Original storage: the 768x768 SIGNED-BYTE primary relation grid A at
// 0x123D6D0, row stride 768 BYTES. The reader computes
//   *(int*)((char*)&dword_123D6CD[192*a] + b) >> 24
// — dword_123D6CD is a DWORD array, so dword_123D6CD[192*a] is the dword at
// byte offset 4*192*a == 768*a from 0x123D6CD; the `>> 24` (arithmetic) then
// extracts the SIGNED HIGH byte of that unaligned dword, i.e. the byte at
//   0x123D6CD + 768*a + b + 3  ==  0x123D6D0 + 768*a + b.
// So the per-(a,b) value is exactly the signed byte at (gridA + 768*a + b):
// row stride 768 bytes, one byte per cell. Self (a==b) is hard-coded to 127.
//
// This is the SAME grid the opcode-0x1B apply handler
// (sim::ExComputeObjectCoords, gilde.exe 0x49818C, command_apply6.{h,cpp})
// mutates: sim::RelationState::matrixA aliases g_relationMatrix below, so the
// reader, the setter and the apply handler address ONE memory the way the
// binary's single global does. (Grid B, byte_1333110, is only touched by the
// 0x1B handler and lives in sim::RelationState::matrixB.)
#include "guild/common/types.h"

namespace guild::world {

// The grid geometry (byte @0x123D6D0): 768 rows x 768 signed-byte columns,
// row stride == row width == 768 bytes. Matches the 768-slot Person array
// (word_12CE910) the relation indices are person indices into.
constexpr int kRelationRowStride = 768;          // bytes per row (orig)
constexpr int kRelationDim       = 768;          // rows == columns
constexpr int kRelationSelf      = 127;          // self-relation sentinel

// Flat byte backing store — the byte image of the grid @0x123D6D0.
constexpr int kRelationBytes = kRelationDim * kRelationRowStride;
extern u8 g_relationMatrix[kRelationBytes];

// Resets the whole matrix to 0 (neutral-but-not-self). Self entries always read
// back as 127 regardless of storage.
void RelationReset();

// gilde.exe 0x5942fc — VIBE_Relation_LookupMatrixEntry  (eax=a, edx=b).
//   if (a == b) return 127;
//   return *(int*)((char*)&dword_123D6CD[192*a] + b) >> 24;
// == the SIGNED byte at (gridA + 768*a + b); see the header comment for the
// dword-index-x4 / high-byte derivation. Note the asymmetry: get(a,b) reads
// cell (768*a + b), get(b,a) reads (768*b + a) — distinct cells, so the matrix
// is asymmetric in general.
int RelationGet(int a, int b);

// Setter consistent with the reader's packing: writes `value` (low 8 bits) into
// the byte the reader extracts, i.e. (gridA + 768*a + b). No-op when a == b
// (self is the fixed 127 sentinel and is never stored).
void RelationSet(int a, int b, int value);

} // namespace guild::world
