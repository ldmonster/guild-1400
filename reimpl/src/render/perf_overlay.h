#pragma once
// =============================================================================
// guild::render — in-game PERFORMANCE OVERLAY (Steam-style FPS counter).
//
// An ADDITIVE host diagnostic (the original game has no FPS counter — this is not
// reverse-engineered engine logic). It mirrors Steam's in-game performance monitor
// (help.steampowered.com .../3462-CD4C-36BD-5767): a small readout composited into
// a chosen screen corner showing the frame rate, with progressively more detail —
//   level 1: FPS only            (like Steam "FPS")
//   level 2: FPS + frame time ms
//   level 3: + a frame-time graph (like Steam's detailed view)
// and a high-contrast dark box behind it for legibility.
//
// It draws onto the final composited render::Surface (16/32 bpp) right before the
// present, using the existing render::DrawText + surface primitives — no new deps,
// no platform calls. Any frame loop feeds Frame(timeMs) once per presented frame
// and calls Draw(fb) before present. A process-global instance (GlobalPerfOverlay)
// is configured from the environment so it can be turned on without code changes.
// =============================================================================
#include "guild/common/types.h"

#include <cstdint>

namespace guild::render {

struct Surface;

enum class PerfCorner { TopLeft, TopRight, BottomLeft, BottomRight };

struct PerfOverlayConfig {
    bool       enabled    = false;
    PerfCorner corner     = PerfCorner::TopLeft;   // Steam default: top-left
    int        detail     = 1;                     // 1=FPS, 2=+ms, 3=+graph
    bool       background = true;                  // dark high-contrast box behind it
};

class PerfOverlay {
public:
    void Configure(const PerfOverlayConfig& c) { cfg_ = c; }
    const PerfOverlayConfig& Config() const { return cfg_; }
    void SetEnabled(bool e) { cfg_.enabled = e; }
    // Off -> level 1 -> 2 -> 3 -> Off (a hotkey can drive this, like toggling Steam's).
    void CycleDetail();

    // Record one presented frame at monotonic `nowMs`. Maintains a smoothed FPS +
    // frame time and a short history for the graph. The displayed number refreshes a
    // few times per second (so it is readable, not jittering every frame).
    void Frame(std::uint32_t nowMs);

    // Composite the overlay onto `fb` (no-op when disabled). 16/32 bpp surfaces.
    void Draw(Surface* fb);
    // Convenience for loops that composite into a raw 32 bpp XRGB buffer (W*H, tightly
    // packed) instead of a render::Surface — wraps it and calls Draw(Surface*).
    void Draw(std::uint32_t* pixels, int width, int height);

    float Fps() const { return fps_; }          // smoothed instantaneous FPS
    float FrameMs() const { return frameMs_; }   // smoothed instantaneous frame time
    int   Samples() const { return histN_; }

private:
    PerfOverlayConfig cfg_{};
    std::uint32_t lastMs_ = 0;
    bool          have_   = false;
    static constexpr int kHist = 120;            // ring of recent frame intervals (ms)
    float hist_[kHist] = {0};
    int   histN_ = 0, histHead_ = 0;
    float fps_ = 0.0f, frameMs_ = 0.0f;
    std::uint32_t lastRefreshMs_ = 0;            // throttle the on-screen number
    float dispFps_ = 0.0f, dispMs_ = 0.0f;
    bool  dispValid_ = false;
};

// Process-global overlay, lazily configured from the environment on first use:
//   GUILD_PERF_OVERLAY      = 0|1|2|3   (0/unset = off; 1..3 = detail level)
//   GUILD_PERF_OVERLAY_POS  = tl|tr|bl|br   (corner; default tl)
// A frame loop can also flip it on/cycle it directly via the returned reference.
PerfOverlay& GlobalPerfOverlay();

} // namespace guild::render
