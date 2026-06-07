#include "render/emitter_setup.h"

namespace guild::render {

// ---------------------------------------------------------------------------
// Validity / error-report boundary (see header). Default: non-null => valid,
// last message stored in a local sink so tests can observe the error path.
// ---------------------------------------------------------------------------
namespace {

const char* g_lastError = nullptr;

bool DefaultIsValid(const void* handle) { return handle != nullptr; }
void DefaultReport(const char* message) { g_lastError = message; }

EmitterErrorHook g_hook = {DefaultIsValid, DefaultReport};

// Resolve a handle: dereference the handle-of-handle, then validate. Mirrors the
// originals' `v6 = *a1; if (VIBE_Memory_IsValidPointer((int)*a1))` two-step.
EmitterRecord* Resolve(EmitterHandle* handle) {
    if (handle == nullptr) {
        return nullptr;
    }
    EmitterRecord* rec = *handle;
    bool valid = g_hook.isValid ? g_hook.isValid(rec) : DefaultIsValid(rec);
    return valid ? rec : nullptr;
}

void Report(const char* message) {
    if (g_hook.report) {
        g_hook.report(message);
    } else {
        DefaultReport(message);
    }
}

} // namespace

void SetEmitterErrorHook(const EmitterErrorHook& hook) {
    g_hook.isValid = hook.isValid ? hook.isValid : DefaultIsValid;
    g_hook.report = hook.report ? hook.report : DefaultReport;
}

const char* LastEmitterError() { return g_lastError; }
void ClearLastEmitterError() { g_lastError = nullptr; }

// gilde.exe 0x43fe9c — VIBE_Emitter_SetAmplitude
//   (__userpurge, eax=handle, edx=x, ebx=y, ecx=z, +stack w, w2)
// amplitude[0..3] = x,y,z,w (all *0.01); amplitudeW = w2*0.01.
int SetAmplitude(EmitterHandle* handle, const i32* x, const i32* y,
                 const i32* z, const i32* w, const i32* w2) {
    EmitterRecord* rec = Resolve(handle);
    if (rec) {
        float vx = static_cast<float>(static_cast<double>(*x) * kEmitterScale01);
        float vy = static_cast<float>(static_cast<double>(*y) * kEmitterScale01);
        float vz = static_cast<float>(static_cast<double>(*z) * kEmitterScale01);
        float vw2 = static_cast<float>(static_cast<double>(*w2) * kEmitterScale01);
        float vw = static_cast<float>(kEmitterScale01 * static_cast<double>(*w));
        rec->amplitude[0] = vx;       // +0x40
        rec->amplitude[1] = vy;       // +0x44
        rec->amplitude[2] = vz;       // +0x48
        rec->amplitude[3] = vw;       // +0x4C
        rec->amplitudeW = vw2;        // +0xB0
    } else {
        Report("SetEmitterAmplitude: invalid handle...");
    }
    return 0;
}

// gilde.exe 0x43ff30 — VIBE_Emitter_SetPhasespeed (same shape, +0x50 / +0xB4)
int SetPhasespeed(EmitterHandle* handle, const i32* x, const i32* y,
                  const i32* z, const i32* w, const i32* w2) {
    EmitterRecord* rec = Resolve(handle);
    if (rec) {
        float vx = static_cast<float>(static_cast<double>(*x) * kEmitterScale01);
        float vy = static_cast<float>(static_cast<double>(*y) * kEmitterScale01);
        float vz = static_cast<float>(static_cast<double>(*z) * kEmitterScale01);
        float vw2 = static_cast<float>(static_cast<double>(*w2) * kEmitterScale01);
        float vw = static_cast<float>(kEmitterScale01 * static_cast<double>(*w));
        rec->phasespeed[0] = vx;      // +0x50
        rec->phasespeed[1] = vy;      // +0x54
        rec->phasespeed[2] = vz;      // +0x58
        rec->phasespeed[3] = vw;      // +0x5C
        rec->phasespeedW = vw2;       // +0xB4
    } else {
        Report("SetEmitterPhasespeed: invalid handle...");
    }
    return 0;
}

// gilde.exe 0x43ffc4 — VIBE_Emitter_SetDirection (eax=handle, edx=x, ebx=y, ecx=z)
int SetDirection(EmitterHandle* handle, const i32* x, const i32* y,
                 const i32* z) {
    EmitterRecord* rec = Resolve(handle);
    if (rec) {
        float vx = static_cast<float>(static_cast<double>(*x) * kEmitterScale01);
        float vy = static_cast<float>(static_cast<double>(*y) * kEmitterScale01);
        float vz = static_cast<float>(kEmitterScale01 * static_cast<double>(*z));
        rec->direction[0] = vx;       // +0x6C (v4[27])
        rec->direction[1] = vy;       // +0x70 (v4[28])
        rec->direction[2] = vz;       // +0x74 (v4[29])
    } else {
        Report("SetEmitterDirection: invalid handle...");
    }
    return 0;
}

// gilde.exe 0x44002c — VIBE_Emitter_SetSize (no scaling; (float)int directly)
int SetSize(EmitterHandle* handle, const i32* x, const i32* y, const i32* z) {
    EmitterRecord* rec = Resolve(handle);
    if (rec) {
        rec->size[0] = static_cast<float>(*x); // +0x60 (v4[24])
        rec->size[1] = static_cast<float>(*y); // +0x64 (v4[25])
        rec->size[2] = static_cast<float>(*z); // +0x68 (v4[26])
    } else {
        Report("SetEmitterSize: invalid handle...");
    }
    return 0;
}

// gilde.exe 0x440068 — VIBE_Emitter_SetVelocity (scaled 0.01)
int SetVelocity(EmitterHandle* handle, const i32* x, const i32* y,
                const i32* z) {
    EmitterRecord* rec = Resolve(handle);
    if (rec) {
        float vx = static_cast<float>(static_cast<double>(*x) * kEmitterScale01);
        float vy = static_cast<float>(static_cast<double>(*y) * kEmitterScale01);
        float vz = static_cast<float>(kEmitterScale01 * static_cast<double>(*z));
        rec->velocity[0] = vx;        // +0x78 (v4[30])
        rec->velocity[1] = vy;        // +0x7C (v4[31])
        rec->velocity[2] = vz;        // +0x80 (v4[32])
    } else {
        Report("SetEmitterVelocity: invalid handle...");
    }
    return 0;
}

// gilde.exe 0x4400d4 — VIBE_Emitter_SetAcceleration (scaled 0.001)
int SetAcceleration(EmitterHandle* handle, const i32* x, const i32* y,
                    const i32* z) {
    EmitterRecord* rec = Resolve(handle);
    if (rec) {
        float vx = static_cast<float>(static_cast<double>(*x) * kEmitterScale001);
        float vy = static_cast<float>(static_cast<double>(*y) * kEmitterScale001);
        float vz = static_cast<float>(kEmitterScale001 * static_cast<double>(*z));
        rec->accel[0] = vx;           // +0x90 (v4[36])
        rec->accel[1] = vy;           // +0x94 (v4[37])
        rec->accel[2] = vz;           // +0x98 (v4[38])
    } else {
        Report("SetEmitterAcceleration: invalid handle...");
    }
    return 0;
}

// gilde.exe 0x440144 — VIBE_Emitter_SetRndVelocity (scaled 0.01)
int SetRndVelocity(EmitterHandle* handle, const i32* x, const i32* y,
                   const i32* z) {
    EmitterRecord* rec = Resolve(handle);
    if (rec) {
        float vx = static_cast<float>(static_cast<double>(*x) * kEmitterScale01);
        float vy = static_cast<float>(static_cast<double>(*y) * kEmitterScale01);
        float vz = static_cast<float>(kEmitterScale01 * static_cast<double>(*z));
        rec->rndVelocity[0] = vx;     // +0x84 (v4[33])
        rec->rndVelocity[1] = vy;     // +0x88 (v4[34])
        rec->rndVelocity[2] = vz;     // +0x8C (v4[35])
    } else {
        Report("SetEmitterRndVelocity: invalid handle...");
    }
    return 0;
}

// gilde.exe 0x4401b4 — VIBE_Emitter_SetPlane (x,y,z scaled 0.01; w plain float)
int SetPlane(EmitterHandle* handle, const i32* x, const i32* y, const i32* z,
             const i32* w) {
    EmitterRecord* rec = Resolve(handle);
    if (rec) {
        float vx = static_cast<float>(static_cast<double>(*x) * kEmitterScale01);
        float vy = static_cast<float>(static_cast<double>(*y) * kEmitterScale01);
        float vz = static_cast<float>(kEmitterScale01 * static_cast<double>(*z));
        rec->plane[0] = vx;           // +0x9C (v5[39])
        rec->plane[1] = vy;           // +0xA0 (v5[40])
        rec->plane[2] = vz;           // +0xA4 (v5[41])
        rec->plane[3] = static_cast<float>(*w); // +0xA8 (v5[42])
    } else {
        Report("SetEmitterPlane: invalid handle...");
    }
    return 0;
}

// gilde.exe 0x440234 — VIBE_Emitter_SetTimeAndAlpha
//   time[0..2] = raw dwords (edx, ebx, ecx); triggerTime = (float)a4.
int SetTimeAndAlpha(EmitterHandle* handle, const u32* t0, const u32* t1,
                    const u32* t2, const i32* alphaTime) {
    EmitterRecord* rec = Resolve(handle);
    if (rec) {
        rec->time[0] = *t0;           // +0xBC (rec+188)
        rec->time[1] = *t1;           // +0xC0 (rec+192)
        rec->time[2] = *t2;           // +0xC4 (rec+196)
        rec->triggerTime = static_cast<float>(*alphaTime); // +0xB8 (rec+184)
    } else {
        Report("SetEmitterTimeAndAlpha: invalid handle...");
    }
    return 0;
}

// gilde.exe 0x44028c — VIBE_Emitter_SetColor
//   v5[202]=a2(R), v5[201]=a3(G), v5[200]=ecx(B), v5[203]=a4(A).
int SetColor(EmitterHandle* handle, const u8* r, const u8* g, const u8* b,
             const u8* a) {
    EmitterRecord* rec = Resolve(handle);
    if (rec) {
        rec->colorR = *r;             // +0xCA (rec+202)
        rec->colorG = *g;             // +0xC9 (rec+201)
        rec->colorB = *b;             // +0xC8 (rec+200)
        rec->colorA = *a;             // +0xCB (rec+203)
    } else {
        Report("SetEmitterColor: invalid handle...");
    }
    return 0;
}

// gilde.exe 0x4402e4 — VIBE_Emitter_SetFlags
//   flagsTime = a2*0.01 (+0xAC). flagByte0 (+0xCC): bits0-4=textureMode(a3&0x1F),
//   bit5=initFill(ecx&1), bit6=rebirthFill(a4&1), bit7=isTrigger(a5&1).
//   flagByte1 (+0xCD) bit0 = triggerOnce(a6&1).
int SetFlags(EmitterHandle* handle, const i32* time, const u8* textureMode,
             const u8* initFill, const u8* rebirthFill, const u8* isTrigger,
             const u8* triggerOnce) {
    EmitterRecord* rec = Resolve(handle);
    if (rec) {
        rec->flagsTime =
            static_cast<float>(static_cast<double>(*time) * kEmitterScale01);

        // bits 0-4: texture mode (replace).
        u8 b = static_cast<u8>((rec->flagByte0 & 0xE0) | (*textureMode & 0x1F));
        // bit 5: init fill.
        b = static_cast<u8>((b & 0xDF) | (32 * (*initFill & 1)));
        // bit 6: rebirth fill.
        b = static_cast<u8>((b & 0xBF) | ((*rebirthFill & 1) << 6));
        // bit 7: is-trigger.
        b = static_cast<u8>((b & 0x7F) | ((*isTrigger & 1) << 7));
        rec->flagByte0 = b;

        // flagByte1 bit0: trigger-once.
        rec->flagByte1 =
            static_cast<u8>((rec->flagByte1 & 0xFE) | (*triggerOnce & 1));
    } else {
        Report("SetEmitterFlags: invalid handle...");
    }
    return 0;
}

// gilde.exe 0x4403c8 — VIBE_Emitter_SetInitFill (flagByte0 bit5)
int SetInitFill(EmitterHandle* handle, const u8* value) {
    EmitterRecord* rec = Resolve(handle);
    if (rec) {
        rec->flagByte0 =
            static_cast<u8>((rec->flagByte0 & 0xDF) | (32 * (*value & 1)));
    } else {
        Report("SetEmitterInitFill: invalid handle...");
    }
    return 0;
}

// gilde.exe 0x440418 — VIBE_Emitter_SetRebirthFill (flagByte0 bit6)
int SetRebirthFill(EmitterHandle* handle, const u8* value) {
    EmitterRecord* rec = Resolve(handle);
    if (rec) {
        rec->flagByte0 =
            static_cast<u8>((rec->flagByte0 & 0xBF) | ((*value & 1) << 6));
    } else {
        Report("SetEmitterRebirthFill: invalid handle...");
    }
    return 0;
}

// gilde.exe 0x440468 — VIBE_Emitter_SetTextureMode (flagByte0 bits0-4)
int SetTextureMode(EmitterHandle* handle, const u8* value) {
    EmitterRecord* rec = Resolve(handle);
    if (rec) {
        rec->flagByte0 =
            static_cast<u8>((rec->flagByte0 & 0xE0) | (*value & 0x1F));
    } else {
        Report("SetEmitterTextureMode: invalid handle...");
    }
    return 0;
}

// gilde.exe 0x4404b8 — VIBE_Emitter_SetIsTrigger (flagByte0 bit7)
int SetIsTrigger(EmitterHandle* handle, const u8* value) {
    EmitterRecord* rec = Resolve(handle);
    if (rec) {
        rec->flagByte0 =
            static_cast<u8>((rec->flagByte0 & 0x7F) | ((*value & 1) << 7));
    } else {
        Report("SetEmitterIsTrigger: invalid handle...");
    }
    return 0;
}

// gilde.exe 0x440508 — VIBE_Emitter_SetTriggerOnce (flagByte1 bit0)
int SetTriggerOnce(EmitterHandle* handle, const u8* value) {
    EmitterRecord* rec = Resolve(handle);
    if (rec) {
        rec->flagByte1 =
            static_cast<u8>((rec->flagByte1 & 0xFE) | (*value & 1));
    } else {
        Report("SetEmitterTriggerOnce: invalid handle...");
    }
    return 0;
}

// gilde.exe 0x440558 — VIBE_Emitter_SetMaxTrigger (flagsTime = (float)value)
int SetMaxTrigger(EmitterHandle* handle, const i32* value) {
    EmitterRecord* rec = Resolve(handle);
    if (rec) {
        rec->flagsTime = static_cast<float>(*value); // +0xAC (rec+172)
    } else {
        Report("SetEmitterMaxTrigger: invalid handle...");
    }
    return 0;
}

// gilde.exe 0x440590 — VIBE_Emitter_Trigger (flagByte1 |= 2)
int Trigger(EmitterHandle* handle) {
    EmitterRecord* rec = Resolve(handle);
    if (rec) {
        rec->flagByte1 = static_cast<u8>(rec->flagByte1 | 2u);
    } else {
        Report("TriggerEmitter: invalid handle...");
    }
    return 0;
}

} // namespace guild::render
