#pragma once
// Common fixed-width typedefs and packing helpers used across the reconstruction.
// The original is 32-bit x86 little-endian, MSVC. Pointers in the live process are
// 32-bit; in this reconstruction we use native pointers and reconstruct records by
// value, so do NOT assume sizeof(pointer)==4 anywhere a record is serialized — those
// paths store explicit 32-bit ids, not pointers (see PLAN §5).
#include <cstdint>

namespace guild {

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;
using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

} // namespace guild

// Apply to structs whose byte layout must match the original record format
// (serialized to savegames / network packets). Verify sizeof() against the
// stride documented in PLAN §4.
#if defined(_MSC_VER)
#  define GUILD_PACKED_BEGIN __pragma(pack(push, 1))
#  define GUILD_PACKED_END   __pragma(pack(pop))
#  define GUILD_PACKED
#else
#  define GUILD_PACKED_BEGIN
#  define GUILD_PACKED_END
#  define GUILD_PACKED __attribute__((packed))
#endif
