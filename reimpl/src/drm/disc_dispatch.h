#pragma once
#include <cstdint>
#include "guild/common/types.h"

// =============================================================================
// guild::drm — Disc generic dispatchers + spin-wait + CRC accumulator
//              (RECONSTRUCTED 1:1)
// =============================================================================
//
// SCOPE
//   This unit reconstructs, byte-for-byte, the *pure logic* of the copy-protection
//   cluster's disc layer:
//     * the GENERIC dispatchers that route a disc operation to either the SPTI
//       or the ASPI hardware backend (and, for some ops, a legacy/fallback driver)
//       based on the recovered mode flags
//       (VIBE_Disc_SetSpeed/SetReadSpeed/ReadSector/ReadRawSector/CheckMediaPresent/
//        ReadTocEntry/ReadSubchannelEntry),
//     * the spin-wait delay loop bound (VIBE_Disc_SpinWaitDelay), and
//     * the CRC-16 accumulator (VIBE_Disc_AccumulateCrc @0x1412330), expressed
//       over a buffer so the polynomial is golden-testable.
//
//   The dispatch-selection branch logic, the spin-wait bound, and the CRC math are
//   reconstructed EXACTLY and are testable headless. The ASPI/SPTI backend calls
//   and the timer/tick source are the hardware boundary; they are routed through
//   INERT-DEFAULT hook structs (DiscDispatchHooks / SpinWaitHooks) so the portable
//   build links and the *selection* is verifiable, while no real OS/SCSI call is
//   ever made unless a real backend is installed. NEVER faked: the selection logic
//   is the original's.
//
// ODR / REUSE
//   The CRC-16 itself (VIBE_Crc16_InitTable @0x1418a40 / VIBE_Crc16_Update
//   @0x1418ae0 — reflected 0x8005 / CRC-16-ARC, seed 0xC0C1, step v=(2*v)^0x4003)
//   is ALREADY reconstructed in src/compress/crc.{h,cpp} as
//   guild::compress::Crc16Update. We REUSE it here (we do NOT redefine the table
//   or the fold). VIBE_Disc_AccumulateCrc's own CRC accumulator is exactly that
//   reflected-0x8005 CRC-16 with init 0 (HashFile initialises word_1460658 = 0),
//   so DiscAccumulateCrcBuffer below delegates to compress::Crc16Update(0,...).
//
//   The existing src/drm/drm_stub.{h,cpp} provide success-returning *stubs* of the
//   live boot path (CheckMediaPresent / MeasureSectorTiming / ...) as plain free
//   functions. This unit is the faithful *logic* reconstruction of the disc
//   layer's reusable pieces and the dispatch tree; names here are distinct
//   (DiscDispatch* taking DiscState/DiscDispatchHooks) so there is no ODR clash
//   with those stubs.
// =============================================================================

namespace guild::drm {

// -----------------------------------------------------------------------------
// CRC-16 accumulator — gilde.exe 0x1412330 (VIBE_Disc_AccumulateCrc)
//
//   __int16 VIBE_Disc_AccumulateCrc(this, count, table, len):
//     if (count < 2) return 1;
//     VIBE_Crc16_InitTable();
//     for (i = 1; i < count; ++i)
//         VIBE_Crc16_HashFile(table[i], len);   // CRC-16 of a file, init 0
//     return word_1460658;                       // last file's CRC-16
//
//   HashFile (0x1418b50) sets word_1460658 = 0 then folds the file bytes with
//   VIBE_Crc16_Update. The per-file CRC is therefore the reflected-0x8005 CRC-16
//   (init 0) of that file's bytes. The file enumeration / I/O is the hardware
//   boundary; the *math* is exactly compress::Crc16Update(0, data, len). This
//   helper exposes that math over an in-memory buffer for golden testing.
// -----------------------------------------------------------------------------

// CRC-16 (reflected 0x8005 / ARC, init 0) of an in-memory buffer — the exact
// per-file accumulator value VIBE_Disc_AccumulateCrc computes via HashFile.
// gilde.exe 0x1412330 / 0x1418ae0 — delegates to guild::compress::Crc16Update.
u16 DiscAccumulateCrcBuffer(const u8* data, int len);

// -----------------------------------------------------------------------------
// Hardware boundary: inert-default hooks.
//
// The dispatchers call exactly ONE of these per operation, selected 1:1 by the
// recovered mode flags. Defaults are inert (return the "no real device" value)
// so the selection logic is verifiable headless. A test or a real backend can
// install hooks and observe which one the dispatcher invoked.
// -----------------------------------------------------------------------------
struct DiscDispatchHooks {
    // Backend tag recorded so tests can see which path the dispatcher chose.
    enum class Backend { kNone, kSpti, kAspi, kFallback };

    // --- SPTI backend (used when modeFlag != 0) ---
    int (*sptiSetSpeed)(int a1, int a2)                  = nullptr; // 0x1412560
    int (*sptiSetReadSpeed)(int a1)                      = nullptr; // 0x14127a0
    int (*sptiReadSector)(int dev, int a1, int a2, int a3)    = nullptr; // 0x14135b0
    int (*sptiReadRawSector)(int dev, int a1, int a2, int a3) = nullptr; // 0x1412ca0
    int (*sptiTestUnitReady)(int dev, int a1)            = nullptr; // 0x1413860

    // --- ASPI backend (used when modeFlag == 0) ---
    int (*aspiSetSpeed)(int a1, int a2)                  = nullptr; // 0x1412460
    int (*aspiSetReadSpeed)(int a1)                      = nullptr; // 0x1412660
    int (*aspiReadSector)(int ha, int tgt, int lun, int a1, int a2, int a3)    = nullptr; // 0x1413430
    int (*aspiReadRawSector)(int ha, int tgt, int lun, int a1, int a2, int a3) = nullptr; // 0x1412e10
    int (*aspiTestUnitReady)(int ha, int tgt, int lun, int a1) = nullptr; // 0x1413790
    int (*aspiReadTocAndDecode)(int ha, int tgt, int lun, void* buf,
                                int* out0, int* out1, int a7)  = nullptr; // 0x1412950

    // --- legacy/fallback driver path (the else branch) ---
    int (*fbReadSector)(int dev, int lba, int n, int flag, int a5) = nullptr; // dword_145CB48
    void(*fbQueryStatus)(int dev, int* outStatus, int a3)          = nullptr; // dword_14603C0
    int (*fbReadTocEntry)(int dev, int* out0, int* out1, int a4)   = nullptr; // dword_145E2F4
    int (*fbReadSubchannel)(int dev, int a2, int a3)               = nullptr; // dword_145A824

    // Last backend the dispatcher selected (set by the inert defaults / tests).
    mutable Backend lastBackend = Backend::kNone;
};

// -----------------------------------------------------------------------------
// Disc state mirrors the original's globals consumed by the dispatchers. The
// branch selection reads exactly these (names map to the gilde.exe globals).
// -----------------------------------------------------------------------------
struct DiscState {
    // dword_1459FEC — primary mode flag. Non-zero => SPTI backend, zero => ASPI.
    int  modeFlag      = 0;
    // dword_145A14C — "ASPI target valid" flag (ASPI vs. fallback selector).
    int  aspiTarget    = 0;
    // dword_145DB20 — "TOC reader available" gate (ReadTocEntry / ReadSubchannel).
    int  tocReader     = 0;

    // ASPI addressing triple: dword_145A13C / _145A140 / _145A144.
    int  aspiHa = 0, aspiTgt = 0, aspiLun = 0;
    // dword_1459FD0 — SPTI device handle / last arg to the fallback driver.
    int  sptiDev       = 0;
    // dword_145AC54 — fallback driver device handle.
    int  fbDev         = 0;
    // dword_145F3C0 — scratch sector buffer pointer (opaque here).
    void* sectorBuf    = nullptr;
    // dword_145A55C — subchannel scratch arg.
    int  subchanArg    = 0;
    // dword_145CAE4 / dword_145DB0C — TOC decode out-params (opaque here).
    int  tocOut0 = 0, tocOut1 = 0;
    // dword_145EC10 — drive status word filled by the fallback status query.
    int  statusWord    = 0;
};

// -----------------------------------------------------------------------------
// Generic dispatchers — reconstructed 1:1. Each selects the SPTI / ASPI /
// fallback backend exactly as the original branch logic does.
// -----------------------------------------------------------------------------

// gilde.exe 0x1412420 — VIBE_Disc_SetSpeed (__stdcall(int,int))
int DiscSetSpeed(const DiscState& st, const DiscDispatchHooks& h, int a1, int a2);

// gilde.exe 0x1412620 — VIBE_Disc_SetReadSpeed (__stdcall(int))
int DiscSetReadSpeed(const DiscState& st, const DiscDispatchHooks& h, int a1);

// gilde.exe 0x1412c30 — VIBE_Disc_ReadRawSector (__stdcall(int,int,int))
int DiscReadRawSector(const DiscState& st, const DiscDispatchHooks& h, int a1, int a2, int a3);

// gilde.exe 0x14133a0 — VIBE_Disc_ReadSector (__stdcall(int,int,int))
int DiscReadSector(const DiscState& st, const DiscDispatchHooks& h, int a1, int a2, int a3);

// gilde.exe 0x14136f0 — VIBE_Disc_CheckMediaPresent (__stdcall(int))
// SPTI/ASPI delegate; fallback path zeros the status word, queries it, and
// returns ((statusWord & 0x800) == 0).
int DiscCheckMediaPresent(DiscState& st, const DiscDispatchHooks& h, int a1);

// gilde.exe 0x14128c0 — VIBE_Disc_ReadTocEntry
int DiscReadTocEntry(DiscState& st, const DiscDispatchHooks& h);

// gilde.exe 0x1413320 — VIBE_Disc_ReadSubchannelEntry
int DiscReadSubchannelEntry(DiscState& st, const DiscDispatchHooks& h);

// -----------------------------------------------------------------------------
// Spin-wait delay (gilde.exe 0x1412390 — VIBE_Disc_SpinWaitDelay).
//
//   void VIBE_Disc_SpinWaitDelay(unsigned a1):
//     if (!a1) return;
//     if (dword_1459FEC) {                         // SPTI: busy-poll a tick counter
//         start = ticks();
//         cur   = ticks();
//         while ((unsigned)(cur - start) < a1)     // disasm: cmp ecx,arg0 / jnb
//             cur = ticks();
//     } else if (dword_145AC60(a1, 0, handle, 0, 16)) {  // ASPI: arm timer
//         dword_145F360(handle, dword_145A054 ? 50 : 10); // then sleep ms
//     }
//
// The tick/timer source is the hardware boundary -> hooks below. We reconstruct
// the *loop bound* (unsigned `>= a1`) and *branch* exactly. The return value is
// the number of tick-source calls the busy loop BODY performed, so the bound is
// golden-testable. Pure: makes no real OS call when hooks are null.
// -----------------------------------------------------------------------------
struct SpinWaitHooks {
    // SPTI tick source (dword_145E2FC). Monotonic counter; the loop spins until
    // (unsigned)(ticks() - start) >= a1.
    int (*ticks)()                                         = nullptr;
    // ASPI timer setup (dword_145AC60): returns non-zero on success.
    int (*timerSetup)(unsigned a1, int, int handle, int, int) = nullptr;
    // ASPI sleep (dword_145F360): sleep(handle, ms).
    void(*sleepMs)(int handle, int ms)                     = nullptr;

    // Mirrors dword_145F3B8 (timer handle) and dword_145A054 ("long sleep" flag).
    int timerHandle = 0;
    int longSleep   = 0;
};

// gilde.exe 0x1412390 — VIBE_Disc_SpinWaitDelay.
// Returns the count of busy-loop-body tick calls (SPTI branch with a tick hook),
// else 0 (ASPI / no-hook / a1==0 paths).
int DiscSpinWaitDelay(const DiscState& st, const SpinWaitHooks& h, unsigned a1);

} // namespace guild::drm
