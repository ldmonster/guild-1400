#pragma once
// guild::audio — second slice of the sample-bank / audio-slot / 3d-sound leaves
// of gilde.exe, translated 1:1 from the VIBE_* originals.
//
// This file models the ORIGINAL raw record layout (the existing samplebank.cpp
// uses a higher-level OOP facade; these functions instead manipulate the raw
// singly-linked record tree exactly as the binary does, via a global "active
// bank" pointer). Record offsets (bytes), recovered from the originals:
//
//   bank node:
//     +0   bank name (StrNCopyPad target, IsValid)
//     +52  accumulated bank size (ClearVariationSamples subtracts here)
//     +56  first top-level sample (HasSamples)
//     +60  first variation         (HasVariations / AddVariation walk)
//   variation node:
//     +0   variation name (FindVariationByName StrCmp, AddVariation StrNCopyPad)
//     +52  variation size (ClearVariationSamples subtracts here)
//     +56  first sample in variation (VariationHasSamples / CountSamples)
//     +60  next variation
//   sample node:
//     +0   sample name (SetVariationName rewrites here)
//     +256 sample size (ClearVariationSamples subtracts)
//     +260 next sample in variation (CountSamples / SetVariationName walk)
//
// Translated functions (addr / size):
//   VIBE_SampleBank_ExtractFileExtension   @0x447770  (85)
//   VIBE_SampleBank_ClassifyAudioFormat    @0x4477c8  (101)
//   VIBE_SampleBank_ValidateSampleFile     @0x447830  (85)  [File I/O via hook]
//   VIBE_SampleBank_GetDirtyCount          @0x4493f8  (6)
//   VIBE_SampleBank_CountSamples           @0x4481ec  (77)
//   VIBE_SampleBank_HasSamples             @0x44823c  (45)
//   VIBE_SampleBank_VariationHasSamples    @0x448290  (56)
//   VIBE_SampleBank_HasVariations          @0x4482c8  (45)
//   VIBE_SampleBank_IsValid                @0x448318  (42)
//   VIBE_SampleBank_AddVariation           @0x4479dc  (288)
//   VIBE_SampleBank_SetVariationName       @0x447b5c  (152)
//   VIBE_SampleBank_ClearVariationSamples  @0x447cb0  (240)
//   VIBE_Audio_FindFreeAmbientSlot         @0x43a898  (41)
//   VIBE_Audio_FindFreeVoiceSlot           @0x43a8c4  (76)
//   VIBE_Sound3d_DetachIfValid             @0x4248c8  (11)
//   VIBE_Sound3d_SetRange                  @0x424cb0  (4)
#include "guild/common/types.h"
#include <cstdint>

namespace guild::audio::sb2 {

// ---- raw record layout ------------------------------------------------------
constexpr int kVarNameOff   = 0;    // +0   variation/bank/sample name
constexpr int kSizeOff      = 52;   // +52  accumulated size
constexpr int kSampleHead   = 56;   // +56  first sample
constexpr int kVarNext      = 60;   // +60  next variation (bank: first variation)
constexpr int kSmpSizeOff   = 256;  // +256 per-sample size
constexpr int kSmpNext      = 260;  // +260 next sample

// The variation/sample/bank records in the original are heap blocks; here we use
// a fixed-size record big enough for the touched offsets (name + links + sizes).
struct Record {
    char  name[64] = {0};   // +0  (AddVariation zero-fills 0x40 then StrNCopyPad 50)
    int32_t size = 0;       // +52 (accumulated size)
    Record* sampleHead = nullptr; // +56
    Record* next = nullptr;       // +60 (variation chain) / unused for samples
    int32_t smpSize = 0;          // +256 per-sample contribution
    Record* smpNext = nullptr;    // +260 next sample-in-variation
};

// ---- active bank (dword_62E8E8) + dirty counter (dword_62E8EC) ---------------
// Defined once in audio_samplebank2.cpp. Tests set/reset these directly.
extern Record* g_activeBank;   // dword_62E8E8
extern int     g_dirtyCount;   // dword_62E8EC

// ---- File-I/O hook (ValidateSampleFile routes OS/VFS calls through here) -----
// Inert default returns "file present and readable" failure (-1), matching the
// no-file environment. Tests install their own to exercise the success path.
struct FileHooks {
    // VIBE_File_OpenStream / Read / Close. open() returns a non-zero handle and
    // sets *outSize to the readable length, or 0 on failure. read() returns the
    // number of records read (the original wants exactly 1). close() frees.
    void* (*open)(const char* path, const char* mode, unsigned* outSize) = nullptr;
    int   (*read)(void* handle, void* buf, unsigned size, int count) = nullptr;
    void  (*close)(void* handle) = nullptr;
};
extern FileHooks g_fileHooks;

// ---- SampleBank leaves -------------------------------------------------------

// VIBE_SampleBank_ExtractFileExtension @0x447770 — given a path, write the part
// AFTER the last '\\' and AFTER the first '.' (the extension incl. the dot is
// skipped; the original copies from `dot+1`). Returns 0 on success, -1 on error
// (null args, no '.', or required length >= cap). `out` is StrNCopyPad-filled.
int ExtractFileExtension(const char* path, char* out, int cap);

// VIBE_SampleBank_ClassifyAudioFormat @0x4477c8 — 1 for .wav/.WAV, 2 for
// .mp3/.MP3 (substring match, case-sensitive), 0 otherwise / null.
int ClassifyAudioFormat(const char* ext);

// VIBE_SampleBank_ValidateSampleFile @0x447830 — open `path`, read the whole
// file once; 0 if the single read succeeded, -1 otherwise. Routed through
// g_fileHooks.
int ValidateSampleFile(const char* path);

// VIBE_SampleBank_GetDirtyCount @0x4493f8 — returns g_dirtyCount.
int GetDirtyCount();

// VIBE_SampleBank_CountSamples @0x4481ec — count samples in the named variation.
// If `name` is null, recurse over every variation of the active bank and sum.
int CountSamples(const char* name);

// VIBE_SampleBank_HasSamples @0x44823c — if the active bank has a top-level
// sample list, copy its first node's name into `out` (cap padded) and return the
// StrNCopyPad result (the bank's +56 pointer, as the original returns ecx==dst);
// returns 0 if no active bank or no samples.
int HasSamples(char* out, int cap);

// VIBE_SampleBank_VariationHasSamples @0x448290 — for the named variation, if it
// has samples, copy its sample-head name into `out`. Returns the dst pointer
// truthiness; 0 if no bank / no variation / no samples.
int VariationHasSamples(const char* name, char* out, int cap);

// VIBE_SampleBank_HasVariations @0x4482c8 — like HasSamples but for the first
// variation (active bank +60).
int HasVariations(char* out, int cap);

// VIBE_SampleBank_IsValid @0x448318 — copy the active bank's name into `out`;
// returns -1 if there is no active bank.
int IsValid(char* out, int cap);

// VIBE_SampleBank_AddVariation @0x4479dc — append a new zero-filled variation
// node named `name` to the active bank's variation chain. Returns 0 on success,
// -1 if no bank / name already present / alloc fails. Increments g_dirtyCount.
int AddVariation(const char* name);

// VIBE_SampleBank_SetVariationName @0x447b5c — within the variation whose first
// sample matches `oldName`, find the sample equal to `oldName` and overwrite its
// name with `newName` (byte copy incl. terminator). Returns 0 / -1.
//   varKey   : the variation to search (selects the sample list)
//   oldName  : sample name to match
//   newName  : replacement
int SetVariationName(const char* varKey, const char* oldName, const char* newName);

// VIBE_SampleBank_ClearVariationSamples @0x447cb0 — for the named variation:
//   if `sampleName` != null: unlink+free the one sample equal to `sampleName`,
//     subtracting its +256 size from the variation (+52) and bank (+52);
//   else: free EVERY sample, subtracting each size, then null the sample head.
// Returns 0 on success, -1 if the variation is not found. Bumps g_dirtyCount per
// freed node.
int ClearVariationSamples(const char* name, const char* sampleName);

// ---- Audio slot scans (operate on the caller-supplied arrays) ---------------

// VIBE_Audio_FindFreeAmbientSlot @0x43a898 — scan ambient slots (stride 296,
// count 10): the first slot where flagB[i]==0 AND flagA[i]!=0. Returns the slot
// index (0..9) or -1 if none. The original returns &base+offset; we return the
// index for testability. `flagA` is byte_62DB34 (active), `flagB` is byte_62DB36
// (claimed/busy), each indexed at i*296.
int FindFreeAmbientSlot(const uint8_t* flagA, const uint8_t* flagB);

// VIBE_Audio_FindFreeVoiceSlot @0x43a8c4 — two-pass voice-slot scan over 10
// slots (loop bound 740 step 74 => 10 iterations). Pass 1: slot with
// flagByte==0 AND word0==0 AND word1==0. Pass 2 (fallback): flagByte==0 AND
// word0==0. Returns slot index (0..9) or -1. Arrays are indexed per-slot.
//   flagByte[i] : byte_62DB34 at (i*74)*4
//   word0[i]    : dword_62DB30 at i*74
//   word1[i]    : dword_62DB3C at i*74
int FindFreeVoiceSlot(const uint8_t* flagByte, const int32_t* word0, const int32_t* word1);

// ---- Sound3d trivial leaves --------------------------------------------------

// VIBE_Sound3d_SetRange @0x424cb0 — store `range` at entry+64 and return the
// entry pointer (the original is `*(int*)(entry+64)=range; return entry;`).
// Modeled here against a minimal 3d entry whose +0x40 field is `range`.
struct Sound3dRangeEntry {
    char  pad[64] = {0}; // +0x00 .. +0x3F
    int32_t range = 0;   // +0x40
};
Sound3dRangeEntry* SetRange(Sound3dRangeEntry* entry, int range);

// Detach hook for DetachIfValid (the real VIBE_Sound3d_DetachEntry is a sibling
// that mutates pool/voice state we don't own here). Inert default records the
// last handle and returns it.
struct Sound3dHooks {
    int (*detachEntry)(int handle) = nullptr;
};
extern Sound3dHooks g_sound3dHooks;

// VIBE_Sound3d_DetachIfValid @0x4248c8 — if handle != 0, DetachEntry(handle)
// (via hook); else return handle unchanged.
int DetachIfValid(int handle);

} // namespace guild::audio::sb2
