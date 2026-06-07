// guild::audio::sb3 — third sample-bank slice of gilde.exe: the .txt sample-bank
// *parser / serializer* family plus the in-memory tree mutators and the sample
// player.  Translated 1:1 from the VIBE_SampleBank_* originals.
//
// Functions reconstructed here (all under namespace guild::audio::sb3, dropping
// the VIBE_SampleBank_ prefix per CONVENTIONS):
//   VIBE_SampleBank_LoadFromText        @0x448958  (.txt parser)
//   VIBE_SampleBank_SaveToText          @0x448ed0  (.txt serializer)
//   VIBE_SampleBank_SaveBinary          @0x449058
//   VIBE_SampleBank_ResolveSamplePaths  @0x449580
//   VIBE_SampleBank_PlaySample          @0x4490e4
//   VIBE_SampleBank_AddSample           @0x447f44
//   VIBE_SampleBank_AddSampleToVariation@0x447da0
//   VIBE_SampleBank_RemoveSample        @0x4480cc
//   VIBE_SampleBank_RemoveVariation     @0x447bf4
//   VIBE_SampleBank_Destroy             @0x4493b4
//   VIBE_Audio_GetSampleFileSize        @0x4475d4  (file-size probe leaf)
//
// The original keeps a single global "active bank" (dword_62E8E8) which is a
// heap record (0x40 bytes) holding:  name @+0, accumulated size @+52,
// first top-level sample @+56, first variation @+60.  Sample records are 0x108
// bytes:  name @+0, file-size @+256, next-sample @+260.  Variation records are
// 0x40 bytes:  name @+0, accumulated size @+52, first sample @+56 (NB: samples
// of a variation chain through +260 like the bank's top-level list — see
// LoadFromText:  *((_DWORD*)v57+14) is the variation's first sample, +260 links
// the rest), next variation @+60.  This module owns its own record type and its
// own global bank so it does not collide with the sb2 / SampleBank facades.

#include "guild/common/types.h"
#include <cstdint>

namespace guild::audio::sb3 {

// ---- raw record offsets (provenance) ----------------------------------------
//   name      +0     (StrNCopyPad 50 for bank/variation, 256 for samples)
//   size      +52    accumulated readable byte size
//   sampleHd  +56    first top-level sample (bank) / first sample (variation)
//   varNext   +60    next variation (bank+60 = first variation)
//   smpSize   +256   per-sample readable byte size
//   smpNext   +260   next sample in the list
struct SbRecord {
    char      name[256] = {0}; // +0
    int32_t   size      = 0;   // +52 accumulated (bank / variation)
    SbRecord* sampleHead = nullptr; // +56 first sample
    SbRecord* varNext   = nullptr;  // +60 next variation
    int32_t   smpSize   = 0;   // +256 this sample's file size
    SbRecord* smpNext   = nullptr;  // +260 next sample
};

// ---- active bank (dword_62E8E8) + dirty counter (dword_62E8EC) ---------------
//   dword_62E8F4 = compiled-blob byte size, dword_62E8F8 = compiled-blob ptr.
// Single definition in audio_samplebank3.cpp; tests reset these directly.
extern SbRecord* g_activeBank;     // dword_62E8E8
extern int       g_dirtyCount;     // dword_62E8EC
extern int       g_compiledSize;   // dword_62E8F4
extern void*     g_compiledBlob;   // dword_62E8F8

// ---- hooks for the OS / VFS boundary -----------------------------------------
// The original goes through VIBE_File_*/VIBE_Vfs_*/VIBE_Memory_* and the Miles
// sample API.  We route every such leaf through this struct; the inert defaults
// (installed in the .cpp) emulate a no-file environment so the logic is testable
// and deterministic.  Tests install their own to drive the success paths.
struct Sb3Hooks {
    // Token-scan used by the %lang / '.' path-rewrite (loc_5CB930 = StrStr).
    // Returns a pointer into `hay` to the first occurrence of `needle`, or null.
    // Default: a faithful clone of loc_5CB930.  The itest wires util::StrStr.
    char* (*strStr)(char* hay, const char* needle) = nullptr;

    // VIBE_Audio_GetSampleFileSize @0x4475d4 — readable byte length of `path`,
    // or 0 if it cannot be opened.  Default returns 0 (no file system).
    int (*sampleFileSize)(const char* path) = nullptr;

    // Text-stream reader (VIBE_Text_ScanfWrapper "%s"): write the next
    // whitespace-delimited token into `out`, return 0, or -1 at EOF.
    // Default returns -1 (empty stream).
    int (*readToken)(void* stream, char* out, int cap) = nullptr;

    // Text-stream writer (VIBE_Crt_Sprintf to a stream): append `text`.
    void (*writeText)(void* stream, const char* text) = nullptr;

    // Open / close a named stream.  open returns a non-null handle or null.
    void* (*openStream)(const char* path, const char* mode) = nullptr;
    void  (*closeStream)(void* stream) = nullptr;
};
extern Sb3Hooks g_hooks;

// ---- the .txt parser / serializer + tree mutators ----------------------------

// VIBE_SampleBank_LoadFromText @0x448958 — parse a whitespace/line oriented bank
// file.  Grammar:  <bankName> {<sample>}* "{EndOfSamples}"
//   {<varName> {<sample>}* "{EndOfVariation}"}* "{EndOfSampleBank}".
// `root` is the path-prefix substituted into any "%lang" token via the strStr
// hook (a no-op for plain names).  Fails (-1) if a bank is already loaded or the
// stream cannot be opened.  Returns 0 on success.
int LoadFromText(const char* root, const char* path);

// VIBE_SampleBank_SaveToText @0x448ed0 — serialize the active bank back to the
// same grammar.  Returns 0 on success, -1 if no bank / open failed.  Round-trips
// byte-for-byte with LoadFromText for token names without embedded whitespace.
int SaveToText(const char* path);

// VIBE_SampleBank_SaveBinary @0x449058 — write the compiled blob (g_compiledBlob,
// g_compiledSize) to `path`.  Returns 0, or -1 on short write, or 0 if nothing
// compiled.  (Compile itself is deferred — see report.)
int SaveBinary(const char* path);

// VIBE_SampleBank_ResolveSamplePaths @0x449580 — walk the bank recomputing every
// sample's file size (and the per-variation / bank accumulators), applying the
// "%lang" -> root substitution.  Returns 0 if every sample resolved, -1 if the
// first missing file aborts the walk early (matches the original break).
int ResolveSamplePaths(const char* root);

// VIBE_SampleBank_PlaySample @0x4490e4 — resolve the named sample and "play" it.
// If `name` has no '.' it is treated as a variation: pick sample index (n %
// count) and recurse.  Returns 0 on success, -1 on failure.  Routed through the
// hooks so no Miles call escapes.
int PlaySample(const char* name, int index);

// VIBE_SampleBank_AddSample @0x447f44 — append a top-level sample by path.
int AddSample(const char* path);

// VIBE_SampleBank_AddSampleToVariation @0x447da0 — append a sample to a variation.
int AddSampleToVariation(const char* varName, const char* path);

// VIBE_SampleBank_RemoveSample @0x4480cc — remove a top-level sample by name, or
// (name==nullptr) free the whole top-level list.  Returns 0, or -1 if no bank.
int RemoveSample(const char* name);

// VIBE_SampleBank_RemoveVariation @0x447bf4 — remove a variation (and its samples)
// by name, or (name==nullptr) clear every variation.  Returns 0 on success.
int RemoveVariation(const char* name);

// VIBE_SampleBank_Destroy @0x4493b4 — free every variation, every top-level
// sample, and the bank record.  Returns 0, or -1 if no bank.
int Destroy();

// VIBE_Audio_GetSampleFileSize @0x4475d4 — readable byte length of `path`.
int GetSampleFileSize(const char* path);

} // namespace guild::audio::sb3
