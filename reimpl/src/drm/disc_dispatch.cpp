#include "drm/disc_dispatch.h"

#include <cstdint>

#include "compress/crc.h"  // guild::compress::Crc16Update (VIBE_Crc16_Update @0x1418ae0)

// =============================================================================
// guild::drm — Disc generic dispatchers + spin-wait + CRC accumulator.
//              RECONSTRUCTED 1:1.
//
// Every branch here matches the gilde.exe decompile/disasm. Hardware leaves
// (ASPI/SPTI/timer/tick) go through the hooks; when a hook is null we record the
// selected backend (inert default) so the dispatch decision is observable and
// testable without touching real hardware. The CRC math (reused from
// compress::Crc16Update) and the spin-wait bound are exact and self-contained.
// =============================================================================

namespace guild::drm {

// -----------------------------------------------------------------------------
// CRC-16 accumulator — gilde.exe 0x1412330 (VIBE_Disc_AccumulateCrc)
// Per-file accumulator == reflected-0x8005 CRC-16 (init 0). Reuse the existing
// compress::Crc16Update reconstruction (no table/fold redefinition here).
// -----------------------------------------------------------------------------
u16 DiscAccumulateCrcBuffer(const u8* data, int len) {
    return guild::compress::Crc16Update(0, data, len);
}

// -----------------------------------------------------------------------------
// Spin-wait delay — gilde.exe 0x1412390
//   disasm 0x1412403: cmp ecx,[arg_0]; jnb -> exit  ==>  spin while
//   (unsigned)(cur - start) < a1.
// -----------------------------------------------------------------------------
int DiscSpinWaitDelay(const DiscState& st, const SpinWaitHooks& h, unsigned a1) {
    if (a1 == 0)                              // cmp [arg_0],0 / jnz ... else return
        return 0;

    if (st.modeFlag != 0) {                   // dword_1459FEC != 0 -> SPTI busy-poll
        if (!h.ticks)
            return 0;                          // inert: no tick source installed
        int start = h.ticks();                 // var_8 = ticks()   (0x14123eb)
        int cur   = h.ticks();                 // var_4 = ticks()   (0x14123f4)
        int iters = 0;
        // loop while (unsigned)(cur - start) < a1 (disasm: cmp ecx,arg0 / jnb).
        while (static_cast<unsigned>(cur - start) < a1) {
            cur = h.ticks();                   // var_4 = ticks()   (0x1412408)
            ++iters;
        }
        return iters;
    }

    // ASPI branch: arm timer (push 10h,0,handle,0,a1); on success sleep 50/10 ms.
    if (h.timerSetup &&
        h.timerSetup(a1, 0, h.timerHandle, 0, 16)) {
        if (h.sleepMs) {
            if (h.longSleep != 0)              // dword_145A054 != 0
                h.sleepMs(h.timerHandle, 50);  // push 32h
            else
                h.sleepMs(h.timerHandle, 10);  // push 0Ah
        }
    }
    return 0;
}

// -----------------------------------------------------------------------------
// Generic dispatchers
// -----------------------------------------------------------------------------

// gilde.exe 0x1412420 — VIBE_Disc_SetSpeed
int DiscSetSpeed(const DiscState& st, const DiscDispatchHooks& h, int a1, int a2) {
    if (st.modeFlag) {                                  // dword_1459FEC
        h.lastBackend = DiscDispatchHooks::Backend::kSpti;
        return h.sptiSetSpeed ? h.sptiSetSpeed(a1, a2) : 0;
    }
    h.lastBackend = DiscDispatchHooks::Backend::kAspi;
    return h.aspiSetSpeed ? h.aspiSetSpeed(a1, a2) : 0;
}

// gilde.exe 0x1412620 — VIBE_Disc_SetReadSpeed
int DiscSetReadSpeed(const DiscState& st, const DiscDispatchHooks& h, int a1) {
    if (st.modeFlag) {
        h.lastBackend = DiscDispatchHooks::Backend::kSpti;
        return h.sptiSetReadSpeed ? h.sptiSetReadSpeed(a1) : 0;
    }
    h.lastBackend = DiscDispatchHooks::Backend::kAspi;
    return h.aspiSetReadSpeed ? h.aspiSetReadSpeed(a1) : 0;
}

// gilde.exe 0x1412c30 — VIBE_Disc_ReadRawSector
int DiscReadRawSector(const DiscState& st, const DiscDispatchHooks& h, int a1, int a2, int a3) {
    if (st.modeFlag) {
        h.lastBackend = DiscDispatchHooks::Backend::kSpti;
        return h.sptiReadRawSector ? h.sptiReadRawSector(st.sptiDev, a1, a2, a3) : 0;
    }
    h.lastBackend = DiscDispatchHooks::Backend::kAspi;
    return h.aspiReadRawSector
               ? h.aspiReadRawSector(st.aspiHa, st.aspiTgt, st.aspiLun, a1, a2, a3)
               : 0;
}

// gilde.exe 0x14133a0 — VIBE_Disc_ReadSector
//   if (modeFlag)        -> SPTI(sptiDev, a1, a2, a3)
//   else if (aspiTarget) -> ASPI(ha, tgt, lun, a1, a2, a3)
//   else                 -> fallback dword_145CB48(fbDev, a2, a1, 0, sptiDev)
int DiscReadSector(const DiscState& st, const DiscDispatchHooks& h, int a1, int a2, int a3) {
    if (st.modeFlag) {
        h.lastBackend = DiscDispatchHooks::Backend::kSpti;
        return h.sptiReadSector ? h.sptiReadSector(st.sptiDev, a1, a2, a3) : 0;
    }
    if (st.aspiTarget) {
        h.lastBackend = DiscDispatchHooks::Backend::kAspi;
        return h.aspiReadSector
                   ? h.aspiReadSector(st.aspiHa, st.aspiTgt, st.aspiLun, a1, a2, a3)
                   : 0;
    }
    h.lastBackend = DiscDispatchHooks::Backend::kFallback;
    // dword_145CB48(dword_145AC54, a2, a1, 0, dword_1459FD0) — original swaps a1/a2.
    return h.fbReadSector ? h.fbReadSector(st.fbDev, a2, a1, 0, st.sptiDev) : 0;
}

// gilde.exe 0x14136f0 — VIBE_Disc_CheckMediaPresent
int DiscCheckMediaPresent(DiscState& st, const DiscDispatchHooks& h, int a1) {
    if (st.modeFlag) {
        h.lastBackend = DiscDispatchHooks::Backend::kSpti;
        return h.sptiTestUnitReady ? h.sptiTestUnitReady(st.sptiDev, a1) : 0;
    }
    if (st.aspiTarget) {
        h.lastBackend = DiscDispatchHooks::Backend::kAspi;
        return h.aspiTestUnitReady
                   ? h.aspiTestUnitReady(st.aspiHa, st.aspiTgt, st.aspiLun, a1)
                   : 0;
    }
    // fallback: dword_145EC10 = 0; query it; return ((status & 0x800) == 0).
    h.lastBackend = DiscDispatchHooks::Backend::kFallback;
    st.statusWord = 0;                                  // dword_145EC10 = 0
    if (h.fbQueryStatus)
        h.fbQueryStatus(st.fbDev, &st.statusWord, st.sptiDev);
    return (st.statusWord & 0x800) == 0;
}

// gilde.exe 0x14128c0 — VIBE_Disc_ReadTocEntry
//   if (tocReader && ((u8)aspiTarget & (modeFlag == 0)))
//       -> ASPI TOC decode(ha,tgt,lun, sectorBuf, &tocOut0, &tocOut1, 1)
//   else
//       -> fallback dword_145E2F4(fbDev, &tocOut0, &tocOut1, sptiDev)
int DiscReadTocEntry(DiscState& st, const DiscDispatchHooks& h) {
    if (st.tocReader &&
        ((static_cast<u8>(st.aspiTarget) & (st.modeFlag == 0 ? 1 : 0)) != 0)) {
        h.lastBackend = DiscDispatchHooks::Backend::kAspi;
        return h.aspiReadTocAndDecode
                   ? h.aspiReadTocAndDecode(st.aspiHa, st.aspiTgt, st.aspiLun,
                                            st.sectorBuf, &st.tocOut0, &st.tocOut1, 1)
                   : 0;
    }
    h.lastBackend = DiscDispatchHooks::Backend::kFallback;
    return h.fbReadTocEntry
               ? h.fbReadTocEntry(st.fbDev, &st.tocOut0, &st.tocOut1, st.sptiDev)
               : 0;
}

// gilde.exe 0x1413320 — VIBE_Disc_ReadSubchannelEntry
//   if (tocReader && !modeFlag && aspiTarget)
//       -> ASPI read sector(ha,tgt,lun, (int)sectorBuf, subchanArg, 0)
//   else
//       -> fallback dword_145A824(fbDev, subchanArg, sptiDev)
int DiscReadSubchannelEntry(DiscState& st, const DiscDispatchHooks& h) {
    if (st.tocReader && !st.modeFlag && st.aspiTarget) {
        h.lastBackend = DiscDispatchHooks::Backend::kAspi;
        const int bufArg =
            static_cast<int>(reinterpret_cast<std::intptr_t>(st.sectorBuf));
        return h.aspiReadSector
                   ? h.aspiReadSector(st.aspiHa, st.aspiTgt, st.aspiLun,
                                      bufArg, st.subchanArg, 0)
                   : 0;
    }
    h.lastBackend = DiscDispatchHooks::Backend::kFallback;
    return h.fbReadSubchannel
               ? h.fbReadSubchannel(st.fbDev, st.subchanArg, st.sptiDev)
               : 0;
}

} // namespace guild::drm
