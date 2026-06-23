#include "drm/drm_control.h"

// =============================================================================
// guild::drm — DRM control-flow / exception-dispatch reconstruction (1:1).
// See drm_control.h for scope, OS-coupling policy and CONTEXT offset map.
//
// These are faithful translations of the original opcode-VM exception
// dispatchers and message/state dispatchers. The pure branch conditions,
// exception-code switches, state transitions and CONTEXT field writes are
// reproduced exactly; OS/timer/exit primitives go through the inert DrmOs
// hooks; in-place CONTEXT mutation is reproduced on the DrmContext mirror.
// =============================================================================

namespace guild::drm {

// gilde.exe 0x140b0b0 — VIBE_Drm_TrapUndefined  (__noreturn, ud0)
void TrapUndefined(DrmOs& os) {
    os.TrapUndefined();          // original: `ud0` -> STATUS_ILLEGAL_INSTRUCTION
    // The original never returns (the trap unwinds into the VEH). The inert
    // hook returns; we must still satisfy [[noreturn]] in the headless model.
    for (;;) {}
}

// gilde.exe 0x140b570 — VIBE_Drm_CheckExceptionCode  (__stdcall)
//   cmp dword ptr [ecx], 0C0000090h ; jnz -> 0 ; else -1
int CheckExceptionCode(u32 exceptionCode) {
    if (exceptionCode == kExcFltInvalidOp)   // 0xC0000090
        return kExcCodeMatch;                // -1
    return kExcCodeNoMatch;                  // 0
}

// gilde.exe 0x140b0c0 — VIBE_Drm_ExceptionDispatch  (__cdecl)
int ExceptionDispatch(DrmExceptionEvent& ev, DrmState& st, DrmOs& os) {
    DrmContext& c = ev.ctx;
    switch (ev.code) {

    case kExcIntDivByZero: { // 0xC0000094
        c.edx = 6;                      // *(a3+172) = 6
        // *(_WORD*)dword_145F02C += *(BYTE*)(dword_145CB54 + dword_14605E8);
        // (key-feedback accumulate into a DRM data word) — pure data side
        // effect on protection state we don't model headlessly; preserved as
        // a documented no-op (no observable control-flow impact).
        return kContinueExecution;
    }

    case kExcBreakpoint: { // 0x80000003
        st.xorAccLo ^= c.eip;           // dword_142DD60 ^= *(a3+184)
        st.xorAccHi ^= c.ecx;           // dword_142DD64 ^= *(a3+176)
        return kContinueExecution;
    }

    case kExcAccessViol: { // 0xC0000005
        c.ecx = 0u;                     // *(a3+176) = &unk_145D528 (data ptr; modelled 0)
        c.eflags |= (1u << 8);          // BYTE1(EFlags) |= 1  -> set bit 8 (TF)
        st.guardArmed = 1;              // dword_145A11C = 1
        c.dr1 = 0;                      // *(a3+8)  = 0
        c.dr2 = 0;                      // *(a3+12) = 0
        c.dr3 = 0;                      // *(a3+16) = 0
        c.dr0 = c.eip + 6;              // *(a3+4)  = *(a3+184) + 6
        c.dr6 = 0;                      // *(a3+20) = 0
        c.dr7 = 3;                      // *(a3+24) = 3
        return kContinueExecution;
    }

    case kExcSingleStep: { // 0x80000004
        if ((c.dr6 & 0xF) != 0) {       // (*(a3+20) & 0xF) != 0
            c.dr0 = 0; c.dr1 = 0; c.dr2 = 0; c.dr3 = 0; c.dr6 = 0; c.dr7 = 0;
        }
        if (!st.guardArmed || st.stepPhase) {
            if (st.guardArmed && st.stepPhase == 1) {
                st.guardArmed = 0;
                st.stepPhase  = 0;
            }
        } else {
            c.dr0 = c.eip + 3;          // *(a3+4) = *(a3+184) + 3
            c.dr1 = 0; c.dr2 = 0; c.dr3 = 0; c.dr6 = 0;
            c.dr7 = 3;
            st.guardArmed = 0;
            st.stepPhase  = 1;
        }
        if (st.stepPhase == 1) {
            c.eflags |= (1u << 8);      // BYTE1(EFlags) |= 1
        }
        if (st.stepPhase == 2) {
            c.eflags |= (1u << 8);
            st.stepPhase = 1;
            c.dr0 = 0; c.dr1 = 0; c.dr2 = 0; c.dr3 = 0; c.dr6 = 0; c.dr7 = 0;
        }
        if (st.stepPhase == 0) {
            c.eflags &= ~(1u << 8);     // BYTE1(EFlags) &= ~1
            c.dr0 = 0; c.dr1 = 0; c.dr2 = 0; c.dr3 = 0; c.dr6 = 0; c.dr7 = 0;
        }
        // Eip outside the protected VM code range -> divert.
        if (c.eip < st.rangeLo || c.eip > st.rangeHi) {
            st.stepPhase = 2;
            c.eflags &= ~(1u << 8);
            c.dr0 = c.returnTarget;     // *(a3+4) = **(a3+196)  (return addr off stack)
            c.dr1 = 0; c.dr2 = 0; c.dr3 = 0; c.dr6 = 0;
            c.dr7 = 3;
        }
        if (st.stepPhase == 1) {
            u8 op = c.byteAtEip;        // v9 = **(a3+184)
            ++st.sumByteA;              // ++byte_145A124
            ++st.sumDword;              // ++dword_145A128
            st.sumByteB = (u8)(st.sumByteB + op);
            if (st.resetCounters) {
                st.resetCounters = 0;
                st.sumByteA = 0;
                st.sumDword = 0;
                st.sumByteB = 0;
            }
        }
        return kContinueExecution;
    }

    default:
        return kContinueSearch;
    }
}

// gilde.exe 0x140f230 — VIBE_Drm_VectoredExceptionHandler  (__cdecl)
int VectoredExceptionHandler(DrmExceptionEvent& ev, DrmState& st, DrmOs& os) {
    DrmContext& c = ev.ctx;
    switch (ev.code) {

    case kExcBreakpoint: { // 0x80000003
        st.xorAccLo ^= c.eip;
        st.xorAccHi ^= c.ecx;
        c.eflags |= (1u << 8);          // BYTE1(EFlags) |= 1
        st.guardArmed = 1;
        return kContinueExecution;
    }

    case kExcAccessViol: { // 0xC0000005
        // **(a3+164) = *(a3+172):  store Edx through the Eax-slot pointer.
        // Modelled as: returnTarget receives edx (the original writes into a
        // pointed-to location; headlessly we capture the value moved).
        c.returnTarget = c.edx;
        if (!st.resetCounters)          // if (!dword_1451468)
            c.ecx = 0u;                 // *(a3+176) = &unk_145D528 (modelled 0)
        return kContinueExecution;
    }

    case kExcSingleStep: { // 0x80000004
        if ((c.dr6 & 0xF) != 0) {
            c.dr0 = 0; c.dr1 = 0; c.dr2 = 0; c.dr3 = 0; c.dr6 = 0; c.dr7 = 0;
        }
        if (!st.guardArmed || st.stepPhase) {
            if (st.guardArmed && st.stepPhase == 1) {
                st.guardArmed = 0;
                st.stepPhase  = 0;
            }
        } else {
            c.dr0 = c.eip + 3;
            c.dr1 = 0; c.dr2 = 0; c.dr3 = 0; c.dr6 = 0;
            c.dr7 = 3;
            st.guardArmed = 0;
            st.stepPhase  = 1;
        }
        if (st.stepPhase == 1)
            c.eflags |= (1u << 8);
        if (st.stepPhase == 2) {
            c.eflags |= (1u << 8);
            st.stepPhase = 1;
        }
        if (st.stepPhase == 0) {
            c.eflags &= ~(1u << 8);
            c.dr0 = 0; c.dr1 = 0; c.dr2 = 0; c.dr3 = 0; c.dr6 = 0; c.dr7 = 0;
        }
        if (c.eip < st.rangeLo || c.eip > st.rangeHi) {
            if (c.byteAtEip == 0xCC) {  // **(a3+184) == 204 (int3 patched in)
                // *(a3+184) = rangeLo + RandomMod(0xCC, rangeLo-rangeHi)
                c.eip = st.rangeLo + os.RandomMod(0xCCu, st.rangeLo - st.rangeHi);
                st.xorAccLo ^= c.eip;
                st.xorAccHi ^= c.ecx;
            } else {
                st.stepPhase = 2;
                c.eflags &= ~(1u << 8);
                c.dr0 = c.returnTarget; // *(a3+4) = **(a3+196)
                c.dr1 = 0; c.dr2 = 0; c.dr3 = 0; c.dr6 = 0;
                c.dr7 = 3;
            }
        }
        if (st.stepPhase == 1) {
            u8 op = c.byteAtEip;
            ++st.sumByteA;
            ++st.sumDword;
            st.sumByteB = (u8)(st.sumByteB + op);
            if (st.resetCounters) {
                st.resetCounters = 0;
                st.sumByteA = 0;
                st.sumDword = 0;
                st.sumByteB = 0;
            }
        }
        return kContinueExecution;
    }

    default:
        return kContinueSearch;
    }
}

// gilde.exe 0x1411e30 — VIBE_Drm_VerifyTimerOrExit  (__stdcall)
//   if (!dword_142DDBC || dbl_1455868 * (double)dword_142DDBC < (double)dword_1459FA0)
//       VIBE_Crt_Exit(-1);
//   return --word_14501E0;
// dbl_1455868 == 0.9 (verified bytes 0x3FEC...CCCD).
i16 VerifyTimerOrExit(DrmState& st, DrmOs& os) {
    constexpr double kTimerSlackFactor = 0.9; // dbl_1455868
    if (st.timerCalibration == 0 ||
        kTimerSlackFactor * (double)st.timerCalibration < (double)st.timerSampleCount) {
        os.Exit(-1);
    }
    st.vmTableSize = (u16)(st.vmTableSize - 1); // --word_14501E0 (16-bit wrap)
    return (i16)st.vmTableSize;
}

// gilde.exe 0x140b590 — VIBE_Drm_StateDispatch  (__stdcall)
StateDispatchResult StateDispatch(u32 state, bool handshakeSucceeded) {
    StateDispatchResult r;

    if ((int)state <= 13)               // if ((int)a1 <= 13) XorDecode127(table[a1])
        r.decodedSmallTable = true;

    switch (state) {                    // decode-arm selection
    case 0u: case 1u: case 2u: case 3u: case 4u: case 5u:
    case 6u: case 7u: case 8u: case 9u: case 0xAu: case 0xBu:
    case 0xCu: case 0xDu:
        r.stringArm = true;             // VIBE_String_CopyThunk
        break;
    case 0x2Cu:                         // 44: keep-alive, no decode
        r.keepAliveArm = true;
        break;
    default:
        r.largeDecodeArm = true;        // XorDecode123(unk_142EE40) + CopyThunk
        break;
    }

    // dword_145A040 = 0; handshake eligible iff (a1 < 6 || a1 == 13) && strcmp(...)
    bool handshakeStay = false;         // dword_145A040 after the handshake block
    if (state < 6 || state == 13) {
        r.handshakeEligible = true;
        // The original opens a registry/network channel; the injected
        // handshakeSucceeded models dword_145A040's resulting value for the
        // "register/keep resident" sub-states (a1==2 || a1==5). Other states
        // always clear it (dword_145A040 = 0).
        if ((state == 2 || state == 5) && handshakeSucceeded)
            handshakeStay = true;
    }

    if (!handshakeStay) {               // if (!dword_145A040) -> full teardown
        // state != 44 frees the whole VM resource set; state 44 keeps it.
        r.teardown = (state != 44);
        // (the final VIBE_Crt_Sprintf reset + dword_1459F9C=0 always run here)
    }
    return r;
}

// gilde.exe 0x1411030 — VIBE_Drm_DispatchKeyAction  (__stdcall)
DrmKeyActionResult DispatchKeyAction(const DrmKeyActionRow& row, u32 a13, u32 a15) {
    DrmKeyActionResult r;
    switch (row.opcode) {               // switch (dword_14514A4[10*a1])
    case 0:
        r.keyOut = 0;        r.keyOutWritten = true;          // *a16 = 0
        r.redirectValue = a13; r.redirectWritten = true;      // *(a7+4) = a13
        break;
    case 1:
        r.keyOut = 1;        r.keyOutWritten = true;          // *a16 = 1
        r.redirectValue = a13; r.redirectWritten = true;
        break;
    case 2:
        r.keyOut = row.immediate; r.keyOutWritten = true;     // *a16 = dword_1451498[10*a1]
        r.redirectValue = a13;    r.redirectWritten = true;
        break;
    case 16: // resource-query (OS-coupled): no key/redirect write; return 1.
        break;
    case 32: // EV_%04d event signal (OS-coupled): no key/redirect write.
    case 33: // EV_%d   event signal (OS-coupled).
        break;
    case 48: // thread op (OS-coupled).
    case 49: // thread op + 1024 (OS-coupled).
        break;
    case 64:
        r.redirectValue = a15; r.redirectWritten = true;      // *(a7+4) = a15
        break;
    default:
        r.ret = 1;          // default returns 1 too (original `return 1`)
        break;
    }
    return r;               // all paths return 1
}

// gilde.exe 0x140fa60 — VIBE_Drm_InitProtection step-2 sector math (PURE part).
//   Reproduces the CD timecode derivation at 0x1410041..0x141020d. The disc
//   uses 75 sectors/second (0x4B) and 4500 sectors/minute (0x1194); +150
//   sectors == +2 s lead-in. start* use dword_145B934; end* use +150.
static u32 PackMsf(u32 base) {
    // dword_145A564 / dword_145CB50 layout: FF + (SS<<8) + (MM<<16).
    u32 ff = base % 0x4Bu;                  // base % 75
    u32 ss = (base % 0x1194u) / 0x4Bu;      // (base % 4500) / 75
    u32 mm = base / 0x1194u;                // base / 4500
    return ff + (ss << 8) + (mm << 16);
}
static u32 PackBcd(u32 base) {
    // dword_145DB40 / dword_145AC48: each field nudged by 6*(f/10) + f
    // (binary->BCD conversion of the same MM/SS/FF fields).
    u32 ff = base % 0x4Bu;
    u32 ss = (base % 0x1194u) / 0x4Bu;
    u32 mm = base / 0x1194u;
    u32 ffb = 6u * (ff / 0xAu) + ff;
    u32 ssb = 6u * (ss / 0xAu) + ss;
    u32 mmb = 6u * (mm / 0xAu) + mm;
    return ffb + (ssb << 8) + (mmb << 16);
}
DrmSectorAddrs ComputeSectorAddrs(u32 sectorBase) {
    DrmSectorAddrs a;
    a.startMsf = PackMsf(sectorBase);
    a.startBcd = PackBcd(sectorBase);
    a.endMsf   = PackMsf(sectorBase + 150u);
    a.endBcd   = PackBcd(sectorBase + 150u);
    return a;
}

// gilde.exe 0x1411e90 / 0x1414b00 / 0x1415590 — VIBE_Drm_NopStub3/4/5.
// 21-byte marker sleds carrying overlay-decryptor range bounds (see header).
// No observable runtime effect; return the documented [lo,hi).
DrmRangeMarker NopStub3() { return { 0x442103u, 0x442204u }; }
DrmRangeMarker NopStub4() { return { 0x401001u, 0x4020ABu }; }
DrmRangeMarker NopStub5() { return { 0x403001u, 0x404121u }; }

} // namespace guild::drm
