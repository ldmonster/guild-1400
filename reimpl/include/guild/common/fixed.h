#pragma once
// 16.16 signed fixed-point, as used pervasively by the engine's coordinate and
// rasterizer code (e.g. window geometry, span interpolation). Stored as a 32-bit
// integer where the low 16 bits are the fraction.
#include "guild/common/types.h"

namespace guild {

struct Fixed16_16 {
    i32 raw = 0;

    static constexpr Fixed16_16 fromRaw(i32 r) { Fixed16_16 f; f.raw = r; return f; }
    static constexpr Fixed16_16 fromInt(i32 v) { return fromRaw(v << 16); }

    // Truncating conversion to integer (drops fraction toward zero for the
    // typical positive coordinates; matches the original >>16 on the raw word).
    constexpr i32 toInt() const { return raw >> 16; }
    constexpr float toFloat() const { return static_cast<float>(raw) / 65536.0f; }
};

constexpr Fixed16_16 operator+(Fixed16_16 a, Fixed16_16 b) { return Fixed16_16::fromRaw(a.raw + b.raw); }
constexpr Fixed16_16 operator-(Fixed16_16 a, Fixed16_16 b) { return Fixed16_16::fromRaw(a.raw - b.raw); }
constexpr bool operator==(Fixed16_16 a, Fixed16_16 b) { return a.raw == b.raw; }

} // namespace guild
