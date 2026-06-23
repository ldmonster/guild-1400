// camera_update_recon_test.cpp — golden-vector unit tests for the per-frame
// camera dispatcher VIBE_Camera_Update @0x4b4c68 (camera_update_recon) and its
// cursor-write leaf VIBE_Coord_ConvertY @0x40da48. Vectors derived from the
// Hex-Rays decompile / disasm (control flow, gate order, packed-word splits).
#include "tests/framework/test.h"

#include "render/camera_update_recon.h"

#include <cmath>
#include <cstring>

using namespace guild::render;
using guild::i32;
using guild::u8;

namespace {

struct World {
    CameraObject      obj;
    CameraState       cs;
    Camera2State      st2;
    Camera2Input      in2;
    CameraInput       in1;
    CameraUpdateState us;
    CameraUpdateInput uin;
    CameraHooks       h1;
    Camera2Hooks      h2;
    CameraUpdateHooks uh;

    World() {
        h1 = Camera_DefaultHooks();
        h2 = Camera2_DefaultHooks();
        uh = CameraUpdate_DefaultHooks();
        obj.present = true;
        in2.g_13FCD1C_present = 1;
        obj.posX = 11.0f; obj.posY = 22.0f; obj.posZ = 33.0f;
        obj.worldX = 0.5f; obj.worldY = 0.25f; obj.worldZ = 0.125f;
        std::memset(obj.matrix, 0, sizeof(obj.matrix));
        obj.matrix[0] = obj.matrix[4] = obj.matrix[8] = obj.matrix[10] = 1.0f;
        // gate state mirroring the live-loop defaults: boxFlag is left 0 so
        // the early path is observable; the main path tests set it as needed.
    }

    i32 run(i32 thisX = 0) {
        return Camera_Update(obj, cs, st2, in2, in1, us, uin, h1, h2, uh, thisX);
    }
};

i32 bitsOf(float f) { i32 b; std::memcpy(&b, &f, sizeof(b)); return b; }

// counting updatePan hook
int g_panCalls = 0;
i32 g_panRet   = 0;
i32 countPan(void*) { ++g_panCalls; return g_panRet; }

// counting setCursorPos hook
int g_cursorCalls = 0;
i32 g_cursorX = -1, g_cursorY = -1;
void countCursor(i32 x, i32 y) { ++g_cursorCalls; g_cursorX = x; g_cursorY = y; }

// family record capture
i32  g_famRec[64];
void* famHook() { return g_famRec; }
void* nullFamHook() { return nullptr; }

} // namespace

// ---------------------------------------------------------------------------
// Early path (0x4b4c7d): no camera object -> panResult zeroed, edge box
// initialized from the packed dword_69FFBC exactly once, cursor mirrors set.
// ---------------------------------------------------------------------------
TEST(CameraUpdateRecon, EarlyPathInitializesEdgeBox) {
    World w;
    w.obj.present = false;          // dword_13FCD1C == 0
    w.uin.d69FFBC = (1024 << 16) | 768;  // HIWORD=width, LOWORD=height
    w.in2.d67220E = 333;            // mouse x feeding the ConvertY call
    w.us.panResult = 77;            // stale dword_631628
    CHECK_EQ(w.run(), 0);
    CHECK_EQ(w.us.panResult, 0);    // 0x4b4c85: dword_631628 = 0
    CHECK_EQ(w.st2.box0, 1024 - 8); // 0x4b4c9a: right = (69FFBC>>16)-8
    CHECK_EQ(w.st2.box1, 768);      // 0x4b4cac: bottom = (i16)LOWORD(69FFBC)
    CHECK_EQ(w.st2.box2, 0);        // 0x4b4cb6: left
    CHECK_EQ(w.st2.box3, 0);        // 0x4b4cc7: top
    CHECK_EQ(w.st2.boxFlag, 1);     // 0x4b4cbf: dword_62D0D4 = 1
    // 0x4b4ccd: ConvertY(mouseX, mouseX) -> cursor mirrors all = (i16)mouseX.
    CHECK_EQ(w.in2.d672174, 333);
    CHECK_EQ(w.in2.d672210, 333);
    CHECK_EQ(w.in2.d67220E, 333);
    CHECK_EQ(w.in1.d672174, 333);
    CHECK_EQ(w.in1.d672210, 333);
}

TEST(CameraUpdateRecon, EarlyPathBoxInitRunsOnlyOnce) {
    World w;
    w.obj.present = false;
    w.uin.d69FFBC = (640 << 16) | 480;
    CHECK_EQ(w.run(), 0);
    CHECK_EQ(w.st2.box0, 632);
    w.uin.d69FFBC = (800 << 16) | 600;   // resize would NOT re-init the box
    CHECK_EQ(w.run(), 0);
    CHECK_EQ(w.st2.box0, 632);           // boxFlag gate (0x4b4c8d) holds
    CHECK_EQ(w.st2.box1, 480);
}

TEST(CameraUpdateRecon, EarlyPathModalGateD62EB4C) {
    World w;                        // camera present...
    w.uin.d62EB4C = 1;              // ...but the modal gate is up
    w.st2.boxFlag = 1;              // box already initialized
    w.us.panResult = 5;
    CHECK_EQ(w.run(), 0);           // early path: returns 0
    CHECK_EQ(w.us.panResult, 0);    // and zeroes dword_631628
}

// Negative packed words: box1 is the SIGN-EXTENDED low word (sar 16 of the
// overlapping dword at 0x69FFBA).
TEST(CameraUpdateRecon, EarlyPathSignExtendsBottomEdge) {
    World w;
    w.obj.present = false;
    w.uin.d69FFBC = (100 << 16) | 0xFFFF;   // LOWORD = -1 as i16
    CHECK_EQ(w.run(), 0);
    CHECK_EQ(w.st2.box1, -1);
    CHECK_EQ(w.st2.box0, 92);
}

// ---------------------------------------------------------------------------
// Main-path gates (0x4b4cd8/0x4b4ce5): dword_11BC24C set or dword_633908 != -1
// -> return dword_631628 UNCHANGED (no zeroing, no UpdatePan/Movement calls).
// ---------------------------------------------------------------------------
TEST(CameraUpdateRecon, GateD11BC24CReturnsStalePanResult) {
    World w;
    w.st2.boxFlag = 1;
    w.uin.d11BC24C = 1;
    w.us.panResult = 7;
    g_panCalls = 0;
    w.uh.updatePan = &countPan;
    CHECK_EQ(w.run(), 7);           // dword_631628 returned unchanged
    CHECK_EQ(w.us.panResult, 7);
    CHECK_EQ(g_panCalls, 0);        // UpdatePan not reached
    CHECK_EQ(w.st2.hist_2D4, 0);    // mirrors not written
}

TEST(CameraUpdateRecon, GateD633908MustBeMinusOne) {
    World w;
    w.st2.boxFlag = 1;
    w.uin.d633908 = 3;              // != -1 -> gated
    w.us.panResult = 9;
    g_panCalls = 0;
    w.uh.updatePan = &countPan;
    CHECK_EQ(w.run(), 9);
    CHECK_EQ(g_panCalls, 0);
}

// ---------------------------------------------------------------------------
// 0x4b4cf9: UpdatePan runs only when no drag (dword_672238 == 0); its result
// becomes dword_631628 and the function's return value.
// ---------------------------------------------------------------------------
TEST(CameraUpdateRecon, UpdatePanGatedByDrag) {
    World w;
    w.st2.boxFlag = 1;
    g_panCalls = 0; g_panRet = 1;
    w.uh.updatePan = &countPan;
    w.in2.d672238 = 0;              // no drag -> pan runs
    CHECK_EQ(w.run(), 1);
    CHECK_EQ(g_panCalls, 1);
    CHECK_EQ(w.us.panResult, 1);

    w.in2.d672238 = 1;              // drag active -> pan skipped,
    g_panCalls = 0;
    CHECK_EQ(w.run(), 1);           // stale dword_631628 still returned
    CHECK_EQ(g_panCalls, 0);
}

// ---------------------------------------------------------------------------
// 0x4b4d0c: UpdateMovement is frozen by bit 0x8000 of dword_11BC2D0.
// Observable via the wheel-zoom branch (672254 != 672250 changes flt_6316DC).
// ---------------------------------------------------------------------------
TEST(CameraUpdateRecon, MovementFrozenBy11BC2D0Bit15) {
    World w;
    w.st2.boxFlag = 1;
    w.in2.d672254 = 1;              // one wheel notch pending
    w.in2.d672250 = 0;
    w.uin.d11BC2D0 = 0x8000;        // frozen
    w.run();
    CHECK(w.cs.zoomT == 0.0f);      // wheel branch did not run

    w.uin.d11BC2D0 = 0;             // unfrozen
    w.run();
    CHECK(std::fabs(w.cs.zoomT - 0.1f) < 1e-6f);  // zoomT += 1 * 0.1
    // AnchorToTerrain re-derived the eye height: 0 + 450 + (1600-450)*0.1
    CHECK(std::fabs(w.obj.posY - 565.0f) < 1e-3f);
}

// ---------------------------------------------------------------------------
// 0x4b4d23: ClampToTerrainHeight gated on byte_6316D8 && !byte_671D6F. With
// zoomT=0 and posY=0 the clamp target is baseHeight (450 headless); the step
// saturates at +10 per frame (flt_61DD88).
// ---------------------------------------------------------------------------
TEST(CameraUpdateRecon, ClampToTerrainGate) {
    World w;
    w.st2.boxFlag = 1;
    w.obj.posY = 0.0f;
    w.uin.byte6316D8 = 0;           // disabled -> no clamp
    w.run();
    CHECK(w.obj.posY == 0.0f);

    w.uin.byte6316D8 = 1;           // enabled
    w.in2.byte671D6F = 1;           // but axis-invert byte blocks it
    w.run();
    CHECK(w.obj.posY == 0.0f);

    w.in2.byte671D6F = 0;           // now it runs
    w.run();
    CHECK(std::fabs(w.obj.posY - 10.0f) < 1e-6f);   // saturated +10 step
}

// ---------------------------------------------------------------------------
// 0x4b4d31..0x4b4dd6: family-record write requires panResult != 0 AND
// 631610 == 631618 AND 631744 == 0 AND 631748 == 0 (note: stricter than the
// RotateView history gate, which tolerates 631748 != 0).
// ---------------------------------------------------------------------------
TEST(CameraUpdateRecon, FamilyRecordWriteOnPan) {
    World w;
    w.st2.boxFlag = 1;
    g_panRet = 1;
    w.uh.updatePan = &countPan;
    w.h2.personGetFamilyRecord = &famHook;
    std::memset(g_famRec, 0, sizeof(g_famRec));
    w.cs.zoomTBits = bitsOf(0.25f);     // dword_6316E0
    CHECK_EQ(w.run(), 1);
    CHECK_EQ(g_famRec[33], bitsOf(11.0f));   // pos
    CHECK_EQ(g_famRec[34], bitsOf(22.0f));
    CHECK_EQ(g_famRec[35], bitsOf(33.0f));
    CHECK_EQ(g_famRec[37], bitsOf(0.5f));    // world
    CHECK_EQ(g_famRec[38], bitsOf(0.25f));
    CHECK_EQ(g_famRec[39], bitsOf(0.125f));
    CHECK_EQ(g_famRec[32], bitsOf(0.25f));   // dword_6316E0 raw copy
}

TEST(CameraUpdateRecon, FamilyRecordGate631748Blocks) {
    World w;
    w.st2.boxFlag = 1;
    g_panRet = 1;
    w.uh.updatePan = &countPan;
    w.h2.personGetFamilyRecord = &famHook;
    std::memset(g_famRec, 0, sizeof(g_famRec));
    w.in2.g_631748 = 1;             // dispatcher gate requires it ZERO
    w.run();
    CHECK_EQ(g_famRec[33], 0);      // no write
}

TEST(CameraUpdateRecon, FamilyRecordSkippedWhenPanZero) {
    World w;
    w.st2.boxFlag = 1;
    g_panRet = 0;                   // pan reports no movement
    w.uh.updatePan = &countPan;
    w.h2.personGetFamilyRecord = &famHook;
    std::memset(g_famRec, 0, sizeof(g_famRec));
    CHECK_EQ(w.run(), 0);
    CHECK_EQ(g_famRec[33], 0);
}

TEST(CameraUpdateRecon, FamilyRecordNullRecordTolerated) {
    World w;
    w.st2.boxFlag = 1;
    g_panRet = 1;
    w.uh.updatePan = &countPan;
    w.h2.personGetFamilyRecord = &nullFamHook;   // 0x4b4d82: jz skip
    CHECK_EQ(w.run(), 1);                        // no crash, result intact
}

// ---------------------------------------------------------------------------
// 0x4b4dd7..0x4b4e15: pos/world history mirrors are written UNCONDITIONALLY
// on the (ungated) main path, even when nothing moved.
// ---------------------------------------------------------------------------
TEST(CameraUpdateRecon, HistoryMirrorsAlwaysWritten) {
    World w;
    w.st2.boxFlag = 1;
    w.run();
    CHECK_EQ(w.st2.hist_2D4, bitsOf(11.0f));     // dword_11BC2D4 = pos
    CHECK_EQ(w.st2.hist_2D8, bitsOf(22.0f));
    CHECK_EQ(w.st2.hist_2DC, bitsOf(33.0f));
    CHECK_EQ(w.st2.hist_2E4, bitsOf(0.5f));      // dword_11BC2E4 = world
    CHECK_EQ(w.st2.hist_2E8, bitsOf(0.25f));
    CHECK_EQ(w.st2.hist_2EC, bitsOf(0.125f));
}

// ---------------------------------------------------------------------------
// 0x40da48 — Camera_CursorCoordWrite: packed-word mirror semantics + the
// SetCursorPos call exactly when the edge box is NOT yet initialized.
// ---------------------------------------------------------------------------
TEST(CameraUpdateRecon, CursorCoordWriteMirrorsAndSetCursorPos) {
    Camera2State st2;
    Camera2Input in2;
    CameraInput in1;
    CameraUpdateHooks uh = CameraUpdate_DefaultHooks();
    uh.setCursorPos = &countCursor;
    g_cursorCalls = 0;

    st2.boxFlag = 0;                 // dword_62D0D4 == 0 -> SetCursorPos fires
    CHECK_EQ(Camera_CursorCoordWrite(100, 200, st2, in2, in1, uh), 100);
    CHECK_EQ(g_cursorCalls, 1);
    CHECK_EQ(g_cursorX, 100);
    CHECK_EQ(g_cursorY, 200);
    CHECK_EQ(in2.d67220E, 100);      // LOWORD lane (x)
    CHECK_EQ(in2.d672210, 200);      // HIWORD lane (y)
    CHECK_EQ(in2.d672174, 200);
    CHECK_EQ(in1.d67220E, 100);
    CHECK_EQ(in1.d672210, 200);

    st2.boxFlag = 1;                 // box initialized -> no SetCursorPos
    g_cursorCalls = 0;
    CHECK_EQ(Camera_CursorCoordWrite(7, 9, st2, in2, in1, uh), 7);
    CHECK_EQ(g_cursorCalls, 0);
    CHECK_EQ(in2.d672210, 9);
}

// Word truncation: the original stores ax/dx (low 16 bits, sign-extended on
// the >>16 read-back).
TEST(CameraUpdateRecon, CursorCoordWriteTruncatesToI16) {
    Camera2State st2; st2.boxFlag = 1;
    Camera2Input in2;
    CameraInput in1;
    CameraUpdateHooks uh = CameraUpdate_DefaultHooks();
    Camera_CursorCoordWrite(0x12345, 0x1FFFF, st2, in2, in1, uh);
    CHECK_EQ(in2.d67220E, (guild::i32)(short)0x2345);
    CHECK_EQ(in2.d672210, -1);       // (i16)0xFFFF
}
