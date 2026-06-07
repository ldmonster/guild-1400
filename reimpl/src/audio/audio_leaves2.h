#pragma once
// guild::audio — Miles digital-driver MANAGEMENT layer of gilde.exe.
//
// The original wraps Miles Sound System (mss32.dll). A single global flag
// dword_62EADC records whether the Miles digital driver has been started
// (VIBE_Audio_StartupMilesDriver = AIL_startup). Up to 16 digital outputs are
// tracked through three parallel arrays keyed by output index:
//   dword_62EA1C[16] — output descriptor pointer (0 = empty slot)
//   dword_62EA5C[16] — that output's sample-handle slot array
//   dword_62EA9C[16] — that output's stream-handle slot array
// Each output descriptor stores: +0x00 the Miles driver/output handle, +0x14
// (offset 20) the live allocated-sample count, +0x18 (offset 24) the per-output
// slot capacity. The global preference dword_62EAE0 (AIL_get_preference(1)) is
// the default max-sample-handles value.
//
// This file reconstructs the DRIVER-MANAGEMENT functions that walk those tables
// (start/shutdown, open/close outputs, release/reacquire drivers, close streams,
// release sample handles, query active counts). All terminal Miles MSS / Win32
// calls (AIL_startup, AIL_shutdown, AIL_waveOutClose, AIL_close_stream,
// AIL_digital_handle_release, AIL_digital_handle_reacquire,
// AIL_set_digital_master_volume, AIL_active_sample_count, PostMessageA) route
// through an installable MilesHooks struct whose default implementations are
// inert (defined in audio_leaves2.cpp). The integer bookkeeping is preserved 1:1.
//
// Recovered from:
//   VIBE_Audio_StartupMilesDriver          @0x449840
//   VIBE_Audio_Shutdown_49878              @0x449878
//   VIBE_Audio_IsInitialized               @0x449570  (driverInstalled query)
//   VIBE_Audio_GetTimerHighestDelay        @0x44ad0c
//   VIBE_Audio_CloseDigitalOutput          @0x449b28
//   VIBE_Audio_CloseAllDigitalOutputs      @0x449bdc
//   VIBE_Audio_ReleaseDigitalDriver        @0x449c20
//   VIBE_Audio_ReleaseAllDigitalDrivers    @0x449c80
//   VIBE_Audio_ReacquireDigitalDriver      @0x449cc8
//   VIBE_Audio_ReacquireAllDigitalDrivers  @0x449d2c
//   VIBE_Audio_CloseStream                 @0x44a5f0
//   VIBE_Audio_CloseAllStreams             @0x44a6d8
//   VIBE_Audio_CloseDriverStreams          @0x44a64c
//   VIBE_Audio_ReleaseDriverSampleHandles  @0x44a084
//   VIBE_Audio_ReleaseAllSampleHandles     @0x44a110
//   VIBE_Audio_GetActiveSampleCount        @0x449fcc
#include "guild/common/types.h"
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace guild::audio {

// Up to 16 digital outputs (dword_62EA1C[16]).
constexpr int kDrv2MaxOutputs = 16;

// Installable Miles / Win32 boundary. The originals call into mss32.dll and
// user32.dll; here every terminal call routes through one of these hooks. The
// defaults (installed by the DriverManager ctor) are inert: they record nothing
// and return success-shaped values, so the bookkeeping stays deterministic in
// tests. Each hook receives the Miles handle (the output descriptor's +0x00
// field) and the relevant arguments, exactly as the original passed them.
struct MilesHooks {
    // AIL_startup() — bring the digital driver up. Default: no-op.
    std::function<void()> startup;
    // AIL_shutdown() — tear the digital driver down. Default: no-op.
    std::function<void()> shutdown;
    // AIL_waveOutClose(handle) — close one output device. Default: no-op.
    std::function<void(std::int32_t handle)> waveOutClose;
    // AIL_close_stream(stream) — close one stream handle. Default: no-op.
    std::function<void(std::int32_t stream)> closeStream;
    // AIL_digital_handle_release(handle) — release a driver, returns non-zero ok.
    std::function<int(std::int32_t handle)> digitalHandleRelease;
    // AIL_digital_handle_reacquire(handle) — reacquire a driver, returns 0 on ok.
    std::function<int(std::int32_t handle)> digitalHandleReacquire;
    // AIL_set_digital_master_volume(handle, vol) — set master volume (0..127).
    std::function<void(std::int32_t handle, unsigned vol)> setDigitalMasterVolume;
    // AIL_active_sample_count(handle) — number of live samples on the output.
    std::function<int(std::int32_t handle)> activeSampleCount;
    // PostMessageA(hwnd, msg, wparam, 0) — Win32 message post; returns non-zero ok.
    std::function<int(void* hwnd, unsigned msg, std::uintptr_t wparam)> postMessage;
    // AIL_get_timer_highest_delay() — Miles timer query.
    std::function<int()> getTimerHighestDelay;
};

// One digital output (the descriptor pointed to by dword_62EA1C[i]).
struct DriverOutput {
    bool used = false;                  // dword_62EA1C[i] != 0
    std::int32_t handle = 0;            // +0x00 Miles output/driver handle
    int allocatedSampleCount = 0;       // +0x14 (offset 20) live sample count
    int slotCapacity = 0;               // +0x18 (offset 24) per-output capacity
    std::vector<std::int32_t> sampleSlots; // dword_62EA5C[i] (0 = free)
    std::vector<std::int32_t> streamSlots; // dword_62EA9C[i] (0 = free)
};

// The Miles digital-driver manager: the 16-output table, the driver-installed
// flag (dword_62EADC) and the global preference (dword_62EAE0). Distinct from
// DigitalAudio (digital_output.cpp) — this models the management/teardown layer.
class DriverManager {
public:
    DriverManager();

    MilesHooks& hooks() { return hooks_; }
    const MilesHooks& hooks() const { return hooks_; }

    // --- direct table access (the parallel global arrays) ------------------
    int outputCount() const { return kDrv2MaxOutputs; }
    DriverOutput& output(int i) { return outputs_[static_cast<std::size_t>(i)]; }
    const DriverOutput& output(int i) const { return outputs_[static_cast<std::size_t>(i)]; }

    bool driverInstalled() const { return driverInstalled_; }      // dword_62EADC
    int  globalPreference() const { return globalPreference_; }    // dword_62EAE0
    void setGlobalPreference(int v) { globalPreference_ = v; }

    // VIBE_Audio_StartupMilesDriver @0x449840 — AIL_startup; arm dword_62EADC.
    // Returns -1 if already up, else 0.
    int startupMilesDriver();

    // VIBE_Audio_Shutdown_49878 @0x449878 — close all streams, release all
    // sample handles, close all outputs, AIL_shutdown, clear dword_62EADC.
    // Returns -1 if not up, else 0.
    int shutdownMilesDriver();

    // VIBE_Audio_IsInitialized @0x449570 models dword_62E8FC; the driver-up flag
    // is dword_62EADC. We expose the driver-installed flag query directly.
    bool isDriverInstalled() const { return driverInstalled_; }

    // VIBE_Audio_GetTimerHighestDelay @0x44ad0c — AIL_get_timer_highest_delay().
    int getTimerHighestDelay() const;

    // VIBE_Audio_FindDriverIndex (same shape as the @0x44aa94 free-fn): index of
    // the output whose handle == `handle`, or -1. Used internally; the @0x44aa94
    // free function itself is already reconstructed in audio_leaves.cpp.
    int findOutputByHandle(std::int32_t handle) const;

    // VIBE_Audio_CloseDigitalOutput @0x449b28 — find output by handle,
    // AIL_waveOutClose, free its slot arrays, clear dword_62EA1C[i]. Returns 0 on
    // success, -1 if driver down / handle unknown.
    int closeDigitalOutput(std::int32_t handle);

    // VIBE_Audio_CloseAllDigitalOutputs @0x449bdc — close every used output.
    // Returns -1 if driver down or any close failed, else 0.
    int closeAllDigitalOutputs();

    // VIBE_Audio_ReleaseDigitalDriver @0x449c20 — find output by handle,
    // AIL_digital_handle_release. Returns 0 on success, -1 otherwise.
    int releaseDigitalDriver(std::int32_t handle);

    // VIBE_Audio_ReleaseAllDigitalDrivers @0x449c80 — release every used output.
    int releaseAllDigitalDrivers();

    // VIBE_Audio_ReacquireDigitalDriver @0x449cc8 — reacquire one driver; on
    // success PostMessageA(hwnd,msg,handle,0). Requires msg >= 0x400. Returns the
    // last produced value (0 if nothing done) exactly as the original.
    int reacquireDigitalDriver(std::int32_t handle, void* hwnd, unsigned msg);

    // VIBE_Audio_ReacquireAllDigitalDrivers @0x449d2c — reacquire every used
    // driver, posting per success. Returns the running result value.
    int reacquireAllDigitalDrivers(void* hwnd, unsigned msg);

    // VIBE_Audio_CloseStream @0x44a5f0 — locate stream by (driver index, slot),
    // AIL_close_stream, decrement output +0x14, clear the stream slot. Returns 0
    // on success, -1 if driver down / stream unknown.
    int closeStream(std::int32_t stream);

    // VIBE_Audio_CloseAllStreams @0x44a6d8 — close every non-zero stream slot of
    // every used output (slot range [0, +0x18)). Returns -1 if driver down or any
    // close failed, else 0.
    int closeAllStreams();

    // VIBE_Audio_CloseDriverStreams @0x44a64c — close every non-zero stream slot
    // of the single output whose handle == `handle` (range [0, +0x18)).
    int closeDriverStreams(std::int32_t handle);

    // VIBE_Audio_ReleaseDriverSampleHandles @0x44a084 — release every non-zero
    // sample slot of the output whose handle == `handle` (range [0, +0x18)).
    int releaseDriverSampleHandles(std::int32_t handle);

    // VIBE_Audio_ReleaseAllSampleHandles @0x44a110 — release every non-zero
    // sample slot of every used output (range [0, +0x18)).
    int releaseAllSampleHandles();

    // VIBE_Audio_GetActiveSampleCount @0x449fcc — for the output whose handle ==
    // `handle`, write AIL_active_sample_count to *out. Returns 0 on success, -1 if
    // driver down / handle unknown.
    int getActiveSampleCount(std::int32_t handle, int* out);

    // Helper used by ReleaseSampleHandle modelling (mirrors @0x44a028's core):
    // clear the matching sample slot across outputs, decrement +0x14. Returns 0
    // on success, -1 if not found. Defined here so the driver-management
    // functions above are self-contained (the @0x44a028 free fn lives in
    // digital_output.cpp; we keep an internal equivalent to avoid cross-owner
    // coupling).
    int releaseSampleHandleSlot(std::int32_t handle);

private:
    void installDefaultHooks();

    MilesHooks hooks_;
    bool driverInstalled_ = false;                  // dword_62EADC
    int  globalPreference_ = 0;                     // dword_62EAE0
    std::array<DriverOutput, kDrv2MaxOutputs> outputs_{}; // the 16 parallel slots
};

} // namespace guild::audio
