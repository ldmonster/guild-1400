#pragma once
// gilde.exe — Per-frame entity hit-test scans from the VIBE_GameTick cluster.
//
//   VIBE_GameTick_InitEntityTracking @0x4146d8
//   VIBE_GameTick_MainLoop           @0x414a38
//
// Both walk the 512-slot scene-entity pointer table (dword_62D26C, indexed from
// byte offset 2044 downward in steps of 4 -> slots 511..0) and hit-test a screen
// point (px, py) against each live entity's nested bounding boxes, writing the
// hit results into a set of shared "current selection / hover" globals. The math
// is the original's 16.16 fixed-point: many fields are read as a 32-bit int and
// arithmetic-shifted right by 16 to get the integer coordinate (note that some
// reads are at *unaligned* byte offsets, e.g. +14, +18, +26, +30 — this is
// faithfully reproduced via byte-offset reads).
//
// The scan touches three external globals besides the entity table:
//   dword_69FFB4  — base of the 740-byte "object type" records; byte +24 of the
//                   record for type index `*entity` must equal 64 for a hit.
//   dword_67EDE4  — 238-dword-stride table gating MainLoop's first scan by the
//                   entity's group id (entity[29]).
//   dword_764CE0  — (referenced by BeginRound, not here).
// These plus the entity table and the output globals are routed through
// GameTickScanState so the scan is testable with synthetic data and carries no
// platform coupling.
//
// NOTE: the entity record layout below models ONLY the fields these two scans
// read; the real scene node is larger (740-byte object-type record is separate).
#include "guild/common/types.h"
#include <cstdint>
#include <cstddef>
#include <vector>

namespace guild::sim {

using namespace guild;

// Result/selection globals updated by the scans (recovered names in comments).
struct GameTickScanState {
    // Entity table: dword_62D26C points at slot 0; the scan reads slots via the
    // byte offset (2044 down to 0, step 4) => up to 512 entries. We model it as
    // an array of raw record pointers (nullptr == empty slot, matching the
    // original's null check on *(table + off)).
    // Each record pointer addresses the entity's field block; see the field
    // accessors in the .cpp. We expose it as a vector of byte blobs by pointer.
    const u8* const* entityTable = nullptr;  // dword_62D26C  (>= 512 entries)
    std::size_t entityTableCount = 0;        // number of valid slots (<= 512)

    // dword_69FFB4: base of 740-byte object-type records. The scan reads
    // base[740*typeIndex + 24]. Null => the "==64" test fails for every entity.
    const u8* objectTypeRecords = nullptr;   // dword_69FFB4

    // dword_67EDE4: 238*4-byte stride table; MainLoop's first scan also requires
    // table[238 * entity[29]] != 0. Null => that extra gate is treated as 0
    // (no hit) in MainLoop's first scan. Indexed in dwords.
    const i32* groupGateTable = nullptr;     // dword_67EDE4 (dword stride 238)

    // dword_764CE0 (referenced by BeginRound). -1 means "no turn filter".
    i32 turnObjectFilter = -1;               // dword_764CE0

    // ---- Output globals (written by the scans) ----
    i32 hoverChildId = -1;     // dword_62D290  (InitEntityTracking + MainLoop)
    i32 mainGroupId  = -1;     // dword_62D294  (MainLoop first scan)
    i32 secondaryId  = -1;     // dword_62D240  (MainLoop second scan)
    i32 selectionId  = -1;     // dword_62D22C  (MainLoop second scan)
};

// gilde.exe 0x4146d8 — VIBE_GameTick_InitEntityTracking
//   (__usercall, ax = px high word, dx = py high word).
// The original receives px/py in the *high* 16 bits of two registers (v10/v11
// with HIWORD set), then compares (px>>16) and (py>>16) against box edges. We
// take the already-shifted integer coordinates (px, py) directly. Sets
// st.hoverChildId (dword_62D290) to the matched entity's entity[29], or leaves
// it -1, and returns it.
i32 GameTickInitEntityTracking(GameTickScanState& st, i32 px, i32 py);

// gilde.exe 0x414a38 — VIBE_GameTick_MainLoop (__usercall, ax = px, dx = py).
// First runs the InitEntityTracking-style scan (writing dword_62D294 and the
// hover child), then a second, richer scan that resolves the picked entity and
// returns one of: the entity's action code *(entity+8), 1155, 1210, or the
// VIBE_SelectEntity_ComputeResult fallback (-1 path). Updates dword_62D240 /
// dword_62D22C / dword_62D290 accordingly. The SelectEntity fallback is supplied
// by the caller as a functor so this stays self-contained.
i32 GameTickMainLoop(GameTickScanState& st, i32 px, i32 py,
                     i32 (*selectEntityFallback)(i32 px, i32 py));

} // namespace guild::sim
