#pragma once
#include "guild/common/types.h"

// x87 / floating-point control & status word state machine (guild::util).
//
// Reconstructed from gilde.exe's MSVC CRT floating-point support. This module
// models the x87 control word (precision / rounding / exception-mask bits) and
// status word as plain integers, plus the CPU-feature flag byte_64A958 that
// selects between the libm path and an x87-software path in float_math.cpp.
//
// Modeled control-word bit layout (matches the x87 hardware FCW the originals
// manipulate via FNSTCW/FLDCW):
//   bit 0   IM  invalid-operation mask
//   bit 1   DM  denormal mask
//   bit 2   ZM  divide-by-zero mask
//   bit 3   OM  overflow mask
//   bit 4   UM  underflow mask
//   bit 5   PM  precision mask
//   bits 8-9   PC  precision control (0=24b,2=53b,3=64b)
//   bits 10-11 RC  rounding control (0=nearest,1=down,2=up,3=truncate)
//
// The CRT exposes the masks/rounding via an abstract word (the "_controlfp"
// word) and converts to/from the hardware FCW with the encode/decode helpers
// below. We keep both representations and faithfully reproduce the bit shuffle.
namespace guild::util {

// Abstract control-word bits (the CRT-level "_controlfp" representation).
// These are the bits the encode/decode helpers translate to/from the x87 FCW.
enum FpControlBits : u32 {
    // exception masks (abstract side)
    kFpEm_Invalid    = 0x10,
    kFpEm_ZeroDivide = 0x08,
    kFpEm_Overflow   = 0x04,
    kFpEm_Underflow  = 0x02,
    kFpEm_Inexact    = 0x01,
    // rounding/precision selector bit used by ControlMask
    kFpDenormalBit   = 0x80000,
};

// Hardware FCW exception-mask bits (used by Sqrt/Fmod state transitions).
enum FpuFcwBits : u16 {
    kFcw_IM = 0x0001,
    kFcw_DM = 0x0002,
    kFcw_ZM = 0x0004,
    kFcw_OM = 0x0008,
    kFcw_UM = 0x0010,
    kFcw_PM = 0x0020,
    kFcw_RC_Mask     = 0x0C00,
    kFcw_RC_Nearest  = 0x0000,
    kFcw_RC_Down     = 0x0400,
    kFcw_RC_Up       = 0x0800,
    kFcw_RC_Truncate = 0x0C00,
};

// Classification codes returned by ClassifyDouble (the original's enum).
enum FpClass : int {
    kFpNormal = 0,  // finite, non-zero, non-special (also +/-0 here)
    kFpPosInf = 1,
    kFpNegInf = 2,
    kFpQNan   = 3,  // quiet NaN  ((hi & 0x7FF8) == 0x7FF8)
    kFpSNan   = 4,  // signalling NaN
};

// gilde.exe 0x1426c03 — VIBE_Fp_DecodeStatusWord
// Convert a hardware FCW low byte into the abstract exception-mask word.
// (Named "status word" by IDA; it operates on the FCW mask bits.)
u8 DecodeStatusWord(u16 fcw);

// gilde.exe 0x1426c95 — VIBE_Fp_EncodeControlWord
// Inverse of DecodeStatusWord: abstract word -> hardware FCW low byte,
// plus the denormal selector (bit 0x80000 -> 0x02).
u8 EncodeControlWord(u32 abstractWord);

// gilde.exe 0x1426bb8 — VIBE_Fp_SetControlWord  (__cdecl(newWord, mask))
// Read the current abstract control word, splice in (new & mask), write it
// back through EncodeControlWord, and return the resulting abstract word.
// The current word is read from the modeled hardware FCW via DecodeStatusWord.
u32 SetControlWord(u32 newWord, u32 mask);

// gilde.exe 0x1426bed — VIBE_Fp_ControlMask
// SetControlWord with the denormal selector bit forced clear in the mask.
u32 ControlMask(u32 newWord, u32 mask);

// gilde.exe 0x14242a2 — VIBE_Fp_ControlWord  (identity: returns its arg)
// Original is the trivial accessor that just returns the passed FCW value.
u16 ControlWord(u16 fcw);

// --- Modeled hardware FCW state (the live x87 control word) ----------------
// gilde.exe FLDCW/FNSTCW state. Get/Set return/round-trip the raw 16-bit FCW.
u16 GetFcw();
void SetFcw(u16 fcw);
// Save the current FCW and install a new one; returns the previous value so the
// caller can restore it (models the FNSTCW/FLDCW save-restore the CRT does).
u16 PushFcw(u16 fcw);

// gilde.exe 0x142416a — VIBE_Fp_ClassifyDouble  (__cdecl(loDword, hiDword))
// Classify the raw 64-bit double given as its two 32-bit halves.
FpClass ClassifyDouble(u32 loDword, u32 hiDword);
// Convenience wrapper that splits a double into its two dwords.
FpClass Classify(double x);

// --- CPU feature flag (byte_64A958) ----------------------------------------
// gilde.exe 0x64A958 — bit 0 set => use the x87-software fallback path
// (Pentium FDIV-bug workaround). VIBE_Fpu_DetectFeatures sets it only on a
// faulty CPU; on every normal CPU it stays 0, so float_math takes the libm
// path. We model it as a settable flag (default 0 = libm path).
bool UseX87SoftwarePath();
void SetX87SoftwarePath(bool on);

} // namespace guild::util
