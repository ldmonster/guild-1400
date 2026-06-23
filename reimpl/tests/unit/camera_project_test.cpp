// camera_project_test.cpp — W11-ANIM hardening for VIBE_Coord_ProjectPoint
// (render/camera, 0x407428) + ProjectFramePoint (0x407488).
//
// ProjectPoint perspective-divides a world point by camera[4] (the view depth) and
// truncates toward zero. These tests pin the valid path and exercise the projection
// at extreme but finite coordinates so the float->int truncation stays well-defined.
//
// DEGENERATE NOTE (envelope, NOT exercised here): camera[4] == 0 makes inv = 1.0/0
// == +inf, exactly as the original's x87 `fld1; fdiv camera[4]` does on a zero view
// depth. The original then FISTPs the infinite product, yielding the x87 integer-
// indefinite 0x80000000 — a well-defined result of that faulting instruction, the
// engine's own envelope. The C++ `(i32)` cast of a non-finite double is UB, so we do
// NOT drive that case under UBSAN. Confirming the exact 0x80000000 result is a 1:1
// question for MCP (the binary never feeds a zero view depth on a valid camera).
//
// Headless: no third-party deps, no main() (test_main.cpp supplies it).
#include "render/camera.h"
#include "test.h"

#include <cstdint>

using namespace guild;
using namespace guild::render;

// Identity-ish camera: origin (0,0,0), view depth 1.0 -> screen == world (x,z) + 0.5.
TEST(CameraProject, UnitDepthMapsWorldXZ) {
    float camera[5] = {0, 0, 0, 0, 1.0f};   // camera[0..2] = pos, camera[4] = depth
    float point[3] = {10.0f, 99.0f, 20.0f}; // Y (99) is dropped
    i32 out[2] = {0, 0};
    ProjectPoint(camera, point, out);
    CHECK_EQ(out[0], 10);                    // trunc(10*1 + 0.5)
    CHECK_EQ(out[1], 20);                    // trunc(0.5 + 1*20)
}

// Non-unit view depth scales the perspective divide.
TEST(CameraProject, ScaledDepth) {
    float camera[5] = {5.0f, 0, 7.0f, 0, 2.0f};
    float point[3] = {25.0f, 0, 27.0f};
    i32 out[2] = {0, 0};
    ProjectPoint(camera, point, out);
    // (25-5)/2 = 10.0 -> trunc(10.5) = 10 ; (27-7)/2 = 10.0 -> trunc(10.5) = 10.
    CHECK_EQ(out[0], 10);
    CHECK_EQ(out[1], 10);
}

// Negative world coordinates: truncation is toward zero (not floor), matching the
// original's x87 round-to-zero, and stays well within int range.
TEST(CameraProject, NegativeCoordTruncTowardZero) {
    float camera[5] = {0, 0, 0, 0, 1.0f};
    float point[3] = {-3.9f, 0, -3.1f};
    i32 out[2] = {0, 0};
    ProjectPoint(camera, point, out);
    // -3.9 + 0.5 = -3.4 -> trunc -> -3 ; -3.1 + 0.5 = -2.6 -> trunc -> -2.
    CHECK_EQ(out[0], -3);
    CHECK_EQ(out[1], -2);
}

// Large but finite coordinates with a large depth: the divide keeps the truncation in
// int range (no overflow / UB on the cast).
TEST(CameraProject, LargeFiniteInRange) {
    float camera[5] = {0, 0, 0, 0, 1000.0f};
    float point[3] = {2000000.0f, 0, -2000000.0f};
    i32 out[2] = {0, 0};
    ProjectPoint(camera, point, out);
    CHECK_EQ(out[0], 2000);     // 2e6/1000 = 2000 (+0.5 trunc)
    CHECK_EQ(out[1], -1999);    // -2e6/1000 = -2000; -2000+0.5 = -1999.5 -> -1999
}

// ProjectFramePoint returns 0 (and never projects) when the tile->world conversion
// reports off-map — proving the early-out before any divide.
TEST(CameraProject, FramePointOffMapEarlyOut) {
    float camera[5] = {0, 0, 0, 0, 1.0f};
    i32 out[2] = {123, 456};
    auto offMap = [](int, int, float*, int) -> bool { return false; };
    int r = ProjectFramePoint(camera, 4, 4, out, offMap, 0);
    CHECK_EQ(r, 0);
    CHECK_EQ(out[0], 123);      // out untouched
    CHECK_EQ(out[1], 456);
}

// On-map: the supplied world point is projected through the same divide.
TEST(CameraProject, FramePointProjectsOnMap) {
    float camera[5] = {0, 0, 0, 0, 1.0f};
    i32 out[2] = {0, 0};
    auto onMap = [](int, int, float* w, int) -> bool {
        w[0] = 8.0f; w[1] = 0.0f; w[2] = 12.0f; w[3] = 0.0f; w[4] = 0.0f; return true;
    };
    int r = ProjectFramePoint(camera, 1, 1, out, onMap, 0);
    CHECK_EQ(r, 1);
    CHECK_EQ(out[0], 8);
    CHECK_EQ(out[1], 12);
}
