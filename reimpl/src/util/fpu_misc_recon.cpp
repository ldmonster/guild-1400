// =============================================================================
// fpu_misc_recon.cpp — VIBE_Fpu cluster (see header). 1:1 from Hex-Rays.
// =============================================================================
#include "util/fpu_misc_recon.h"

namespace guild::util {

namespace {
// finit/fninit reset the x87 control word, status word and tag word; on the host
// FPU the reconstruction does not drive the x87 stack directly, so the reset is a
// no-op here (kept as a named marker so the call sites read 1:1).
inline void x87_finit() {}
} // namespace

// gilde.exe 0x606210 — VIBE_Fpu_DetectNanType
//   __asm { finit }
//   result = 2;
//   if ( -(1.0 / 0.0) != 1.0 / 0.0 ) result = 3;
//   __asm { finit }
//   return result;
u8 Fpu_DetectNanType() {
    x87_finit();                                   /*0x606212*/
    u8 result = 2;                                 /*0x606223*/
    // -inf vs +inf: a quiet IEEE comparison, evaluated exactly as the original.
    // The original forms +INF at runtime via `1.0 / 0.0`; a volatile zero keeps
    // the same runtime division while avoiding a compile-time literal 1.0/0.0.
    volatile double zero = 0.0;
    const double posInf = 1.0 / zero;              // 1.0/0.0
    if (-posInf != posInf)                          /*0x606226*/
        result = 3;                                 /*0x606228*/
    x87_finit();                                    /*0x60622a*/
    return result;                                  /*0x606236*/
}

// gilde.exe 0x5fa5dc — VIBE_Fpu_Init
//   BYTE1(result) = byte_64A1A0;
//   if ( !byte_64A1A0 ) {
//     byte_64A1A1 = 0;
//     result = 0;
//     __asm { fninit }
//     if ( !byte_64A99C ) { byte_64A1A0 = 0; byte_64A1A1 = 0; }
//   }
//   return result;
int Fpu_Init(FpuState& st) {
    // eax enters as 0 at the call sites; BYTE1 sets bits[8..15] from the flag.
    int result = static_cast<int>(st.initialised) << 8; /*0x5fa5dd*/
    if (!st.initialised) {                               /*0x5fa5e5*/
        st.wantHooks = 0;                                /*0x5fa5e7*/
        result = 0;                                      /*0x5fa5ef*/
        x87_finit();                                     /*0x5fa5f2  fninit*/
        if (!st.externalOwner) {                         /*0x5fa610*/
            st.initialised = 0;                          /*0x5fa612*/
            st.wantHooks = 0;                            /*0x5fa618*/
        }
    }
    return result;                                       /*0x5fa61f*/
}

// gilde.exe 0x5fa59c — VIBE_Fpu_InstallSaveRestore
//   if ( byte_64A1A1 ) {
//     off_64AE00[0] = VIBE_Fpu_SaveState;
//     off_64AE04    = VIBE_Fpu_RestoreState;
//   }
//   return VIBE_Fpu_DetectNanType();
u8 Fpu_InstallSaveRestore(FpuState& st, FpuSaveFn* saveSlot, FpuRestoreFn* restoreSlot) {
    if (st.wantHooks) {                                  /*0x5fa5a5*/
        if (saveSlot)
            *saveSlot = &Fpu_SaveState;                  /*0x5fa5b1*/
        if (restoreSlot)
            *restoreSlot = &Fpu_RestoreState;            /*0x5fa5b7*/
    }
    return Fpu_DetectNanType();                          /*0x5fa5cb*/
}

// gilde.exe 0x5fa590 — fsave byte ptr [eax]; wait; retn   (inert on host FPU).
int Fpu_SaveState(void* /*image*/) { return 0; }

// gilde.exe 0x5fa598 — frstor byte ptr [eax]; wait; retn  (inert on host FPU).
int Fpu_RestoreState(void* /*image*/) { return 0; }

} // namespace guild::util
