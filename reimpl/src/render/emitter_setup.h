#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — particle EMITTER property setters (gilde.exe d3_engine.c /
// particle script-binding cluster, 0x43fe9c .. 0x4405c4).
//
// These are the script-callable "SetEmitterXxx" / "TriggerEmitter" entry points.
// Each receives a HANDLE (a pointer to a pointer to the live runtime emitter
// record), validates it via the Memory tracker (VIBE_Memory_IsValidPointer
// @0x4391f0), and on success writes scaled/packed values into the emitter record
// at fixed byte offsets. On an invalid handle they emit the corresponding
// "...: invalid handle" diagnostic via VIBE_Script_ReportError @0x440f94 and
// return 0 (all setters return 0).
//
// REGISTER MAPPING (recovered from the __usercall / __userpurge prototypes):
//   eax = a1  -> handle (EmitterHandle*, i.e. EmitterRecord**)
//   edx = a2  -> first value pointer
//   ebx = a3  -> second value pointer
//   ecx = (v7/v8) third value pointer  (implicit ecx arg in the originals)
//   stack a4, a5 -> fourth / fifth value pointers
// The originals take POINTERS to the int/byte inputs (they were passed by
// reference from the script VM operand stack); we keep that 1:1.
//
// SCALE CONSTANTS (verified bit-exact via get_bytes):
//   amplitude/phasespeed/direction/velocity/rndvelocity/plane/flags-time
//                          = 0x3c23d70a = 0.01f
//   acceleration           = 0x3a83126f = 0.001f
// Integer inputs are converted to double, multiplied by the scale, then stored
// as float — matching the x87 `fild dword / fmul / fstp dword` sequence.
//
// EMITTER RECORD LAYOUT (byte offsets recovered from the *(float*)(rec + N)
// writes; only the fields these setters touch are modelled):
//   +0x40 (64)  amplitude    x,y,z,w  (4 floats: a2,a3,ecx then a4 at +0xB0)
//   +0x50 (80)  phasespeed   x,y,z,w  (a2,a3,ecx then a5 at +0xB4)
//   +0x60 (96)  size         x,y,z
//   +0x6C (108) direction    x,y,z
//   +0x78 (120) velocity     x,y,z
//   +0x84 (132) rndvelocity  x,y,z
//   +0x90 (144) acceleration x,y,z
//   +0x9C (156) plane        x,y,z,w  (w = a4, stored as plain float)
//   +0xB0 (176) amplitude.w  / amplitudeW
//   +0xB4 (180) phasespeed.w / phasespeedW
//   +0xAC (172) flags-time scalar (SetFlags: a2*0.01; SetMaxTrigger: (float)v)
//   +0xB8 (184) trigger-time scalar (float)
//   +0xBC (188) time dword 0
//   +0xC0 (192) time dword 1
//   +0xC4 (196) time dword 2
//   +0xC8 (200) color B
//   +0xC9 (201) color G
//   +0xCA (202) color R
//   +0xCB (203) color A
//   +0xCC (204) flagByte0  (bits: 0-4 textureMode, 5 initFill, 6 rebirthFill,
//                           7 isTrigger)
//   +0xCD (205) flagByte1  (bit0 triggerOnce, bit1 triggerPending)
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// Runtime emitter record. Modelled as a raw byte block so the original byte
// offsets are exact; typed accessors are provided by the setter functions.
// Only the fields written by the setter family are documented above; the record
// is larger in the game (the leading 0x40 bytes hold spawn/header state set up
// by VIBE_Particle_SpawnSystemByType, not these setters).
// ---------------------------------------------------------------------------
struct EmitterRecord {
    float amplitude[4];   // +0x40 (64)   x,y,z,(w unused here)
    float phasespeed[4];  // +0x50 (80)   x,y,z,(w unused here)
    float size[3];        // +0x60 (96)
    float direction[3];   // +0x6C (108)
    float velocity[3];    // +0x78 (120)
    float rndVelocity[3]; // +0x84 (132)
    float accel[3];       // +0x90 (144)
    float plane[4];       // +0x9C (156)  x,y,z,w
    float flagsTime;      // +0xAC (172)
    float amplitudeW;     // +0xB0 (176)
    float phasespeedW;    // +0xB4 (180)
    float triggerTime;    // +0xB8 (184)
    u32   time[3];        // +0xBC (188)  three raw dwords
    u8    colorB;         // +0xC8 (200)
    u8    colorG;         // +0xC9 (201)
    u8    colorR;         // +0xCA (202)
    u8    colorA;         // +0xCB (203)
    u8    flagByte0;      // +0xCC (204)
    u8    flagByte1;      // +0xCD (205)
};

// A handle is a pointer to the (possibly relocatable) emitter record, matching
// the original `EmitterRecord** a1@<eax>` calling convention.
using EmitterHandle = EmitterRecord*;

// Recovered scale constants (bit-exact).
constexpr float kEmitterScale01  = 0.009999999776482582f; // 0x3c23d70a (1/100)
constexpr float kEmitterScale001 = 0.0010000000474974513f; // 0x3a83126f (1/1000)

// ---------------------------------------------------------------------------
// Validity check / error reporting boundary.
//
// The originals call VIBE_Memory_IsValidPointer (a method on the global memory
// tracker) and VIBE_Script_ReportError (the script VM). To keep this module
// self-contained and testable we route both through a hook. The default hook
// treats any non-null pointer as valid (matching IsValidPointer's null-or-live
// contract for the common case) and records the last reported diagnostic.
// Production code installs a hook that delegates to the real tracker/VM.
// ---------------------------------------------------------------------------
struct EmitterErrorHook {
    // Return true if `handle` is a valid live emitter pointer. Mirrors
    // VIBE_Memory_IsValidPointer (null is "valid" there, but the setters
    // dereference, so a null handle still takes the error path in practice via
    // the report; we keep IsValid faithful and let the caller's contract hold).
    bool (*isValid)(const void* handle) = nullptr;
    // Report an "invalid handle" diagnostic for the named setter.
    void (*report)(const char* message) = nullptr;
};

// Install/replace the active hook. Passing a hook with null function pointers
// restores the default behaviour (non-null => valid, report stored locally).
void SetEmitterErrorHook(const EmitterErrorHook& hook);

// For tests: the message passed to the most recent report() (or default sink).
const char* LastEmitterError();
void ClearLastEmitterError();

// ---------------------------------------------------------------------------
// The setter family. `handle` is `a1@<eax>` (EmitterRecord** in the original).
// Value parameters are pointers, exactly as the script VM passed them. All
// return 0 like the originals.
// ---------------------------------------------------------------------------

// 0x43fe9c — amplitude (x,y,z,w) + amplitudeW. order: rec.amplitude[0..3]=x,y,z,
//            ecx; rec.amplitudeW = a4.  (a2=x@edx, a3=y@ebx, ecx=z, a4=W, a5=w2)
int SetAmplitude(EmitterHandle* handle, const i32* x, const i32* y,
                 const i32* z, const i32* w, const i32* w2);

// 0x43ff30 — phasespeed, same shape as amplitude (offsets +0x50 / +0xB4).
int SetPhasespeed(EmitterHandle* handle, const i32* x, const i32* y,
                  const i32* z, const i32* w, const i32* w2);

// 0x43ffc4 — direction (x,y,z), scaled 0.01.
int SetDirection(EmitterHandle* handle, const i32* x, const i32* y, const i32* z);

// 0x44002c — size (x,y,z), NO scaling (stored as (float)int directly).
int SetSize(EmitterHandle* handle, const i32* x, const i32* y, const i32* z);

// 0x440068 — velocity (x,y,z), scaled 0.01.
int SetVelocity(EmitterHandle* handle, const i32* x, const i32* y, const i32* z);

// 0x4400d4 — acceleration (x,y,z), scaled 0.001.
int SetAcceleration(EmitterHandle* handle, const i32* x, const i32* y,
                    const i32* z);

// 0x440144 — random velocity (x,y,z), scaled 0.01.
int SetRndVelocity(EmitterHandle* handle, const i32* x, const i32* y,
                   const i32* z);

// 0x4401b4 — plane (x,y,z scaled 0.01; w stored as plain (float)int).
int SetPlane(EmitterHandle* handle, const i32* x, const i32* y, const i32* z,
             const i32* w);

// 0x440234 — time triple (raw dwords) + trigger-time (float). order:
//            time[0]=a2, time[1]=a3, time[2]=ecx, triggerTime=(float)a4.
int SetTimeAndAlpha(EmitterHandle* handle, const u32* t0, const u32* t1,
                    const u32* t2, const i32* alphaTime);

// 0x44028c — color. order (note byte order R,G,B,A vs storage): rec.colorR=a2,
//            rec.colorG=a3, rec.colorB=ecx, rec.colorA=a4.
int SetColor(EmitterHandle* handle, const u8* r, const u8* g, const u8* b,
             const u8* a);

// 0x4402e4 — flags + flagsTime. flagsTime = a2*0.01. Then packs into flagByte0:
//   bits0-4 = textureMode (a3 & 0x1F), bit5 = initFill (ecx&1),
//   bit6 = rebirthFill (a4&1), bit7 = isTrigger (a5&1); flagByte1 bit0 =
//   triggerOnce (a6&1).
int SetFlags(EmitterHandle* handle, const i32* time, const u8* textureMode,
             const u8* initFill, const u8* rebirthFill, const u8* isTrigger,
             const u8* triggerOnce);

// 0x4403c8 — flagByte0 bit5 = value&1 (init fill).
int SetInitFill(EmitterHandle* handle, const u8* value);

// 0x440418 — flagByte0 bit6 = value&1 (rebirth fill).
int SetRebirthFill(EmitterHandle* handle, const u8* value);

// 0x440468 — flagByte0 bits0-4 = value&0x1F (texture mode).
int SetTextureMode(EmitterHandle* handle, const u8* value);

// 0x4404b8 — flagByte0 bit7 = value&1 (is-trigger).
int SetIsTrigger(EmitterHandle* handle, const u8* value);

// 0x440508 — flagByte1 bit0 = value&1 (trigger-once).
int SetTriggerOnce(EmitterHandle* handle, const u8* value);

// 0x440558 — flagsTime = (float)value (max-trigger count).
int SetMaxTrigger(EmitterHandle* handle, const i32* value);

// 0x440590 — flagByte1 |= 2 (raise the trigger-pending bit).
int Trigger(EmitterHandle* handle);

} // namespace guild::render
