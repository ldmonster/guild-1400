// Unit tests for guild::render emitter property setters (gilde.exe 0x43fe9c..
// 0x440590). Golden float values computed with python from the recovered scale
// constants (0.01f = 0x3c23d70a, 0.001f = 0x3a83126f).
#include "render/emitter_setup.h"
#include "test.h"
#include <cstddef>
#include <string>

using namespace guild;
using namespace guild::render;

// Verify the modelled struct's inter-field byte offsets match the binary's
// record offsets (record base = +0x40 in the original; we anchor amplitude at 0).
static_assert(offsetof(EmitterRecord, amplitude) == 0, "amplitude @ +0x40");
static_assert(offsetof(EmitterRecord, phasespeed) == 16, "phasespeed @ +0x50");
static_assert(offsetof(EmitterRecord, size) == 32, "size @ +0x60");
static_assert(offsetof(EmitterRecord, direction) == 44, "direction @ +0x6C");
static_assert(offsetof(EmitterRecord, velocity) == 56, "velocity @ +0x78");
static_assert(offsetof(EmitterRecord, rndVelocity) == 68, "rndVel @ +0x84");
static_assert(offsetof(EmitterRecord, accel) == 80, "accel @ +0x90");
static_assert(offsetof(EmitterRecord, plane) == 92, "plane @ +0x9C");
static_assert(offsetof(EmitterRecord, flagsTime) == 108, "flagsTime @ +0xAC");
static_assert(offsetof(EmitterRecord, amplitudeW) == 112, "amplitudeW @ +0xB0");
static_assert(offsetof(EmitterRecord, phasespeedW) == 116, "phasespeedW @ +0xB4");
static_assert(offsetof(EmitterRecord, triggerTime) == 120, "triggerTime @ +0xB8");
static_assert(offsetof(EmitterRecord, time) == 124, "time @ +0xBC");
static_assert(offsetof(EmitterRecord, colorB) == 136, "colorB @ +0xC8");
static_assert(offsetof(EmitterRecord, flagByte0) == 140, "flagByte0 @ +0xCC");
static_assert(offsetof(EmitterRecord, flagByte1) == 141, "flagByte1 @ +0xCD");

namespace {
EmitterRecord* MakeHandle(EmitterRecord& r, EmitterHandle& h) {
    h = &r;
    return &r;
}
}

TEST(RenderEmitterSetup, AmplitudeScaling) {
    EmitterRecord r{};
    EmitterHandle h;
    MakeHandle(r, h);
    i32 x = 100, y = 250, z = -50, w = 1000, w2 = 33;
    CHECK_EQ(SetAmplitude(&h, &x, &y, &z, &w, &w2), 0);
    CHECK_EQ(r.amplitude[0], 1.0f);
    CHECK_EQ(r.amplitude[1], 2.5f);
    CHECK_EQ(r.amplitude[2], -0.5f);
    CHECK_EQ(r.amplitude[3], 10.0f);
    CHECK_EQ(r.amplitudeW, 0.32999998331069946f);
}

TEST(RenderEmitterSetup, PhasespeedScaling) {
    EmitterRecord r{};
    EmitterHandle h = &r;
    i32 x = 100, y = 250, z = -50, w = 1000, w2 = 33;
    SetPhasespeed(&h, &x, &y, &z, &w, &w2);
    CHECK_EQ(r.phasespeed[0], 1.0f);
    CHECK_EQ(r.phasespeed[1], 2.5f);
    CHECK_EQ(r.phasespeed[2], -0.5f);
    CHECK_EQ(r.phasespeed[3], 10.0f);
    CHECK_EQ(r.phasespeedW, 0.32999998331069946f);
}

TEST(RenderEmitterSetup, DirectionVelocityRnd) {
    EmitterRecord r{};
    EmitterHandle h = &r;
    i32 x = 100, y = 250, z = -50;
    SetDirection(&h, &x, &y, &z);
    SetVelocity(&h, &x, &y, &z);
    SetRndVelocity(&h, &x, &y, &z);
    for (auto* v : {r.direction, r.velocity, r.rndVelocity}) {
        CHECK_EQ(v[0], 1.0f);
        CHECK_EQ(v[1], 2.5f);
        CHECK_EQ(v[2], -0.5f);
    }
}

TEST(RenderEmitterSetup, AccelerationMilliScale) {
    EmitterRecord r{};
    EmitterHandle h = &r;
    i32 x = 7000, y = -3000, z = 12000;
    SetAcceleration(&h, &x, &y, &z);
    CHECK_EQ(r.accel[0], 7.000000476837158f);
    CHECK_EQ(r.accel[1], -3.000000238418579f);
    CHECK_EQ(r.accel[2], 12.000000953674316f);
}

TEST(RenderEmitterSetup, SizeNoScaling) {
    EmitterRecord r{};
    EmitterHandle h = &r;
    i32 x = 7, y = -50, z = 128;
    SetSize(&h, &x, &y, &z);
    CHECK_EQ(r.size[0], 7.0f);
    CHECK_EQ(r.size[1], -50.0f);
    CHECK_EQ(r.size[2], 128.0f);
}

TEST(RenderEmitterSetup, PlaneXyzScaledWplain) {
    EmitterRecord r{};
    EmitterHandle h = &r;
    i32 x = 100, y = 250, z = -50, w = 42;
    SetPlane(&h, &x, &y, &z, &w);
    CHECK_EQ(r.plane[0], 1.0f);
    CHECK_EQ(r.plane[1], 2.5f);
    CHECK_EQ(r.plane[2], -0.5f);
    CHECK_EQ(r.plane[3], 42.0f);
}

TEST(RenderEmitterSetup, TimeAndAlpha) {
    EmitterRecord r{};
    EmitterHandle h = &r;
    u32 t0 = 0x11112222u, t1 = 0xDEADBEEFu, t2 = 7u;
    i32 alpha = 200;
    SetTimeAndAlpha(&h, &t0, &t1, &t2, &alpha);
    CHECK_EQ(r.time[0], 0x11112222u);
    CHECK_EQ(r.time[1], 0xDEADBEEFu);
    CHECK_EQ(r.time[2], 7u);
    CHECK_EQ(r.triggerTime, 200.0f);
}

TEST(RenderEmitterSetup, ColorChannelOrder) {
    EmitterRecord r{};
    EmitterHandle h = &r;
    u8 cr = 10, cg = 20, cb = 30, ca = 255;
    SetColor(&h, &cr, &cg, &cb, &ca);
    CHECK_EQ(r.colorR, 10);
    CHECK_EQ(r.colorG, 20);
    CHECK_EQ(r.colorB, 30);
    CHECK_EQ(r.colorA, 255);
}

TEST(RenderEmitterSetup, FlagsPacking) {
    EmitterRecord r{};
    r.flagByte0 = 0xFF; // start all-set to exercise the clear-then-set masks
    r.flagByte1 = 0xFF;
    EmitterHandle h = &r;
    i32 time = 500;
    u8 tex = 0x15, init = 1, rebirth = 0, isTrig = 1, once = 0;
    SetFlags(&h, &time, &tex, &init, &rebirth, &isTrig, &once);
    CHECK_EQ(r.flagsTime, 5.0f);
    // bits0-4=0x15, bit5=init(1), bit6=rebirth(0), bit7=isTrig(1)
    // => 0x15 | 0x20 | 0x00 | 0x80 = 0xB5
    CHECK_EQ(r.flagByte0, 0xB5);
    // flagByte1 bit0 cleared (once=0), other bits preserved => 0xFE
    CHECK_EQ(r.flagByte1, 0xFE);
}

TEST(RenderEmitterSetup, IndividualFlagSetters) {
    EmitterRecord r{};
    EmitterHandle h = &r;
    u8 one = 1, zero = 0, mode = 0x1F;

    SetInitFill(&h, &one);
    CHECK_EQ(r.flagByte0 & 0x20, 0x20);
    SetInitFill(&h, &zero);
    CHECK_EQ(r.flagByte0 & 0x20, 0x00);

    SetRebirthFill(&h, &one);
    CHECK_EQ(r.flagByte0 & 0x40, 0x40);

    SetIsTrigger(&h, &one);
    CHECK_EQ(r.flagByte0 & 0x80, 0x80);

    SetTextureMode(&h, &mode);
    CHECK_EQ(r.flagByte0 & 0x1F, 0x1F);
    // texture-mode write must not disturb the high bits set above.
    CHECK_EQ(r.flagByte0 & 0xE0, 0xC0);

    SetTriggerOnce(&h, &one);
    CHECK_EQ(r.flagByte1 & 1, 1);
}

TEST(RenderEmitterSetup, MaxTriggerAndTrigger) {
    EmitterRecord r{};
    EmitterHandle h = &r;
    i32 mt = 9;
    SetMaxTrigger(&h, &mt);
    CHECK_EQ(r.flagsTime, 9.0f);
    Trigger(&h);
    CHECK_EQ(r.flagByte1 & 2, 2);
    Trigger(&h); // idempotent OR
    CHECK_EQ(r.flagByte1 & 2, 2);
}

TEST(RenderEmitterSetup, InvalidHandleTakesErrorPath) {
    ClearLastEmitterError();
    EmitterRecord* nullRec = nullptr;
    EmitterHandle h = nullRec; // *handle == nullptr -> invalid
    i32 v = 1;
    CHECK_EQ(SetSize(&h, &v, &v, &v), 0);
    CHECK(LastEmitterError() != nullptr);
    // The record was never written (it's null), so no crash and error recorded.
    CHECK(std::string(LastEmitterError()).find("SetEmitterSize") == 0);
}
