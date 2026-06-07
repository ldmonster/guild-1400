// End-to-end test for the guild::render emitter setup flow (gilde.exe particle
// script-binding cluster). Drives the full SetEmitterXxx suite the way the
// script VM would when constructing a particle emitter from a *.par definition,
// then verifies the fully-assembled EmitterRecord and the error-hook path.
//
// Real-asset portion is GUARDED: if europe_guild_1400_original/Resources/
// forms.BIN is absent it skip-passes (the setters operate on in-memory records,
// so the asset is only used to confirm the harness can locate game data).
#include "render/emitter_setup.h"
#include "test.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

// A custom error hook capturing every reported diagnostic, used to prove the
// invalid-handle path is exercised exactly when expected.
std::vector<std::string> g_reports;
bool RejectAll(const void*) { return false; }
bool AcceptNonNull(const void* p) { return p != nullptr; }
void Capture(const char* m) { g_reports.emplace_back(m ? m : ""); }

bool FormsBinPresent() {
    const char* paths[] = {
        "europe_guild_1400_original/Resources/forms.BIN",
        "../europe_guild_1400_original/Resources/forms.BIN",
        "reimpl/europe_guild_1400_original/Resources/forms.BIN",
    };
    for (const char* p : paths) {
        if (std::FILE* f = std::fopen(p, "rb")) {
            std::fclose(f);
            return true;
        }
    }
    return false;
}

} // namespace

TEST(RenderEmitterSetupE2E, BuildEmitterFromScriptSuite) {
    // Install a hook that accepts the live record but rejects null handles,
    // and captures diagnostics — mirroring the IsValidPointer/ReportError pair.
    g_reports.clear();
    EmitterErrorHook hook;
    hook.isValid = AcceptNonNull;
    hook.report = Capture;
    SetEmitterErrorHook(hook);

    EmitterRecord rec{};
    EmitterHandle h = &rec;

    // --- the script issues a full emitter definition (values in 1/100 units) ---
    i32 amp[5] = {50, 75, 100, 200, 25};
    SetAmplitude(&h, &amp[0], &amp[1], &amp[2], &amp[3], &amp[4]);
    i32 phase[5] = {10, 20, 30, 40, 50};
    SetPhasespeed(&h, &phase[0], &phase[1], &phase[2], &phase[3], &phase[4]);
    i32 dir[3] = {0, 100, 0};
    SetDirection(&h, &dir[0], &dir[1], &dir[2]);
    i32 sz[3] = {4, 4, 4};
    SetSize(&h, &sz[0], &sz[1], &sz[2]);
    i32 vel[3] = {0, 300, 0};
    SetVelocity(&h, &vel[0], &vel[1], &vel[2]);
    i32 acc[3] = {0, -9810, 0}; // ~ -9.81 in 1/1000 units
    SetAcceleration(&h, &acc[0], &acc[1], &acc[2]);
    i32 plane[4] = {0, 100, 0, 1};
    SetPlane(&h, &plane[0], &plane[1], &plane[2], &plane[3]);
    u8 col[4] = {200, 180, 160, 255};
    SetColor(&h, &col[0], &col[1], &col[2], &col[3]); // r,g,b,a
    u32 tm[3] = {1000, 2000, 3000};
    i32 alpha = 64;
    SetTimeAndAlpha(&h, &tm[0], &tm[1], &tm[2], &alpha);
    i32 ft = 1500;
    u8 tex = 3, init = 1, rebirth = 1, isTrig = 0, once = 1;
    SetFlags(&h, &ft, &tex, &init, &rebirth, &isTrig, &once);
    i32 maxTrig = 8;
    SetMaxTrigger(&h, &maxTrig); // overwrites flagsTime
    Trigger(&h);

    // --- verify the assembled record ---
    CHECK_EQ(rec.amplitude[0], 0.5f);
    CHECK_EQ(rec.amplitude[1], 0.75f);
    CHECK_EQ(rec.amplitude[2], 1.0f);
    CHECK_EQ(rec.amplitude[3], 2.0f);
    CHECK_EQ(rec.amplitudeW, 0.25f);
    CHECK_EQ(rec.phasespeed[0], 0.09999999403953552f); // 10 * 0.01f (not 0.1f!)
    CHECK_EQ(rec.phasespeedW, 0.5f);
    CHECK_EQ(rec.direction[1], 1.0f);
    CHECK_EQ(rec.size[0], 4.0f);
    CHECK_EQ(rec.velocity[1], 3.0f);
    CHECK_EQ(rec.accel[1], -9.8100004196167f);
    CHECK_EQ(rec.plane[1], 1.0f);
    CHECK_EQ(rec.plane[3], 1.0f); // plain (float)int
    CHECK_EQ(rec.colorR, 200);
    CHECK_EQ(rec.colorG, 180);
    CHECK_EQ(rec.colorB, 160);
    CHECK_EQ(rec.colorA, 255);
    CHECK_EQ(rec.time[2], 3000u);
    CHECK_EQ(rec.triggerTime, 64.0f);

    // SetMaxTrigger overwrote the SetFlags-written flagsTime (both target +0xAC).
    CHECK_EQ(rec.flagsTime, 8.0f);

    // flagByte0: tex(3) | init(0x20) | rebirth(0x40) | isTrigger(0) = 0x63
    CHECK_EQ(rec.flagByte0, 0x63);
    // flagByte1: triggerOnce bit0 (1) | Trigger pending bit1 (2) = 3
    CHECK_EQ(rec.flagByte1, 3);

    // No diagnostics should have fired on the happy path.
    CHECK_EQ(g_reports.size(), static_cast<size_t>(0));

    // --- now drive the error path via a null handle ---
    EmitterRecord* nullRec = nullptr;
    EmitterHandle bad = nullRec;
    SetVelocity(&bad, &vel[0], &vel[1], &vel[2]);
    Trigger(&bad);
    CHECK_EQ(g_reports.size(), static_cast<size_t>(2));
    CHECK(g_reports[0].find("SetEmitterVelocity") == 0);
    CHECK(g_reports[1].find("TriggerEmitter") == 0);

    // --- and a reject-all hook makes even a non-null record invalid ---
    g_reports.clear();
    EmitterErrorHook reject;
    reject.isValid = RejectAll;
    reject.report = Capture;
    SetEmitterErrorHook(reject);
    EmitterRecord untouched{};
    EmitterHandle h2 = &untouched;
    SetSize(&h2, &sz[0], &sz[1], &sz[2]);
    CHECK_EQ(g_reports.size(), static_cast<size_t>(1));
    CHECK_EQ(untouched.size[0], 0.0f); // never written

    // Restore default hook so later suites are unaffected.
    EmitterErrorHook def;
    SetEmitterErrorHook(def);
}

TEST(RenderEmitterSetupE2E, RealAssetGuardedSmoke) {
    if (!FormsBinPresent()) {
        // Skip-pass: real game assets not present in this checkout.
        CHECK(true);
        return;
    }
    // Assets present: assemble a minimal emitter and confirm the suite runs end
    // to end against a record (no asset parsing needed for these pure setters).
    EmitterErrorHook def;
    SetEmitterErrorHook(def);
    EmitterRecord rec{};
    EmitterHandle h = &rec;
    i32 v = 1000;
    SetVelocity(&h, &v, &v, &v);
    CHECK_EQ(rec.velocity[0], 10.0f);
}
