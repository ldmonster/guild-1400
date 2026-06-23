#pragma once
// =====================================================================================
// audio_recon_engine.h — strict 1:1 reconstruction of the gilde.exe engine-side audio
// bookkeeping layer (voice-slot / sample-bank / 3d-channel bookkeeping, crossfade math).
//
// This is the LOGIC layer of VIBE_Sound / VIBE_Audio / VIBE_Sound3d / VIBE_SampleBank.
// It is reconstructed directly from the Hex-Rays decompile, operating on the original
// raw byte offsets of the engine's audio records (banks, voices, active-sample table,
// streaming track slots, 3d channel records).
//
// Boundary (project rule 5 — audio -> SDL): the Miles Sound System / MSS32 / _AIL*
// backend is NOT reconstructed here. Every leaf that the original forwards to MSS, or
// to subsystems outside this cluster (file-format compile, scene-graph walk, memory
// allocator), is reached through a function-pointer HOOK on AudioEngineHooks. The hooks
// default to inert behaviour so the bookkeeping logic is testable in isolation and so a
// real backend (shim::IAudioDevice) can be bound later. The pure engine arithmetic /
// control-flow is reproduced exactly.
//
// Record layouts (recovered from offset arithmetic in the decompile):
//
//   SampleBank record (linked list, head = g.bankListHead):
//     +0x000  char  name[50]            (VIBE_Util_StrCmpNoCaseN(.., 50))
//     +0x134  i32   voiceCount          ([77] = +308)
//     +0x138  i32*  voices              ([78] = +312)  -> array of 64-byte voice recs
//     +0x13C  bank* next               ([79] = +316)
//     +0x140  i32   markCount          ([80] = +320)   (zeroed on unload)
//
//   Voice record inside a bank's voice array (stride 64):
//     +0x04  char name[50]              (sample name; FindSampleInBank cmp at +4)
//     +0x36  u8   loadKind              (+54: 1 => raw/.raw, else streamed)
//     +0x38  void* loadedData           (+56: non-null once VIBE_Sound_LoadEntry ran)
//     +0x3C  i32   priority/age          (+60: lower => older; oldest-active scan)
//
//   Active-sample / channel record (table = g.sampleTable, stride 48, count g.sampleCount):
//     +0x00  i32   sampleHandle         (MSS sample handle; opaque to engine)
//     +0x04  i32   boundVoice           (+4: bank voice this channel plays, 0 == free)
//     +0x08  i32   allocPriority        (+8: AllocVoiceChannel oldest pick key)
//     +0x0C  i32   loopCount            (+12)
//     +0x10  i32   posScratch           (+16: 13*g.sampleClock written on resume)
//     +0x14  u8    flags                (+20: bit4 0x10 == reserved/sticky, &0xE busy)
//     +0x18  i32   field18              (+24: non-zero => "managed")
//     +0x20  i32   volume               (+32)
//     +0x24  i32   variation            (+36)
//     +0x28  i32   pan                  (+40)
//
//   Streaming track slot (table base g.trackSlots, stride 296, count 10):
//     +0x000 char  name[256]
//     +0x100 i32   stream               (+256: MSS stream handle, 0 == none)
//     +0x104 u8    active               (+260)
//     +0x105 u8    paused               (+261)
//     +0x106 u8    isMusic              (+262)
//     +0x10C i32   posMs                (+268)
//     +0x110 i32   fadeStartMs          (+272)
//     +0x114 u8    fadeMode             (+276: 0 none / 1 in / 2 out)
//     +0x118 i32   targetVol            (+280)
//     +0x11C i32   fadeDurMs            (+284: current volume during fade)
//     +0x120 i32   fadeLenMs            (+288)
//     +0x124 slot* chainNext            (+292: track to resume when this one ends)
//
//   3d channel record (g.snd3dChannels base, stride 25): pair of (handle, sampleHandle)
//   plus a loop flag, addressed via SelectChannel; routed through hooks.
//
// Provenance: every function carries its gilde.exe address.
// =====================================================================================
#include "guild/common/types.h"
#include <cstdint>
#include <cstring>
#include <map>
#include <utility>

namespace guild::audio::recon {

// ---- opaque engine handles ----------------------------------------------------------
// In the original these are raw 32-bit pointers/ids into engine-owned memory; here we
// keep them as uintptr_t so the bookkeeping arithmetic (pointer +offset reads/writes,
// linked-list walks) reproduces exactly while staying host-pointer-clean.
using Addr = std::uintptr_t;

// ---- engine globals (the original's dword_62E8xx / dword_62DAxx / dword_62EAxx) ------
// gilde.exe globals, gathered into one struct so the recon is testable & free of ODR
// clashes with the existing high-level facade.
struct AudioEngineState {
    // VIBE_Sound subsystem ---------------------------------------------------------
    i32  soundEnabled    = 0;   // dword_62E8FC : digital output open / lib live
    i32  masterEnable    = 0;   // dword_62EA10 : VIBE_Sound_SetMasterEnable
    Addr bankListHead    = 0;   // dword_62E8F8 : head of bank linked list
    Addr sampleTable     = 0;   // dword_62EA08 : base of 48-byte channel table
    i32  sampleCount     = 0;   // dword_62EA0C : channel-table entry count
    i32  digitalOutput   = 0;   // dword_62EA18 : MSS digital driver (UpdatePlayback)
    i32  playbackActive  = 0;   // dword_62EA14 : set by UpdatePlayback when resuming
    i32  memBudget       = 0;   // dword_62E8F0 : sample memory budget
    i32  memUsed         = 0;   // dword_62E8F4 : sample memory currently used
    i32  sampleClock     = 0;   // dword_62EB38 : ms clock (13*clock = pos units)

    // VIBE_SampleBank create state -------------------------------------------------
    Addr pendingBank     = 0;   // dword_62E8E8 : bank under construction
    i32  bankSeq         = 0;   // dword_62E8EC : bank counter

    // VIBE_Audio music/track subsystem ---------------------------------------------
    i32  trackSysEnabled = 0;   // dword_62DA28 : track mixer enabled
    i32  ambientEnabled  = 0;   // dword_62DA2C : ambient playback enabled
    Addr trackSlots      = 0;   // unk_62DA30   : base of 296-byte track-slot table
    i32  trackMasterClock= 0;   // dword_62DA24 : crossfade clock scale
    // master-volume fade (MixerUpdate tail)
    float masterVolTarget= -1.0f; // flt_62DA00 : <0 => no fade in progress
    float masterVolBase  = 0.0f;  // flt_62D9FC
    float masterVolCur   = 0.0f;  // flt_62D9F8
    i32   masterFadeStart= 0;     // dword_62DA08
    i32   masterFadeLen  = 0;     // dword_62DA04
    i32   masterVolHandle= 0;     // dword_62DA0C

    // VIBE_Sound3d channel state ---------------------------------------------------
    Addr snd3dChannels   = 0;   // unk_62D38C : base of 3d channel records (stride 25)
    Addr snd3dSampleA    = 0;   // dword_62D434 : channel A sample
    Addr snd3dHandleA    = 0;   // dword_62D468[0] : channel A voice
    Addr snd3dSampleB    = 0;   // dword_62D488 : channel B sample
    Addr snd3dHandleB    = 0;   // dword_62D4BC : channel B voice
};

// ---- hooks: every MSS / out-of-cluster leaf ----------------------------------------
struct AudioEngineHooks {
    // --- MSS sample / voice leaves (audio -> SDL boundary) ---
    int  (*sampleStatus)(Addr channel)               = nullptr; // VIBE_Audio_GetSampleStatus
    void (*initSample)(Addr channel)                 = nullptr; // VIBE_Audio_InitSample
    void (*setSampleLoopCount)(Addr channel)         = nullptr; // VIBE_Audio_SetSampleLoopCount
    void (*playVoiceSample)(Addr channel, int a2)    = nullptr; // VIBE_Audio_PlayVoiceSample
    void (*resumeSample)(Addr channel)               = nullptr; // VIBE_Audio_ResumeSample
    void (*stopVoice)(Addr channel, int a2)          = nullptr; // VIBE_Audio_StopVoice
    int  (*voiceIsPlaying)(Addr voice, Addr cmpName) = nullptr; // VIBE_Audio_VoiceIsPlaying
    // streaming leaves
    void (*closeStream)(i32 stream)                  = nullptr; // VIBE_Audio_CloseStream
    void (*pauseStream)(i32 stream, int a2)          = nullptr; // VIBE_Audio_PauseStream
    int  (*streamStatus)(i32 stream)                 = nullptr; // VIBE_Audio_GetStreamStatus
    void (*setStreamVolume)(i32 stream, i32 vol)     = nullptr; // VIBE_Audio_SetStreamVolume
    void (*getStreamMsPosition)(i32 stream, Addr out)= nullptr; // VIBE_Audio_GetStreamMsPosition
    void (*applyMasterVolume)(i32 handle, i32 clock) = nullptr; // VIBE_Audio_ApplyMasterVolume
    void (*closeDigitalOutput)(Addr drv)             = nullptr; // VIBE_Audio_CloseDigitalOutput
    // --- out-of-cluster leaves ---
    void (*memFree)(Addr block)                      = nullptr; // VIBE_Memory_FreeDebug
    Addr (*loadEntry)(Addr bank, Addr voice)         = nullptr; // VIBE_Sound_LoadEntry
    Addr (*playSample)(int kind, Addr name)          = nullptr; // VIBE_Sound_PlaySample
    void (*setLoopFlag)(Addr voice, int on)          = nullptr; // VIBE_Sound_SetLoopFlag
    Addr (*findActiveTrackSlot)()                    = nullptr; // VIBE_Audio_FindActiveTrackSlot
    Addr (*findFreeVoiceSlot)()                      = nullptr; // VIBE_Audio_FindFreeVoiceSlot
    void (*stopTrack)(Addr slot, int a2)             = nullptr; // VIBE_Audio_StopTrack
    int  (*startTrack)(Addr slot, int a2)            = nullptr; // VIBE_Audio_StartTrack
    // --- 3d / cmd leaves ---
    void (*sound3dPlayOneShot)(Addr voice, Addr name, int baseVol, float pos) = nullptr;
    Addr (*allocVoiceChannel)(int a2, int a3, Addr bank)                      = nullptr;
    Addr (*findSampleInBank)(Addr bank, Addr name)                           = nullptr;
    // --- SampleBank create leaves ---
    Addr (*memAlloc)(int size, const char* tag)              = nullptr; // VIBE_Memory_AllocDebug
    void (*strNCopyPad)(Addr dst, const char* src, int n)    = nullptr; // VIBE_Util_StrNCopyPad
    void (*sampleBankDestroy)()                              = nullptr; // VIBE_SampleBank_Destroy
    int  (*soundLibInit)(int a, int b, int c, int d)         = nullptr; // VIBE_Sound_LibInit
};

// =====================================================================================
// The engine. One instance bundles state + hooks; methods are the 1:1 functions.
// =====================================================================================
class AudioEngine {
public:
    AudioEngineState  st;
    AudioEngineHooks  hooks;

    // side storage for pointer-bearing record fields (see addrAt below)
    std::map<std::pair<Addr,int>, Addr> ptrFields_;

    // Raw integer/byte accessors over the engine records: these read/write the original
    // 4-byte / 1-byte fields at their EXACT gilde.exe offsets.
    static i32&  i32at (Addr base, int off) { return *reinterpret_cast<i32*>(base + off); }
    static u8&   u8at  (Addr base, int off) { return *reinterpret_cast<u8*> (base + off); }

    // Pointer-field accessor. The original is 32-bit: host pointers are 64-bit, so two
    // pointer fields spaced 4 bytes apart (e.g. bank.voices@312 / bank.next@316) would
    // overlap if punned in place. We therefore keep pointer-bearing fields in a side map
    // keyed by (record, original-offset) — the integer bookkeeping above is unaffected and
    // stays byte-exact, while pointer links remain 64-bit-safe. (Per types.h §5: these
    // records are engine-internal, never serialized, so off-record pointer storage is
    // behaviorally identical.)
    Addr  getPtr(Addr base, int off) const;
    void  setPtr(Addr base, int off, Addr val);

    // addrAt mirrors the decompile's pointer-field read/writes through the side map.
    // It returns a proxy so existing `addrAt(b,off) = v` / reads keep working unchanged.
    struct PtrRef {
        AudioEngine* e; Addr base; int off;
        operator Addr() const { return e->getPtr(base, off); }
        PtrRef& operator=(Addr v) { e->setPtr(base, off, v); return *this; }
        PtrRef& operator=(const PtrRef& o) { e->setPtr(base, off, (Addr)o); return *this; }
    };
    PtrRef addrAt(Addr base, int off) { return PtrRef{this, base, off}; }

    // -------- VIBE_Sound: enable / shutdown ------------------------------------------
    int  setMasterEnable(int value);                       // 0x445ef8
    void libShutdown();                                    // 0x445ea4

    // -------- VIBE_Sound: bank / voice search ----------------------------------------
    Addr findBankByName(const char* name);                 // 0x44644c
    Addr findVoiceBySample(Addr voice, int* idx);          // 0x4464b0
    Addr findBankContainingVoice(Addr voice);              // 0x446640
    Addr getLastBank();                                    // 0x446800
    Addr findOldestActiveSample();                         // 0x446540
    int  freeMemoryForLoad(int need);                      // 0x446694
    void unloadAllSampleBanks();                           // 0x447058

    // -------- VIBE_Sound: playback bookkeeping ---------------------------------------
    int  updatePlayback(int result);                       // 0x446d88
    int  countActiveVoices();                              // 0x4494a4

    // -------- VIBE_Audio: voice mixer bookkeeping ------------------------------------
    Addr setVoiceLoopCount(Addr voice, int count);         // 0x4475b8
    Addr restartVoice(Addr voice);                         // 0x44727c
    Addr startVoiceVariation(Addr bank, int a2, int a3,
                             int a4, int a5, int a6);       // 0x4472c4

    // -------- VIBE_Audio: streaming track / crossfade --------------------------------
    void mixerUpdate();                                    // 0x43a3e4
    void setVoicePosition(Addr slot, i32 vol);             // 0x439d74

    // -------- VIBE_Sound3d ------------------------------------------------------------
    Addr sound3dStopEntry(Addr entry, int a2);             // 0x424c70
    Addr sound3dSelectChannel(int channelIdx, int* mode);  // 0x424f00
    Addr sound3dBindHandle(Addr entry, int loop, int a3);  // 0x424bd4

    // -------- VIBE_Sound: script command dispatchers ---------------------------------
    Addr cmdPlaySample(int kind, Addr name, int* loopOut); // 0x440d28
    Addr cmdPlaySample3D(int* pos, int* vol, int* mode,
                         Addr nameRef);                     // 0x440d5c
    int  cmdStopSample(Addr voiceRef);                     // 0x440db0

    // -------- VIBE_Audio: track/ambient control --------------------------------------
    void stopActiveTrack(int a2);                          // 0x43a2b4
    void updateActiveVoice(int vol);                       // 0x439d30
    Addr playAmbientTrack(const char* name, int vol);      // 0x43a984
    int  initDefaultMixer();                               // 0x449400 (-> LibInit)

    // -------- VIBE_SampleBank ---------------------------------------------------------
    int  sampleBankCreate(const char* name);               // 0x447920

    // helpers used internally (also exposed for tests)
    static const char* unkFallback();                      // &unk_767xx sentinels
    static Addr unkFallbackAddr();                          // same, as an Addr

    // 64-bit branch-free abs used by the fade-in test:
    //   (int)((HIDWORD(x) ^ x) - HIDWORD(x))   where x is a sign-extended 64-bit value.
    // Reproduces the exact truncation: result is the low 32 bits of llabs(x).
    static int abs64lo(i64 x) {
        i64 hi = (i64)((u64)x >> 32);          // HIDWORD(x)
        i64 r  = (i64)((u64)(hi ^ x) - (u64)hi);
        return (int)(u32)r;
    }
};

// String compare helper mirroring VIBE_Util_StrCmpNoCaseN @0x5e0db0 semantics:
// case-insensitive compare of at most n bytes, returns 0 on equal-prefix (like strncmp).
int vibeStrCmpNoCaseN(const char* a, const char* b, int n);  // models 0x5e0db0
int vibeStrCmp(const char* a, const char* b);                // models VIBE_Util_StrCmp 0x5d3f10

} // namespace guild::audio::recon
