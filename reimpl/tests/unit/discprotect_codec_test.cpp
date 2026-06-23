// =============================================================================
// Golden-vector unit tests for the DiscProtect XOR/LFSR codecs.
//   src/drm/discprotect_codec.{h,cpp}  (gilde.exe DiscProtect cluster)
//
// Suite prefix: DiscProtectCodec. Headless, no main() (shared test_main.cpp).
//
// Coverage:
//   * LFSR keystream equals the byte-exact table baked at gilde.exe 0x145AD40.
//   * Each rolling-XOR codec round-trips with its inverse (and against an
//     independent reference implementation of the exact loop).
//   * CompareBytes match counting incl. edge cases.
//   * EvaluateSignatureScore exact f32-narrowed values at the 27/72 boundaries.
// =============================================================================
#include "tests/framework/test.h"
#include "drm/discprotect_codec.h"

#include <cstring>
#include <cmath>

using namespace guild;
using namespace guild::drm;

// ---------------------------------------------------------------------------
// LFSR table
// ---------------------------------------------------------------------------

// Byte-exact keystream as it appears statically at gilde.exe 0x145AD40.
static const u8 kGolden[32] = {
    0x28, 0x62, 0x9e, 0xa9, 0xa8, 0x7e, 0xfe, 0xa0,
    0x40, 0x78, 0x30, 0x22, 0x94, 0x19, 0xaf, 0x4a,
    0xfc, 0x37, 0x01, 0xd6, 0x80, 0x5e, 0xe0, 0x38,
    0x48, 0x12, 0xb6, 0x8d, 0xb6, 0xe5, 0xb6, 0xcb,
};

TEST(DiscProtectCodec, LfsrTableMatchesGoldenImage) {
    u8 out[32];
    std::memset(out, 0xAA, sizeof(out));
    InitLfsrTable(out);
    for (int i = 0; i < 32; ++i)
        CHECK_EQ(out[i], kGolden[i]);
}

TEST(DiscProtectCodec, LfsrExportedConstantMatchesGenerator) {
    u8 out[32];
    InitLfsrTable(out);
    for (int i = 0; i < 32; ++i) {
        CHECK_EQ(kLfsrTableSeed1[i], kGolden[i]);
        CHECK_EQ(kLfsrTableSeed1[i], out[i]);
    }
}

// Independent re-derivation of the 15-bit LFSR (taps 0,1; feedback 0x4000;
// seed 1; 72-byte warm-up, 32-byte emit) — confirms the production code's
// untangled feedback predicate matches a from-scratch model.
TEST(DiscProtectCodec, LfsrIndependentModel) {
    u16 v = 1;
    u8 ref[32];
    const int WARM = 0x48, OUTER = WARM + 0x20;
    for (int i = 0; i < OUTER; ++i) {
        if (i >= WARM && i < OUTER) ref[i - WARM] = static_cast<u8>(v & 0xFF);
        for (int j = 0; j < 8; ++j) {
            int b0 = v & 1;
            int b1 = (v >> 1) & 1;
            if (b0 ^ b1) v = static_cast<u16>((v >> 1) | 0x4000);
            else         v = static_cast<u16>(v >> 1);
        }
    }
    u8 out[32];
    InitLfsrTable(out);
    for (int i = 0; i < 32; ++i)
        CHECK_EQ(out[i], ref[i]);
}

// ---------------------------------------------------------------------------
// Rolling-XOR codecs: forward/reverse round-trips
// ---------------------------------------------------------------------------

// Reference: exact forward loop buf[i]^=buf[i+1] for i in [0,len-1).
static void RefForward(u8* b, int len) {
    if (len > 0)
        for (int i = 0; i < len - 1; ++i) b[i] ^= b[i + 1];
}

TEST(DiscProtectCodec, XorForwardReverseRoundTrip) {
    for (int len : {1, 2, 3, 7, 16, 22, 28, 37, 64, 123, 128}) {
        std::vector<u8> orig(len), buf(len);
        for (int i = 0; i < len; ++i) orig[i] = static_cast<u8>((i * 37 + 11) & 0xFF);
        buf = orig;

        XorEncodeForward(buf.data(), len);
        // XorEncodeReverse is the exact inverse of XorEncodeForward.
        XorEncodeReverse(buf.data(), len);
        for (int i = 0; i < len; ++i) CHECK_EQ(buf[i], orig[i]);
    }
}

TEST(DiscProtectCodec, XorForwardMatchesReference) {
    const int len = 28;
    std::vector<u8> buf(len), ref(len);
    for (int i = 0; i < len; ++i) buf[i] = ref[i] = static_cast<u8>((i * 53 + 7) & 0xFF);
    XorEncodeForward(buf.data(), len);
    RefForward(ref.data(), len);
    for (int i = 0; i < len; ++i) CHECK_EQ(buf[i], ref[i]);
}

TEST(DiscProtectCodec, XorForwardLenGuards) {
    // len<=0: no-op (and must not touch memory). len==1: no-op.
    u8 b0[1] = {0x99};
    XorEncodeForward(b0, 0);
    CHECK_EQ(b0[0], static_cast<u8>(0x99));
    XorEncodeForward(b0, 1);
    CHECK_EQ(b0[0], static_cast<u8>(0x99));
    // Reverse guards on len<=1.
    XorEncodeReverse(b0, 1);
    CHECK_EQ(b0[0], static_cast<u8>(0x99));
}

// The fixed-size 123/127 variants are the same rolling XOR over fixed spans.
// XorDecode123 == reverse pass over a 123-byte block; its forward inverse is
// XorEncodeForward(buf,123). XorEncode127/XorDecode127 are exact inverses.

TEST(DiscProtectCodec, XorDecode123IsForward123Inverse) {
    // XorDecode123 runs the reverse pass i in [122..0] over a 123-byte block
    // (reading buf[123]). Its exact inverse is the forward pass i in [0..122]
    // over the same 124-byte window.
    const int N = 123;
    std::vector<u8> orig(N + 1), buf(N + 1);
    for (int i = 0; i < N + 1; ++i) orig[i] = static_cast<u8>((i * 91 + 3) & 0xFF);
    buf = orig;
    for (int i = 0; i < 123; ++i) buf[i] ^= buf[i + 1];  // forward i in [0,122]
    XorDecode123(buf.data());                            // reverse i in [122,0]
    for (int i = 0; i < N + 1; ++i) CHECK_EQ(buf[i], orig[i]);
}

TEST(DiscProtectCodec, XorEncode127Decode127RoundTrip) {
    const int N = 128;  // touches [0..127]
    std::vector<u8> orig(N), buf(N);
    for (int i = 0; i < N; ++i) orig[i] = static_cast<u8>((i * 17 + 200) & 0xFF);
    buf = orig;
    XorEncode127(buf.data());   // forward i in [0,126]
    XorDecode127(buf.data());   // reverse i in [126,0]
    for (int i = 0; i < N; ++i) CHECK_EQ(buf[i], orig[i]);
}

TEST(DiscProtectCodec, XorEncode127MatchesReverseOf123Family) {
    // XorEncode127 (forward, 127 steps) inverts via reverse over same span.
    // Cross-check: applying XorEncodeReverse(buf,128) then XorEncodeForward
    // round-trips the same 128-byte block (general-length forms agree with the
    // fixed-127 forms on their overlapping span).
    const int N = 128;
    std::vector<u8> a(N), b(N), orig(N);
    for (int i = 0; i < N; ++i) orig[i] = static_cast<u8>((i * 7 + 5) & 0xFF);
    a = b = orig;
    XorEncode127(a.data());                  // forward i in [0,126]
    XorEncodeForward(b.data(), 128);         // forward i in [0,126] (len-1=127 -> i<127)
    for (int i = 0; i < N; ++i) CHECK_EQ(a[i], b[i]);
}

// ---------------------------------------------------------------------------
// CompareBytes
// ---------------------------------------------------------------------------

TEST(DiscProtectCodec, CompareBytesCounts) {
    const u8 a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const u8 b[8] = {1, 0, 3, 0, 5, 0, 7, 0};
    CHECK_EQ(CompareBytes(a, b, 8), 4);
    CHECK_EQ(CompareBytes(a, a, 8), 8);
    CHECK_EQ(CompareBytes(a, b, 0), 0);   // empty -> 0
    CHECK_EQ(CompareBytes(a, b, 1), 1);   // only first compared
    CHECK_EQ(CompareBytes(a, b, 2), 1);
}

// ---------------------------------------------------------------------------
// EvaluateSignatureScore — exact f32-narrowed values (threshold 27, window 72)
// ---------------------------------------------------------------------------

static bool feq(double a, double b) { return std::fabs(a - b) <= 1e-6; }

TEST(DiscProtectCodec, EvaluateSignatureScoreBoundaries) {
    // At the threshold the score is exactly 0; at the full window, exactly 1.
    CHECK(feq(EvaluateSignatureScore(27), 0.0));
    CHECK(feq(EvaluateSignatureScore(72), 1.0));
    // Zero matches -> -1.0 (cnt-T)/T = -27/27.
    CHECK(feq(EvaluateSignatureScore(0), -1.0));
}

TEST(DiscProtectCodec, EvaluateSignatureScoreInterior) {
    // cnt>=T branch: (cnt-27)/(72-27) = (cnt-27)/45.
    CHECK(feq(EvaluateSignatureScore(28), 1.0 / 45.0));
    CHECK(feq(EvaluateSignatureScore(40), 13.0 / 45.0));
    // cnt<T branch: (cnt-27)/27.
    CHECK(feq(EvaluateSignatureScore(26), -1.0 / 27.0));
    CHECK(feq(EvaluateSignatureScore(13), -14.0 / 27.0));
}

TEST(DiscProtectCodec, EvaluateSignatureScoreFromCompareBytes) {
    // End-to-end shape: build two 72-byte buffers with exactly 40 matches,
    // run CompareBytes, then score.
    u8 sig[72], dec[72];
    for (int i = 0; i < 72; ++i) { sig[i] = static_cast<u8>(i); dec[i] = static_cast<u8>(i); }
    for (int i = 40; i < 72; ++i) dec[i] = 0xFF;  // 40 matches remain
    int cnt = CompareBytes(sig, dec, 72);
    CHECK_EQ(cnt, 40);
    CHECK(feq(EvaluateSignatureScore(cnt), 13.0 / 45.0));
}
