#pragma once
// =============================================================================
// fpu_misc_recon — small leftover VIBE_Fpu cluster (x87 FPU bring-up helpers).
//
// gilde.exe addresses:
//   0x5fa590  VIBE_Fpu_SaveState          (fsave [eax]; wait; ret)        thunk
//   0x5fa598  VIBE_Fpu_RestoreState       (frstor [eax]; wait; ret)       thunk
//   0x5fa59c  VIBE_Fpu_InstallSaveRestore (__cdecl, al)
//   0x5fa5dc  VIBE_Fpu_Init               (__cdecl, eax)
//   0x606210  VIBE_Fpu_DetectNanType      (__cdecl, al)
//
// These are the one-time x87 control-word bring-up and the NaN-sign probe.
// SaveState/RestoreState are 3-instruction thunks (< 12 bytes) around the x87
// FSAVE / FRSTOR instructions: there is no portable C++ equivalent for the full
// 108-byte x87 state image, so they are kept as inert provenance hooks (the
// reconstruction runs on the host FPU, which needs no manual save image).
// =============================================================================
#include "guild/common/types.h"

namespace guild::util {

// Mirrors the three module bytes the cluster touches.  In the original these are
// process globals (byte_64A1A0 "fpu-initialised", byte_64A1A1 "install-hooks",
// byte_64A99C "external-owner").  Grouped here so the cluster is self-contained.
struct FpuState {
    u8 initialised;   // byte_64A1A0
    u8 wantHooks;     // byte_64A1A1
    u8 externalOwner; // byte_64A99C
};

// gilde.exe 0x606210 — VIBE_Fpu_DetectNanType.
//   finit; result = 2; if ( -(1.0/0.0) != 1.0/0.0 ) result = 3; finit; return.
// Returns 3 when negative-infinity compares unequal to positive-infinity (the
// normal IEEE case), else 2.  finit/finit bracket reset the x87 stack.
u8 Fpu_DetectNanType();

// gilde.exe 0x5fa5dc — VIBE_Fpu_Init.
//   BYTE1(result) = st.initialised;
//   if ( !st.initialised ) {
//     st.wantHooks = 0; result = 0; fninit;
//     if ( !st.externalOwner ) { st.initialised = 0; st.wantHooks = 0; }
//   }
//   return result;
int Fpu_Init(FpuState& st);

// gilde.exe 0x5fa59c — VIBE_Fpu_InstallSaveRestore.
//   if ( st.wantHooks ) { off_64AE00 = SaveState; off_64AE04 = RestoreState; }
//   return VIBE_Fpu_DetectNanType();
// `saveSlot`/`restoreSlot` model off_64AE00 / off_64AE04 (the context-switch
// FPU save/restore callback table the original installs into).
using FpuSaveFn    = int (*)(void* image);
using FpuRestoreFn = int (*)(void* image);
u8 Fpu_InstallSaveRestore(FpuState& st, FpuSaveFn* saveSlot, FpuRestoreFn* restoreSlot);

// gilde.exe 0x5fa590 / 0x5fa598 — x87 FSAVE / FRSTOR thunks.  Inert on host FPU.
int Fpu_SaveState(void* image);
int Fpu_RestoreState(void* image);

} // namespace guild::util
