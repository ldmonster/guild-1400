#include "audio/audio_leaves2.h"

namespace guild::audio {

// ---------------------------------------------------------------------------
// Inert default hooks (the Miles/Win32 boundary). These stand in for mss32.dll
// and user32.dll: they perform no real I/O but return the success-shaped values
// the originals expect, keeping the integer bookkeeping deterministic. Tests
// install their own hooks to observe / drive the calls.
// ---------------------------------------------------------------------------
void DriverManager::installDefaultHooks() {
    hooks_.startup = [] {};
    hooks_.shutdown = [] {};
    hooks_.waveOutClose = [](std::int32_t) {};
    hooks_.closeStream = [](std::int32_t) {};
    // AIL_digital_handle_release: original treats non-zero return as success.
    hooks_.digitalHandleRelease = [](std::int32_t) { return 1; };
    // AIL_digital_handle_reacquire: original treats 0 return as success.
    hooks_.digitalHandleReacquire = [](std::int32_t) { return 0; };
    hooks_.setDigitalMasterVolume = [](std::int32_t, unsigned) {};
    hooks_.activeSampleCount = [](std::int32_t) { return 0; };
    // PostMessageA: non-zero == posted.
    hooks_.postMessage = [](void*, unsigned, std::uintptr_t) { return 1; };
    hooks_.getTimerHighestDelay = [] { return 0; };
}

DriverManager::DriverManager() {
    installDefaultHooks();
}

// gilde.exe 0x449840 — VIBE_Audio_StartupMilesDriver
//   if ( dword_62EADC ) return -1;
//   AIL_startup(); AIL_last_error(); dword_62EADC = -1; return 0;
int DriverManager::startupMilesDriver() {
    if (driverInstalled_)            // already up
        return -1;
    hooks_.startup();                // AIL_startup()
    // AIL_last_error() — return value discarded by the original; skipped.
    driverInstalled_ = true;         // dword_62EADC = -1 (non-zero "installed")
    return 0;
}

// gilde.exe 0x449878 — VIBE_Audio_Shutdown_49878
//   if (!dword_62EADC) return -1;
//   CloseAllStreams(); ReleaseAllSampleHandles(); CloseAllDigitalOutputs();
//   AIL_shutdown(); dword_62EADC = 0; return 0;
int DriverManager::shutdownMilesDriver() {
    if (!driverInstalled_)
        return -1;
    closeAllStreams();               // VIBE_Audio_CloseAllStreams
    releaseAllSampleHandles();       // VIBE_Audio_ReleaseAllSampleHandles
    closeAllDigitalOutputs();        // VIBE_Audio_CloseAllDigitalOutputs
    hooks_.shutdown();               // AIL_shutdown()
    driverInstalled_ = false;        // dword_62EADC = 0
    return 0;
}

// gilde.exe 0x44ad0c — VIBE_Audio_GetTimerHighestDelay
//   return AIL_get_timer_highest_delay();
int DriverManager::getTimerHighestDelay() const {
    return hooks_.getTimerHighestDelay();
}

// gilde.exe 0x44aa94 — VIBE_Audio_FindDriverIndex (internal equivalent)
//   linear search of dword_62EA1C[0..15] for the output whose handle == a1.
//   First slot checked separately (matches the unrolled original); returns the
//   index or -1.
int DriverManager::findOutputByHandle(std::int32_t handle) const {
    if (outputs_[0].used && outputs_[0].handle == handle)
        return 0;
    for (int i = 1; i < kDrv2MaxOutputs; ++i) {
        if (outputs_[static_cast<std::size_t>(i)].used &&
            outputs_[static_cast<std::size_t>(i)].handle == handle)
            return i;
    }
    return -1;
}

// gilde.exe 0x449b28 — VIBE_Audio_CloseDigitalOutput
//   if (!dword_62EADC) return -1;
//   find index of a1 in dword_62EA1C; if (idx < 0) return -1;
//   AIL_waveOutClose(*a1);
//   free dword_62EA5C[idx]; free dword_62EA9C[idx]; free a1; clear dword_62EA1C[idx];
//   return 0;
int DriverManager::closeDigitalOutput(std::int32_t handle) {
    if (!driverInstalled_)
        return -1;
    int idx = findOutputByHandle(handle);
    if (idx < 0)
        return -1;
    DriverOutput& o = outputs_[static_cast<std::size_t>(idx)];
    hooks_.waveOutClose(o.handle);   // AIL_waveOutClose(*a1)
    // free the two slot arrays and the descriptor; clear the table slot.
    o.sampleSlots.clear();           // free dword_62EA5C[idx]
    o.streamSlots.clear();           // free dword_62EA9C[idx]
    o = DriverOutput{};              // free a1 + dword_62EA1C[idx] = 0
    return 0;
}

// gilde.exe 0x449bdc — VIBE_Audio_CloseAllDigitalOutputs
//   if (!dword_62EADC) return -1;
//   for i in 0..15: if (dword_62EA1C[i]) if (CloseDigitalOutput(...) == -1) v0 = -1;
//   return v0;
int DriverManager::closeAllDigitalOutputs() {
    int result = 0;
    if (!driverInstalled_)
        return -1;
    for (int i = 0; i < kDrv2MaxOutputs; ++i) {
        DriverOutput& o = outputs_[static_cast<std::size_t>(i)];
        if (o.used) {
            if (closeDigitalOutput(o.handle) == -1)
                result = -1;
        }
    }
    return result;
}

// gilde.exe 0x449c20 — VIBE_Audio_ReleaseDigitalDriver
//   if (!dword_62EADC) return -1;
//   find idx of a1; if (idx < 0) return -1;
//   if (AIL_digital_handle_release(*a1)) return 0; return -1;
int DriverManager::releaseDigitalDriver(std::int32_t handle) {
    if (!driverInstalled_)
        return -1;
    int idx = findOutputByHandle(handle);
    if (idx < 0)
        return -1;
    DriverOutput& o = outputs_[static_cast<std::size_t>(idx)];
    if (hooks_.digitalHandleRelease(o.handle))  // non-zero == success
        return 0;
    return -1;
}

// gilde.exe 0x449c80 — VIBE_Audio_ReleaseAllDigitalDrivers
//   if (!dword_62EADC) return -1;
//   for i: if (dword_62EA1C[i]) if (ReleaseDigitalDriver(...)) v0 = -1;
//   return v0;
// (the original's "if (Release...) v0=-1" treats a -1 return as failure)
int DriverManager::releaseAllDigitalDrivers() {
    int result = 0;
    if (!driverInstalled_)
        return -1;
    for (int i = 0; i < kDrv2MaxOutputs; ++i) {
        DriverOutput& o = outputs_[static_cast<std::size_t>(i)];
        if (o.used) {
            if (releaseDigitalDriver(o.handle))
                result = -1;
        }
    }
    return result;
}

// gilde.exe 0x449cc8 — VIBE_Audio_ReacquireDigitalDriver
//   v3 = result (the output ptr a1);
//   if (dword_62EADC && a3 (msg) >= 0x400) {
//     find idx of a1 (scanning dword_62EA1C, byte stride 4 == idx stride 1);
//     if (idx >= 0) {
//       result = AIL_digital_handle_reacquire(*a1);
//       if (!result) return PostMessageA(a2, a3, a1, 0);
//     }
//   }
//   return result;  // 0 when nothing posted / reacquire failed
int DriverManager::reacquireDigitalDriver(std::int32_t handle, void* hwnd, unsigned msg) {
    int result = static_cast<int>(handle);  // mirrors result = a1 entry value
    if (driverInstalled_ && msg >= 0x400) {
        // The original uses eax as a byte offset (0,4,...) into dword_62EA1C while
        // searching; when the handle is NOT found the loop breaks at eax==64 and
        // that value (0x40) is returned. When found at index i it holds eax==4*i
        // only transiently, then v6 (=i) is checked (>= 0 always) and eax is
        // overwritten with the reacquire result / PostMessageA return. So: found
        // -> reacquire path; not found -> return 64. (Disasm @0x449cf8 jge ->
        // loc_449D06 retn with eax==0x40.)
        int idx = findOutputByHandle(handle);
        if (idx >= 0) {
            DriverOutput& o = outputs_[static_cast<std::size_t>(idx)];
            result = hooks_.digitalHandleReacquire(o.handle);
            if (result == 0)
                return hooks_.postMessage(hwnd, msg,
                                          static_cast<std::uintptr_t>(
                                              static_cast<std::uint32_t>(handle)));
        } else {
            result = 64;                       // eax == 0x40 at the not-found break
        }
    }
    return result;
}

// gilde.exe 0x449d2c — VIBE_Audio_ReacquireAllDigitalDrivers
// Disasm (reference of record). The return value is eax, carried across the
// 16-slot loop; whichever code path the LAST processed slot took leaves its
// value in eax. Per-slot (ebx = 4*index walking dword_62EA1C):
//   * empty slot (ecx == 0): eax untouched, advance.        (449d49/449d4d)
//   * used slot: eax = hwnd; eax = dword_62EADC (== -1 when installed); if
//     msg < 0x400 -> eax = -1, advance.                     (449d66..449d79)
//   * used + msg >= 0x400: search the table for the slot's handle (it is its own
//     table entry, so always found at its index -> the result>=64 not-found
//     path is unreachable here); eax = reacquire(handle); if eax == 0 ->
//     eax = PostMessageA(hwnd, msg, handle, 0).             (449d9c..449db6)
// Initial eax (no used slot processed / driver down) is the entry value = hwnd.
// dword_62EADC is -1 when the driver is up (StartupMilesDriver sets it to -1).
int DriverManager::reacquireAllDigitalDrivers(void* hwnd, unsigned msg) {
    // Seed = entry eax = the hwnd argument (returned if no used slot is touched).
    int result = static_cast<int>(reinterpret_cast<std::uintptr_t>(hwnd));
    if (!driverInstalled_)
        return result;                       // jz loc_449D55 -> retn eax(hwnd)
    for (int v3 = 0; v3 < kDrv2MaxOutputs; ++v3) {
        DriverOutput& o = outputs_[static_cast<std::size_t>(v3)];
        if (!o.used)
            continue;                        // ecx == 0 -> 449d4d (eax untouched)
        result = -1;                         // eax = dword_62EADC (== -1, installed)
        if (msg < 0x400)
            continue;                        // jb loc_449D4D (eax stays -1)
        // The slot's handle is its own table entry: search always succeeds, so
        // the not-found (eax>=64) path never triggers for ReacquireAll.
        result = hooks_.digitalHandleReacquire(o.handle);
        if (result == 0) {
            result = hooks_.postMessage(hwnd, msg,
                                        static_cast<std::uintptr_t>(
                                            static_cast<std::uint32_t>(o.handle)));
        }
    }
    return result;
}

// gilde.exe 0x44a5f0 — VIBE_Audio_CloseStream
//   if (!dword_62EADC) return -1;
//   driverIdx = LookupStreamDriverIndex(a1); if (driverIdx < 0) return -1;
//   slot = LookupStreamHandleIndex(a1);      if (slot < 0) return -1;
//   AIL_close_stream(a1);
//   --*(dword_62EA1C[driverIdx] + 20);            // allocatedSampleCount
//   dword_62EA9C[driverIdx][slot] = 0;            // clear stream slot
//   return 0;
int DriverManager::closeStream(std::int32_t stream) {
    if (!driverInstalled_)
        return -1;
    // Locate the output + slot that own this stream handle (LookupStream*Index).
    int driverIdx = -1;
    int slot = -1;
    for (int i = 0; i < kDrv2MaxOutputs && driverIdx < 0; ++i) {
        DriverOutput& o = outputs_[static_cast<std::size_t>(i)];
        if (!o.used)
            continue;
        for (int s = 0; s < static_cast<int>(o.streamSlots.size()); ++s) {
            if (o.streamSlots[static_cast<std::size_t>(s)] == stream) {
                driverIdx = i;
                slot = s;
                break;
            }
        }
    }
    if (driverIdx < 0)
        return -1;
    if (slot < 0)
        return -1;
    DriverOutput& o = outputs_[static_cast<std::size_t>(driverIdx)];
    hooks_.closeStream(stream);              // AIL_close_stream(a1)
    --o.allocatedSampleCount;                // --*(+0x14)
    o.streamSlots[static_cast<std::size_t>(slot)] = 0;
    return 0;
}

// gilde.exe 0x44a6d8 — VIBE_Audio_CloseAllStreams
//   if (!dword_62EADC) return -1;
//   for i in 0..15: if (dword_62EA1C[i])
//     for s in [0, *(dword_62EA1C[i]+24)):          // +0x18 capacity
//       h = dword_62EA9C[i][s]; if (h) if (CloseStream(h)) v0 = -1;
//   return v0;
int DriverManager::closeAllStreams() {
    int result = 0;
    if (!driverInstalled_)
        return -1;
    for (int i = 0; i < kDrv2MaxOutputs; ++i) {
        DriverOutput& o = outputs_[static_cast<std::size_t>(i)];
        if (!o.used)
            continue;
        for (int s = 0; s < o.slotCapacity; ++s) {
            std::int32_t h = (s < static_cast<int>(o.streamSlots.size()))
                                 ? o.streamSlots[static_cast<std::size_t>(s)]
                                 : 0;
            if (h) {
                if (closeStream(h))
                    result = -1;
            }
        }
    }
    return result;
}

// gilde.exe 0x44a64c — VIBE_Audio_CloseDriverStreams
//   if (!dword_62EADC) return -1;
//   find idx of a1 (16-entry scan); if not found return -1; if (idx < 0) return -1;
//   if (*(a1+24) > 0):                                 // +0x18 capacity
//     for s in [0, *(a1+24)):
//       h = dword_62EA9C[idx][s]; if (h) if (CloseStream(h)) v2 = -1;
//   return v2;
int DriverManager::closeDriverStreams(std::int32_t handle) {
    int result = 0;
    if (!driverInstalled_)
        return -1;
    int idx = findOutputByHandle(handle);
    if (idx < 0)
        return -1;
    DriverOutput& o = outputs_[static_cast<std::size_t>(idx)];
    if (o.slotCapacity > 0) {
        for (int s = 0; s < o.slotCapacity; ++s) {
            std::int32_t h = (s < static_cast<int>(o.streamSlots.size()))
                                 ? o.streamSlots[static_cast<std::size_t>(s)]
                                 : 0;
            if (h) {
                if (closeStream(h))
                    result = -1;
            }
        }
    }
    return result;
}

// gilde.exe 0x44a084 — VIBE_Audio_ReleaseDriverSampleHandles
//   if (!dword_62EADC) return -1;
//   find idx of a1 (16-entry scan); if not found return -1; if (idx < 0) return -1;
//   if (*(a1+24) > 0):                                 // +0x18 capacity
//     for s in [0, *(a1+24)):
//       h = dword_62EA5C[idx][s]; if (h) if (ReleaseSampleHandle(h) == -1) v2 = -1;
//   return v2;
int DriverManager::releaseDriverSampleHandles(std::int32_t handle) {
    int result = 0;
    if (!driverInstalled_)
        return -1;
    int idx = findOutputByHandle(handle);
    if (idx < 0)
        return -1;
    DriverOutput& o = outputs_[static_cast<std::size_t>(idx)];
    if (o.slotCapacity > 0) {
        for (int s = 0; s < o.slotCapacity; ++s) {
            std::int32_t h = (s < static_cast<int>(o.sampleSlots.size()))
                                 ? o.sampleSlots[static_cast<std::size_t>(s)]
                                 : 0;
            if (h) {
                if (releaseSampleHandleSlot(h) == -1)
                    result = -1;
            }
        }
    }
    return result;
}

// gilde.exe 0x44a110 — VIBE_Audio_ReleaseAllSampleHandles
//   if (!dword_62EADC) return -1;
//   for i in 0..15: if (dword_62EA1C[i])
//     for s in [0, *(dword_62EA1C[i]+24)):           // +0x18 capacity
//       h = dword_62EA5C[i][s]; if (h) if (ReleaseSampleHandle(h) == -1) v0 = -1;
//   return v0;
int DriverManager::releaseAllSampleHandles() {
    int result = 0;
    if (!driverInstalled_)
        return -1;
    for (int i = 0; i < kDrv2MaxOutputs; ++i) {
        DriverOutput& o = outputs_[static_cast<std::size_t>(i)];
        if (!o.used)
            continue;
        for (int s = 0; s < o.slotCapacity; ++s) {
            std::int32_t h = (s < static_cast<int>(o.sampleSlots.size()))
                                 ? o.sampleSlots[static_cast<std::size_t>(s)]
                                 : 0;
            if (h) {
                if (releaseSampleHandleSlot(h) == -1)
                    result = -1;
            }
        }
    }
    return result;
}

// Internal mirror of VIBE_Audio_ReleaseSampleHandle @0x44a028's bookkeeping:
//   LookupSampleDriverIndex; LookupSampleHandleIndex; AIL_release_sample_handle;
//   dword_62EA5C[idx][slot] = 0;        // clear sample slot   (44a06f)
//   --*(dword_62EA1C[idx] + 20);        // allocatedSampleCount (44a079)
// The decrement is UNCONDITIONAL in the binary (no `> 0` guard); model it 1:1.
// Returns 0 on success, -1 if the handle is not currently allocated.
int DriverManager::releaseSampleHandleSlot(std::int32_t handle) {
    for (int i = 0; i < kDrv2MaxOutputs; ++i) {
        DriverOutput& o = outputs_[static_cast<std::size_t>(i)];
        if (!o.used)
            continue;
        for (std::size_t s = 0; s < o.sampleSlots.size(); ++s) {
            if (o.sampleSlots[s] == handle) {
                o.sampleSlots[s] = 0;
                --o.allocatedSampleCount;   // unconditional, matches 44a079
                return 0;
            }
        }
    }
    return -1;
}

// gilde.exe 0x449fcc — VIBE_Audio_GetActiveSampleCount
//   if (!dword_62EADC) return -1;
//   find idx of a1 (16-entry scan); if (idx < 0) return -1;
//   *a2 = AIL_active_sample_count(*a1); return 0;
int DriverManager::getActiveSampleCount(std::int32_t handle, int* out) {
    if (!driverInstalled_)
        return -1;
    int idx = findOutputByHandle(handle);
    if (idx < 0)
        return -1;
    DriverOutput& o = outputs_[static_cast<std::size_t>(idx)];
    if (out)
        *out = hooks_.activeSampleCount(o.handle);
    return 0;
}

} // namespace guild::audio
