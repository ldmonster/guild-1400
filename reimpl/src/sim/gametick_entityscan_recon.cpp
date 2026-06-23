#include "sim/gametick_entityscan_recon.h"

#include <cstring>

namespace guild::sim {

namespace {

// Read a 32-bit int at byte offset `off` from record base `rec`. The original
// freely reads at unaligned offsets (e.g. +14, +18, +26, +30); we replicate the
// exact little-endian load + signed semantics.
inline i32 rd32(const u8* rec, std::size_t off) {
    i32 v;
    std::memcpy(&v, rec + off, sizeof(v));
    return v;
}
inline u8 rd8(const u8* rec, std::size_t off) {
    return rec[off];
}
inline u16 rd16(const u8* rec, std::size_t off) {
    u16 v;
    std::memcpy(&v, rec + off, sizeof(v));
    return v;
}
// Read a native sub-node pointer stored at byte offset `off`. In the 32-bit
// original this is a 4-byte pointer; we store/read a native pointer by value.
inline const u8* rdptr(const u8* rec, std::size_t off) {
    const u8* p;
    std::memcpy(&p, rec + off, sizeof(p));
    return p;
}

// The slot index walk: the original starts at byte offset 2044 and decrements
// by 4 until < 0, i.e. dword slots 511 down to 0. We iterate slot indices the
// same way but bound by the provided count (a null/out-of-range slot is treated
// as the original's null pointer => skipped).
inline const u8* slotAt(const GameTickScanState& st, int slotIndex) {
    if (slotIndex < 0 || static_cast<std::size_t>(slotIndex) >= st.entityTableCount)
        return nullptr;
    return st.entityTable ? st.entityTable[slotIndex] : nullptr;
}

// *(_BYTE *)(740 * typeIndex + dword_69FFB4 + 24) == 64
inline bool objectTypeByte24Is64(const GameTickScanState& st, i32 typeIndex) {
    if (!st.objectTypeRecords)
        return false;
    return st.objectTypeRecords[740 * static_cast<std::size_t>(typeIndex) + 24] == 64;
}

} // namespace

// gilde.exe 0x4146d8 — VIBE_GameTick_InitEntityTracking.
i32 GameTickInitEntityTracking(GameTickScanState& st, i32 px, i32 py) {
    // dword_62D290 = -1;
    st.hoverChildId = -1;

    // The original loops `v2` over byte offsets 2044,2040,... => slots 511..0.
    for (int slot = 511; slot >= 0; --slot) {
        const u8* v3 = slotAt(st, slot);  // *(table + off)
        i32 v4 = 0;
        if (v3) {
            // v3[15] (offset +60) is a real pointer to a child node; if present,
            // v4 = *(child + 408) is the child's busy flag. We read it as a
            // native pointer and dereference faithfully.
            const u8* v5 = rdptr(v3, 60);          // v3[15]
            if (v5)
                v4 = rd32(v5, 408);                 // *(v5 + 408)
            i32 v6 = rd32(v3, 14) >> 16;           // box top
            if (px >= v6 && px <= (rd32(v3, 18) >> 16) + v6) {
                i32 v7 = rd32(v3, 16) >> 16;       // box left
                i32 v8 = py;                        // v11 >> 16
                if (py >= v7
                    && v8 <= (rd32(v3, 20) >> 16) + v7
                    && !v4
                    && v8 >= (rd32(v3, 30) >> 16)
                    && v8 <= (rd32(v3, 32) >> 16)
                    && !rd32(v3, 56)                // v3[14]
                    && !rd32(v3, 52)                // v3[13]
                    && objectTypeByte24Is64(st, rd32(v3, 0))) {
                    // break;  dword_62D290 = v3[29];
                    st.hoverChildId = rd32(v3, 116);  // v3[29]
                    return st.hoverChildId;
                }
            }
        }
        // v2 -= 4; if (v2 < 0) return dword_62D290;  (handled by loop bound)
    }
    return st.hoverChildId;  // -1
}

// gilde.exe 0x414a38 — VIBE_GameTick_MainLoop.
i32 GameTickMainLoop(GameTickScanState& st, i32 px, i32 py,
                     i32 (*selectEntityFallback)(i32, i32)) {
    // VIBE_GameTick_InitEntityTracking(a1, a2) is called for its side effects
    // (it sets dword_62D290), then MainLoop runs its OWN first scan that writes
    // dword_62D294 with an extra group-gate condition.
    GameTickInitEntityTracking(st, px, py);

    // dword_62D294 = -1; dword_62D240 = -1;
    st.mainGroupId = -1;
    st.secondaryId = -1;

    // ---- First scan: like InitEntityTracking but also gated by groupGateTable
    // and writing dword_62D294 = v4[29]. ----
    for (int slot = 511; slot >= 0; --slot) {
        const u8* v4 = slotAt(st, slot);
        i32 v5 = 0;
        if (v4) {
            const u8* v6 = rdptr(v4, 60);          // v4[15]
            if (v6)
                v5 = rd32(v6, 408);                 // *(v6 + 408)
            i32 v7 = rd32(v4, 14) >> 16;
            if (px >= v7 && px <= (rd32(v4, 18) >> 16) + v7) {
                i32 v8 = rd32(v4, 16) >> 16;
                i32 v9 = py;
                bool gate = false;
                if (st.groupGateTable) {
                    // dword_67EDE4[238 * v4[29]] != 0
                    i32 g = rd32(v4, 116);
                    gate = st.groupGateTable[238 * static_cast<std::size_t>(g)] != 0;
                }
                if (py >= v8
                    && v9 <= (rd32(v4, 20) >> 16) + v8
                    && !v5
                    && v9 >= (rd32(v4, 30) >> 16)
                    && v9 <= (rd32(v4, 32) >> 16)
                    && !rd32(v4, 56)
                    && !rd32(v4, 52)
                    && objectTypeByte24Is64(st, rd32(v4, 0))
                    && gate) {
                    st.mainGroupId = rd32(v4, 116);  // dword_62D294 = v4[29]
                    break;
                }
            }
        }
    }

    // ---- Second scan: resolves the picked entity. Uses byte-offset field
    // reads against the (here byte-addressed) record `v11`. ----
    for (int slot = 511; slot >= 0; --slot) {
        const u8* v11 = slotAt(st, slot);
        bool matched = false;
        do {
            if (!v11) break;                         // goto LABEL_45
            const u8* v13 = rdptr(v11, 60);          // *(v11 + 60) — child ptr
            i32 v12 = 0;
            if (v13)
                v12 = rd32(v13, 408);                // *(v13 + 408)
            i32 v14 = rd32(v11, 14) >> 16;
            if (px < v14) break;
            if (px > (rd32(v11, 18) >> 16) + v14) break;
            i32 v15 = rd32(v11, 16) >> 16;
            if ((py < v15) || (py > (rd32(v11, 20) >> 16) + v15) || v12) break;

            i32 v29;       // top edge (x-range upper for final test)
            i32 b0;        // *(_DWORD*)v30 == right/bottom bound
            i32 v26;       // py upper bound
            i32 v25;       // py lower bound
            // *(v11 + 44) is a real sub-node POINTER in the original. We store it
            // as a native pointer (rdptr) so it can be faithfully dereferenced;
            // the object-type record for the sub-node's type is indexed through
            // dword_69FFB4 (== st.objectTypeRecords) exactly as the original.
            const u8* v16 = rdptr(v11, 44);          // *(v11 + 44)
            if (v16) {
                const u8* v17 = v16;                  // alias used by the reads
                // v18 = dword_69FFB4 + 740 * *(_DWORD*)(v16 + 620)
                const u8* v18 = st.objectTypeRecords
                    ? st.objectTypeRecords + 740 * static_cast<std::size_t>(rd32(v16, 620))
                    : nullptr;
                // v19 = (*(v17+2)>>16 <= *(v18+26)>>16) ? *(_WORD*)(v18+28)
                //                                       : *(_WORD*)(v17+4)
                i32 v18_26 = v18 ? (rd32(v18, 26) >> 16) : 0;
                i32 v18_28 = v18 ? (rd32(v18, 28) >> 16) : 0;
                i32 v18_28w = v18 ? static_cast<i16>(rd16(v18, 28)) : 0;
                i32 v18_30 = v18 ? (rd32(v18, 30) >> 16) : 0;
                i32 v18_32 = v18 ? (rd32(v18, 32) >> 16) : 0;
                i32 v18_32w = v18 ? static_cast<i16>(rd16(v18, 32)) : 0;

                v29 = (rd32(v17, 2) >> 16 <= v18_26) ? v18_28w
                                                     : static_cast<i16>(rd16(v17, 4));
                // v23 = *(v18+28)>>16 ; v24 = (*(v17+2)>>16) + (*(v17+6)>>16);
                // b0 = min(v24, v23)
                i32 v21 = rd32(v17, 6);
                i32 v22 = rd32(v17, 2) >> 16;
                i32 v23 = v18_28;
                i32 v24 = v22 + (v21 >> 16);
                b0 = (v24 < v23) ? v24 : v23;
                // v25 = (*(v17+4)>>16 <= *(v18+30)>>16) ? *(_WORD*)(v18+32)
                //                                       : *(_WORD*)(v17+6)
                v25 = (rd32(v17, 4) >> 16 <= v18_30) ? v18_32w
                                                     : static_cast<i16>(rd16(v17, 6));
                // v26 = *(v18+32)>>16 ; clamp to (*(v17+4)>>16)+(*(v17+8)>>16)
                v26 = v18_32;
                i32 alt = (rd32(v17, 4) >> 16) + (rd32(v17, 8) >> 16);
                if (alt < v26)
                    v26 = alt;
            } else {
                // else branch (LABEL near 0x414cf4):
                v29 = rd32(v11, 26) >> 16;           // *(v11 + 26) >> 16
                b0  = rd32(v11, 28) >> 16;           // *(v11 + 28) >> 16
                v26 = rd32(v11, 32) >> 16;           // *(v11 + 32) >> 16
                v25 = rd32(v11, 30) >> 16;           // *(v11 + 30) >> 16
            }

            // if (py < v25 || py > v26 || px < v29 || px > b0
            //     || *(v11+52) || (!*(v11+72) && !*(v11+68)
            //                      && *(v11+24) != 65 && !*(v11+80)))
            //     goto LABEL_45;
            if (py < v25 || py > v26 || px < v29 || px > b0
                || rd32(v11, 52)
                || (!rd32(v11, 72) && !rd32(v11, 68)
                    && rd8(v11, 24) != 65 && !rd32(v11, 80))) {
                break;
            }

            if (!rd32(v11, 56)) {  // if (!*(v11+56)) break out of while(1) -> pick
                matched = true;
                break;
            }
            // else: record a tentative secondary pick and continue scanning
            if (st.secondaryId == -1)               // dword_62D240 == -1
                st.secondaryId = rd32(v11, 0);       // = *(_DWORD*)v11
            st.selectionId = -1;                     // dword_62D22C = -1
        } while (false);

        if (matched) {
            // Picked: resolve the return value.
            const u8* v28 = rdptr(v11, 44);          // v28 = *(int**)(v11 + 44)
            st.selectionId = rd32(v11, 0);           // dword_62D22C = *(_DWORD*)v11
            st.secondaryId = st.selectionId;         // dword_62D240 = dword_62D22C
            if (v28)
                st.hoverChildId = rd32(v28, 0);      // dword_62D290 = *v28
            if (rd8(v11, 24) != 9)                   // *(v11+24) != 9
                return rd32(v11, 8);                 // return *(v11 + 8)
            if (rd8(v11, 444) & 0x10)                // *(v11+444) & 0x10
                return 1155;
            return 1210;
        }
        // LABEL_45: v10 -= 4; if (v10 < 0) -> fallback
    }

    // result = VIBE_SelectEntity_ComputeResult(px, py);
    i32 result = selectEntityFallback ? selectEntityFallback(px, py) : -1;
    if (result == -1)
        st.selectionId = -1;                         // dword_62D22C = -1
    return result;
}

} // namespace guild::sim
