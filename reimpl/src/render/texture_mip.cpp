#include "render/texture_mip.h"

// =============================================================================
// guild::render texture mip / LUT helpers — implementation. See texture_mip.h.
// =============================================================================
namespace guild::render {

// ---------------------------------------------------------------------------
// gilde.exe 0x4358d8 — VIBE_Render_ComputeChannelShifts.
//   loop1 (i<32): count trailing-zero bits of each mask -> down/up base
//   loop2 (j<32): count the set bits of each (shifted) mask -> field width
//   out down = 8 - width ; out up = trailing-zero count.
// We reproduce the exact two-pass bit walk per channel.
// ---------------------------------------------------------------------------
static void ChannelShiftOne(u32 mask, u8& down, u8& up) {
    u8 trailing = 0;       // v14 / v13 / v17 (trailing-zero count)
    for (int i = 0; i < 32; ++i) {
        if ((mask & 1) == 0) {
            ++trailing;
            mask >>= 1;
        }
    }
    u8 width = 0;          // v16 / v18 / v15 (set-bit width)
    for (int j = 0; j < 32; ++j) {
        if ((mask & 1) == 1) {
            ++width;
            mask >>= 1;
        }
    }
    down = (u8)(8 - width);
    up = trailing;
}

ChannelShifts MipChannelShifts(u32 maskR, u32 maskG, u32 maskB) {
    ChannelShifts s{};
    ChannelShiftOne(maskR, s.downR, s.upR);
    ChannelShiftOne(maskG, s.downG, s.upG);
    ChannelShiftOne(maskB, s.downB, s.upB);
    return s;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x435a3c — VIBE_Render_BuildChannelLUT.
//   for i in 0..255: out[i]=(i>>downR)<<upR ; out[256+i]=(i>>downG)<<upG ;
//                    out[512+i]=(i>>downB)<<upB
// ---------------------------------------------------------------------------
void BuildChannelLUT(u32 out[768], u32 maskR, u32 maskG, u32 maskB) {
    ChannelShifts s = MipChannelShifts(maskR, maskG, maskB);
    for (u32 i = 0; i < 256; ++i) {
        out[i]       = (i >> s.downR) << s.upR;
        out[256 + i] = (i >> s.downG) << s.upG;
        out[512 + i] = (i >> s.downB) << s.upB;
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5b9230 — VIBE_TextureCache_BuildBlendLut. Faithful translation of
// the 16.16 weight recurrence. `out` receives n*n*4 bytes.
// ---------------------------------------------------------------------------
u32 BuildBlendLut(u8* out, u32 a2) {
    u32 v9 = 4 * a2;                 // per-row byte stride
    u32 v11 = 0x100 / a2;            // step = 256 / n
    u32 result = 0;
    u32 v13 = 0;                     // row byte base
    for (u32 v14 = 0; v14 < a2; ++v14) {
        int v2 = (int)((v11 * v14) << 8);   // base2 16.16  (signed arithmetic)
        int v3 = 0xFFFF - v2;
        int v4 = (int)(v11 * (u32)((0xFFFF - v2) >> 8)); // d2
        int v5 = 0;
        int v6 = 0;
        // decrement of v2 per column == ((int)((v11*v14)<<8) >> 8) * v11
        int dec = (int)((u32)(((int)((v11 * v14) << 8)) >> 8) * v11);
        u8* p = out + v13;
        for (u32 c = 0; c < a2; ++c) {
            p[0] = (u8)(((u32)v3 >> 8) & 0xFF);  // BYTE1(v3)
            p[1] = (u8)(((u32)v5 >> 8) & 0xFF);  // BYTE1(v5)
            p[2] = (u8)(((u32)v2 >> 8) & 0xFF);  // BYTE1(v2)
            p[3] = (u8)(((u32)v6 >> 8) & 0xFF);  // BYTE1(v6)
            v3 -= v4;
            v2 -= dec;
            p += 4;
            v6 += dec;
            v5 += v4;
        }
        result = v9;
        v13 += v9;
    }
    return result;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5b903c (head) — block-size pick: width/height clamped to [16,64].
//   0x5b905c xor edx,edx / 0x5b905e div ecx  => UNSIGNED division a2[3]/a2[4].
//   0x5b9065 cmp eax,10h / jnb ... cmp esi,40h => unsigned clamp to [16,64].
// ---------------------------------------------------------------------------
int MipBlockSize(int width, int height) {
    // a2[3]/a2[4] is an unconditional unsigned `div`; height==0 faults in the
    // binary. We reproduce the unsigned division (callers always pass height>0).
    u32 b = (u32)width / (u32)height; // dword_1404E6C = a2[3]/a2[4]
    if (b > 0x40)
        return 64;
    if (b >= 0x10)
        return (int)b;
    return 16;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5db234 (tail) — *(v4+116) = *(v4+120) >> byte_64A350.
//   0x5db347 mov cl, byte_64A350 / 0x5db350 shr eax, cl => UNSIGNED shift, and
//   there is NO saturation to 1 in the binary (bare shr stored to +116).
// ---------------------------------------------------------------------------
int MipWidth(int baseWidth, int shift) {
    return (int)((u32)baseWidth >> shift); // shr eax, cl ; mov [esi+74h], eax
}

int MipLevelCount(int width) {
    int levels = 0;
    for (int w = width; w >= 1; w >>= 1)
        ++levels;
    return levels; // width=256 -> 256,128,...,1 = 9 levels
}

// ---------------------------------------------------------------------------
// 2x2 box-select downsample of an 8-bit index buffer (preserves palette idx).
// dst is (srcWidth/2)^2; dst[v][u] = src[2v][2u].
// ---------------------------------------------------------------------------
void DownsampleIndex2x(const u8* src, int srcWidth, u8* dst) {
    int dw = srcWidth >> 1;
    for (int v = 0; v < dw; ++v)
        for (int u = 0; u < dw; ++u)
            dst[v * dw + u] = src[(2 * v) * srcWidth + (2 * u)];
}

std::vector<std::vector<u8>> BuildIndexMipChain(const u8* base, int width) {
    std::vector<std::vector<u8>> levels;
    levels.emplace_back(base, base + (size_t)width * width);
    int w = width;
    while (w > 1) {
        int dw = w >> 1;
        std::vector<u8> next((size_t)dw * dw);
        DownsampleIndex2x(levels.back().data(), w, next.data());
        levels.push_back(std::move(next));
        w = dw;
    }
    return levels;
}

} // namespace guild::render
