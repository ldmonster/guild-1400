// bone_sample_test.cpp — W11-ANIM hardening tests for the bone keyframe samplers
// (render/bone_sample): GetBonePosition (0x5cc850) + GetBoneFramePose (0x5ccea0).
//
// These index the bone's anim header: keyframe base = *(*(bone+104)+348), 192-byte
// stride. The pointer slots (+104, +348) are 32-bit in the binary but stored as host
// pointers in the model; offset 348 is NOT 8-byte aligned, so the read must avoid a
// misaligned 8-byte load (the W11 UBSAN fix uses memcpy). These tests pin the values
// AND exercise the misaligned-slot read so ASAN/UBSAN cover it.
//
// Headless: no third-party deps, no main() (test_main.cpp supplies it).
#include "render/bone_sample.h"
#include "test.h"

#include <cstdint>
#include <cstring>
#include <vector>
#include <cmath>

using namespace guild::render;

namespace {
// Store a host pointer at byte offset `off` of a buffer (mirrors the model's putPtr).
void putPtr(std::uint8_t* base, std::size_t off, const void* p) {
    std::memcpy(base + off, &p, sizeof(p));
}
// Write a float into a keyframe at float index `idx` (byte 4*idx).
void putKfFloat(std::uint8_t* kfBase, int frame, int idx, float v) {
    std::memcpy(kfBase + 192 * frame + 4 * idx, &v, sizeof(v));
}
bool feq(float a, float b) { return std::fabs(a - b) < 1e-6f; }
} // namespace

// GetBonePosition: out = boneFrame[33..35] + keyframe[11..13]. The anim pointer is
// stored at bone+104; the keyframe base at anim+348 (a 4-byte-misaligned host-pointer
// slot). ASAN/UBSAN exercise the chained indirect reads.
TEST(BoneSample, GetBonePositionAddsFrameAndKeyframe) {
    std::vector<std::uint8_t> bone(256, 0), anim(512, 0), kf(1024, 0);
    putPtr(bone.data(), 104, anim.data());
    putPtr(anim.data(), 348, kf.data());

    // keyframe 2 translation (float idx 11..13).
    putKfFloat(kf.data(), 2, 11, 1.5f);
    putKfFloat(kf.data(), 2, 12, 2.5f);
    putKfFloat(kf.data(), 2, 13, 3.5f);

    float boneFrame[40] = {0};
    boneFrame[33] = 10.0f; boneFrame[34] = 20.0f; boneFrame[35] = 30.0f;

    float out[3] = {0,0,0};
    float* r = GetBonePosition(boneFrame, bone.data(), 2, out);
    CHECK(r == out);
    CHECK(feq(out[0], 11.5f));
    CHECK(feq(out[1], 22.5f));
    CHECK(feq(out[2], 33.5f));
}

// GetBoneFramePose: outRot = keyframe[8..10], outTrans = keyframe[11..13]; returns
// 192*frameIdx. Exercises the same misaligned-slot indirection for a different frame.
TEST(BoneSample, GetBoneFramePoseReadsRotAndTrans) {
    std::vector<std::uint8_t> bone(256, 0), anim(512, 0), kf(2048, 0);
    putPtr(bone.data(), 104, anim.data());
    putPtr(anim.data(), 348, kf.data());

    putKfFloat(kf.data(), 5, 8,  0.11f);
    putKfFloat(kf.data(), 5, 9,  0.22f);
    putKfFloat(kf.data(), 5, 10, 0.33f);
    putKfFloat(kf.data(), 5, 11, 4.0f);
    putKfFloat(kf.data(), 5, 12, 5.0f);
    putKfFloat(kf.data(), 5, 13, 6.0f);

    float rot[3] = {0,0,0}, trans[3] = {0,0,0};
    int e = GetBoneFramePose(bone.data(), rot, 5, trans);
    CHECK_EQ(e, 192 * 5);
    CHECK(feq(rot[0], 0.11f));
    CHECK(feq(rot[2], 0.33f));
    CHECK(feq(trans[0], 4.0f));
    CHECK(feq(trans[2], 6.0f));
}

// Frame 0 reads the first keyframe record (offset 0) — the lower bound.
TEST(BoneSample, FrameZeroReadsFirstRecord) {
    std::vector<std::uint8_t> bone(256, 0), anim(512, 0), kf(512, 0);
    putPtr(bone.data(), 104, anim.data());
    putPtr(anim.data(), 348, kf.data());
    putKfFloat(kf.data(), 0, 11, 7.0f);
    putKfFloat(kf.data(), 0, 12, 8.0f);
    putKfFloat(kf.data(), 0, 13, 9.0f);

    float boneFrame[40] = {0};
    float out[3] = {0,0,0};
    GetBonePosition(boneFrame, bone.data(), 0, out);
    CHECK(feq(out[0], 7.0f));
    CHECK(feq(out[2], 9.0f));
}

// A high frame index sized exactly to the keyframe buffer: the 192-byte record at the
// last slot is fully in bounds. ASAN proves the +52 (idx 13) read lands inside `kf`.
TEST(BoneSample, HighFrameIndexInBounds) {
    const int frame = 9;
    std::vector<std::uint8_t> bone(256, 0), anim(512, 0);
    std::vector<std::uint8_t> kf(192 * (frame + 1), 0);   // exactly enough records
    putPtr(bone.data(), 104, anim.data());
    putPtr(anim.data(), 348, kf.data());
    putKfFloat(kf.data(), frame, 13, 42.0f);

    float boneFrame[40] = {0};
    float out[3] = {0,0,0};
    GetBonePosition(boneFrame, bone.data(), frame, out);
    CHECK(feq(out[2], 42.0f));
}
