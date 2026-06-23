#include "sim/gametime_recon.h"

#include <cstring>

namespace guild::sim {

// gilde.exe 0x58320c — VIBE_GameTime_InitDefault.
//   qmemcpy(rec,        &qword_13CE852, 12);
//   qmemcpy(rec + 12,   &unk_13CE85E,   2);
//   return 14;
// The source bytes at 0x13CE852 (12 bytes) and 0x13CE85E (2 bytes) are all zero
// in the binary (recovered via get_bytes), so the effect is to clear the
// 14-byte record. We reproduce the zero template and the byte count return.
int GameTimeInitDefault(GameTime* rec) {
    // qword_13CE852 (12 bytes) + unk_13CE85E (2 bytes), all 0x00 in gilde.exe.
    static const u8 kTemplate[14] = {0};
    std::memcpy(rec, kTemplate, sizeof(kTemplate));  // 12 + 2 bytes
    return 14;
}

// gilde.exe 0x58334c — VIBE_GameTime_UnpackFromRecord.
//   *(_DWORD *)out      = (src[0] >> 16) - 1400;   // sar ecx,16 ; sub 0x578
//   *(_WORD  *)(out+4)  = (u8)src[4];              // movzx/xor ch
//   *(_DWORD *)(out+6)  = (u8)src[5];              // xor ecx ; mov cl
//   eax = *(_DWORD *)(src+8);
//   *(_DWORD *)(out+10) = eax;
//   return eax;
i32 GameTimeUnpackFromRecord(const GameTimePackedRecord* src, GameTime* out) {
    // (src->packedHead >> 16) is an arithmetic (signed) shift in the original
    // (SAR), then minus 1400 (0x578).
    out->day    = (src->packedHead >> 16) - 1400;
    out->hour   = static_cast<u16>(static_cast<u8>(src->field4));
    out->minute = static_cast<i32>(static_cast<u8>(src->field5));
    i32 result  = src->field8;
    out->second = result;
    return result;
}

// gilde.exe 0x583374 — VIBE_GameTime_AdvanceThunk.
//   mov edx, ebx        ; Advance.a2 (addDays)     <- thunk ebx
//   mov ebx, ecx        ; Advance.a4 (addMinutes)  <- thunk ecx
//   mov ecx, [esp+arg0] ; Advance.a3 (addSeconds)  <- thunk stack arg
//   call VIBE_GameTime_Advance  (a1@eax, a2@edx, a3@ecx, a4@ebx)
//   retn 4
// VIBE_GameTime_Advance(rec, addDays, addSeconds, addMinutes):
//   addDays    = ebxArg (thunk ebx -> edx)
//   addSeconds = stackArg (thunk stack -> ecx)
//   addMinutes = ecxArg (thunk ecx -> ebx)
int GameTimeAdvanceThunk(GameTime* rec, int ecxArg, int ebxArg, int stackArg) {
    return GameTimeAdvance(rec, /*addDays=*/ebxArg,
                                /*addSeconds=*/stackArg,
                                /*addMinutes=*/ecxArg);
}

// gilde.exe 0x43c680 — VIBE_GameTick_GetScaledDelay.
//   return uDelay * dword_62EB38;
u32 GameTickGetScaledDelay(const GameTickClockState& clock) {
    return clock.uDelay * clock.throttledTicks;
}

} // namespace guild::sim
