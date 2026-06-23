// ===========================================================================
// ai_meister_equip.cpp — MeisterAi equipment + workstation supply sub-planners
// gilde.exe reconstruction (namespace guild::sim).
//
// Reconstructed functions (1:1 behavioral clone, rule 1/2):
//   0x45e350  VIBE_MeisterAi_EquipStaffWeapon        -> EquipStaffWeapon
//   0x45a62c  VIBE_MeisterAi_CollectStorageItems      -> MeisterCollectStorageItems
//   0x45c10c  VIBE_MeisterAi_GatherRequiredItems      -> MeisterGatherRequiredItems
//   0x45bd68  VIBE_MeisterAi_ReserveWorkstationItems  -> MeisterReserveWorkstationItems
//   0x45ba84  VIBE_MeisterAi_CheckWorkstationCapacity -> MeisterCheckWorkstationCapacity
//
// The original addresses all record arrays by raw byte offset. We reproduce that
// exactly via the rd*/wr* helpers and aimei:: column offsets from
// ai_meister_internal.h. All scratch-table columns map to g_stockTable and
// g_workOrderTable (both in ai_meister_core.cpp). The flag column word_B564BC is
// a parallel table starting 88 bytes past the WO data base; we model it in the
// module-level g_woBitsTable[] below.
//
// Engine leaves not yet in MeisterAiLeaves are surfaced as supplemental module-
// level function-pointer globals (g_meisterEquipLeaves).  They are set by the
// live bridge / tests. On null, the same fallback as a null/zero return is taken
// (documented per call site).
//
// VIBE_Crt_Sprintf_0 debug-log calls have no sim side-effect and are DROPPED.
// VIBE_Coord_ConvertX is float->int truncation toward zero = (int) cast.
// ===========================================================================
#include "sim/ai_meister.h"
#include "sim/ai_meister_internal.h"
#include "sim/ai_meister_equip.h"

#include <cstring>
#include <cmath>

namespace guild::sim {

using namespace aimei;

// g_meisterEquipLeaves — active supplemental leaf table (null == inert fallbacks).
// Declared in ai_meister_equip.h; defined here.
MeisterEquipLeaves g_meisterEquipLeaves;

// Maximum sellers returned by EvaluateStockNeeds (dword_13CE278 cap):
// The original loop uses a simple int count. We cap generously.
static constexpr int kMaxStockSellers = 128;

// ===========================================================================
// Scratch tables: the decompile accesses two sets of "scene-type-ID" word arrays
// that configure which production-slot and storage-slot scene types the Meister
// manages. In the original, these are word_B56FAC/B56FAE (production/storage
// "FA" list) and word_B56FC8/B56FCA (the "FC" list). They are written once by
// VIBE_GameLogic_InitGuardState (0x4520d0) from the AI data file. We expose
// them as extern globals from this TU so the bridge / world-layer can populate
// them, and tests can set them directly.
//
// word_B56FAC[v/2]: 2-byte stride scene type list A (production-slot types).
//   Entry 0 = first scene type id (nonzero if populated).
//   Entry k = next; list is zero-terminated.
// word_B56FAE[v/2]: same list advanced by one word (the "next" field).
// dword_B56FA8 + v + 2: the combined "search key" written as a word at byte +2.
//   In the original, dword_B56FA8 is a 4-byte block and the key is written
//   into it at +2; we model this as a 2-byte key scratch updated per iteration.
//
// word_B56FC8[v/2]: scene type list B (storage-slot types).
// word_B56FCA[v/2]: same list, next field.
// dword_B56FC4 + v + 2: search key scratch B.
//
// Both lists are word arrays with 88-slot capacity (matching the workstation
// table stride). The type IDs match scene-type entries in dword_13CE27C (65-byte
// stride item-type-def table).
// ===========================================================================
// (kSceneTypeListCap = 88 is declared in ai_meister_equip.h)

// Definitions of globals declared in ai_meister_equip.h:
i16 g_sceneTypeListA[kSceneTypeListCap] = {};  // word_B56FAC
i16 g_sceneTypeListB[kSceneTypeListCap] = {};  // word_B56FC8

// The "search key" scratch (dword_B56FA8 + 2 / dword_B56FC4 + 2).
// The decompile writes the current iterator's scene type word into these at +2
// and then reads them back. We model as simple word variables updated per step.
static i16 s_searchKeyA = 0;  // dword_B56FA8 view at +2
static i16 s_searchKeyB = 0;  // dword_B56FC4 view at +2

// Work-order flag table: word_B564BC[44*i] where 44 words = 88 bytes = 1 WO row.
// B564BC - B56464 = 88 = kWorkOrderStride, so in the original binary the flags
// live at a parallel base immediately after the WO data block. We model this as
// a separate array with kMaxWorkOrders entries.
// gilde.exe provenance: word_B564BC (0xB564BC)
static u16 g_woBitsTable[kMaxWorkOrders] = {};

// Helpers to access the WO bit table (address exactly as decompile does):
//   word_B564BC[44 * woIdx]  == g_woBitsTable[woIdx]
//   'LOBYTE(word_B564BC[v11/2]) |= bit' where v11 = 88*woIdx -> [44*woIdx]
static inline u16  rdWoBits(int woIdx) {
    return (woIdx >= 0 && woIdx < kMaxWorkOrders) ? g_woBitsTable[woIdx] : 0;
}
static inline void orWoBitsLo(int woIdx, u8 bit) {
    if (woIdx >= 0 && woIdx < kMaxWorkOrders)
        reinterpret_cast<u8*>(&g_woBitsTable[woIdx])[0] |= bit;
}

// ===========================================================================
// Stock-table column byte offsets within g_stockTable[64*i + off]
// (all relative to g_stockTable base = dword_B5444E base):
//   +0   : key dword (HIWORD = type id, LOWORD = sub)     B5444E
//   +6   : backIndex dword (-1 = no workstation)          B54454
//   +10  : sourceBuilding ptr dword                       B54458
//   +18  : field18 dword                                  B54460
//   +22  : required count dword                           B54464
//   +26  : field26 dword                                  B54468
//   +30  : unitPrice float                                flt_B5446C
//   +38  : reserved/incoming dword                        B54474
//   +42  : stock dword                                    B54478
//   +46  : freeCap dword                                  B5447C
//   +50  : flags50 dword (33=item,129=WO)                 B54480
//   +62  : bits word (0x02 reserve, 0x80 storage)         word_B5448C
// ===========================================================================
static constexpr int kST_key      = 0x00;  // B5444E (+0)
static constexpr int kST_backIdx  = 0x06;  // B54454 (+6)
static constexpr int kST_srcBldg  = 0x0A;  // B54458 (+10)
static constexpr int kST_field18  = 0x12;  // B54460 (+18)
static constexpr int kST_required = 0x16;  // B54464 (+22)
static constexpr int kST_field26  = 0x1A;  // B54468 (+26)
static constexpr int kST_price    = 0x1E;  // flt_B5446C (+30) float
static constexpr int kST_reserved = 0x26;  // B54474 (+38)
static constexpr int kST_stock    = 0x2A;  // B54478 (+42)
static constexpr int kST_freeCap  = 0x2E;  // B5447C (+46)
static constexpr int kST_flags50  = 0x32;  // B54480 (+50)
static constexpr int kST_bits     = 0x3E;  // word_B5448C (+62) word

// Work-order table column byte offsets within g_workOrderTable[88*i + off]:
//   +0   : key dword (HIWORD = scene type hi, LOWORD = ?)  B56464
//   +4   : sub-key dword                                    B56468
//   +60  : incoming production dword                        B564A0
//   +64  : current stock dword                              B564A4
//   +68  : free capacity dword                              B564A8
//   +72  : planned qty column                               B564AC (+72?)
//   +76  : reserve target dword                             B564B0
//   flag : g_woBitsTable[i] (word) -- word_B564BC [separate]
// gilde.exe provenance: B56464 column offsets per meister_workstation.h
static constexpr int kWO_key      = 0x00;  // B56464
static constexpr int kWO_subKey   = 0x04;  // B56468
static constexpr int kWO_incoming = 0x3C;  // B564A0 (+60)
static constexpr int kWO_stock    = 0x40;  // B564A4 (+64)
static constexpr int kWO_freeCap  = 0x44;  // B564A8 (+68)
static constexpr int kWO_planned  = 0x4C;  // B564B0 (+76)

// Inline accessors for stock table row i:
static inline u8* st(int i) {
    return g_stockTable + (kStockStride * i);
}
static inline i32  strd32(int i, int off)          { return rd32(st(i), off); }
static inline u16  strdu16(int i, int off)         { return rdu16(st(i), off); }
static inline float stflt(int i, int off)          {
    float v; std::memcpy(&v, st(i)+off, 4); return v;
}
static inline void stwr32(int i, int off, i32 v)   { wr32(st(i), off, v); }
static inline void stwr16(int i, int off, u16 v)   { wr16(st(i), off, v); }
static inline void stwr8 (int i, int off, u8 v)    { wr8 (st(i), off, v); }
static inline void stwrflt(int i, int off, float v){ std::memcpy(st(i)+off, &v, 4); }

// Accessor for HIWORD of stock key (type id field):
static inline i32 stTypeHi(int i) {
    return (i32)((u32)strd32(i, kST_key) >> 16);
}

// Inline accessors for workstation row i:
static inline u8* wo(int i) {
    return g_workOrderTable + (kWorkOrderStride * i);
}
static inline i32  word32(int i, int off)          { return rd32(wo(i), off); }
static inline void wowr32(int i, int off, i32 v)   { wr32(wo(i), off, v); }

// Check if scene-type-list A entry at position k is nonzero (iterator sentinel):
static inline bool stlA_valid(int k) {
    return (k < kSceneTypeListCap) && (g_sceneTypeListA[k] != 0);
}
static inline bool stlB_valid(int k) {
    return (k < kSceneTypeListCap) && (g_sceneTypeListB[k] != 0);
}

// currency byte (byte_6477A1 = 0x00 in runtime — confirmed via get_bytes):
// gilde.exe 0x6477A1 byte = 0x00 (null terminator / currency index 0)
static constexpr u8 kCurrencyByte = 0x00;  // byte_6477A1

// Item-type def table access (dword_13CE27C + 65*typeId + off):
// The decompile uses g_itemTypeDefBase from ai_meister_internal.h.
static inline u16 itemTypeDef16(i16 typeId, int off) {
    return itemTypeWord(typeId, off);
}

// ===========================================================================
// 0x45e350  VIBE_MeisterAi_EquipStaffWeapon  (__usercall eax=a1, edx=a2)
//
// Scan the person array for staff belonging to this Meister's building. For each
// assigned staff member whose action object is in the same building: QueryFind
// the staff's container (+0x178) for scene-type-4 nodes (weapon slots). Walk the
// stock table's FA list first (items in B56FAC), then FC list (items in B56FC8),
// matching against stock-table entries by HIWORD comparison. When a match is
// found and the slot has items (*(v4+40) > 0) and the avatar exists and the
// weapon is compatible, emit a QueueRequest17 (sell-weapon command) through the
// MeisterCommand sink and decrement the slot's item count.
//
// Original locals:
//   v19/v3 = person index; v18 = person rec ptr (= unk_12CE940 + 536*i = pr(v19))
//   v15 = meister name (a1+48); v16 = a2 (building rec ptr or context)
//   v4 = matched item slot ptr (__int16*) within unk_B54490/stock tables
//   v22..v26 = iterator state for the FA/FC list
//   v25/v14 = byte offset into stock-type search (advances by 2 per step)
// ===========================================================================
// gilde.exe 0x45e350 — VIBE_MeisterAi_EquipStaffWeapon (__usercall eax,edx)
int EquipStaffWeapon(u8* meisterRec, int arg) {
    // a2 = arg (building rec ptr or related context for QueueRequest17)
    // v15 = name string at meisterRec+48 (only used in dropped sprintf)
    // v16 = arg

    int result = 0;

    // Scan person array for assigned staff (768 persons, stride 536):
    // gilde.exe: 'if (word_12CE910[268*v19] != -1 && dword_12CEA7C[134*v19] == *(v17+364))'
    // i.e.: pr(v19)[kP_marker] != -1 AND pr(v19)[kP_employer] == *(meisterRec+364)
    i32 ownBldgRec = rd32(meisterRec, kM_bldgRec);  // *(meisterRec+364)

    for (int v19 = 0; v19 < 768; ++v19) {
        result = kStockStride * v19;  // running result = 64*v19 per loop start

        // 'word_12CE910[268*v19] != -1' == pr(v19)+0 != -1 (as i16)
        if (rd16(pr(v19), kP_marker) == (i16)(-1))
            goto next_person;

        // 'dword_12CEA7C[134*v19] == *(v17+364)'
        if (rd32(pr(v19), kP_employer) != ownBldgRec)
            goto next_person;

        // 'byte_12CEA75[536*v19]' — profession byte nonzero
        if (rd8(pr(v19), kP_profByte) == 0)
            goto next_person;

        {
            // Check action-object is in same building:
            // v3 = dword_12CEA94[134*v19]  (action-object ptr)
            i32 v3 = rd32(pr(v19), kP_actionObj);
            if (v3 == 0)
                goto next_person;
            // '*(v3+44) == *(dword_12CEA7C[134*v19]+1)'
            // building id at +1 of employer building rec (handle columns -> rdptr):
            u8* empRec = rdptr(pr(v19), kP_employer);
            u8* aoRec  = resolveHandle(v3);
            if (!empRec || !aoRec)
                goto next_person;
            i32 employerBldgId = rd32(empRec, kB_id1);
            i32 actionObjBldg  = rd32(aoRec, 44);
            if (actionObjBldg != employerBldgId)
                goto next_person;

            // QueryFind: search staff container (+0x178) for type 4 scene node (weapon):
            // 'VIBE_GameObject_QueryFind(dword_12CEA88[134*v19], 1, 4, 12)'
            // = QueryFind(containerSceneId, 1 filter: op=4 type==12)
            // Through g_meisterLeaves->queryFind:
            i32 containerSceneId = rd32(pr(v19), kP_container);
            u8* v22_ptr = nullptr;
            if (g_meisterLeaves && g_meisterLeaves->queryFind) {
                // Raw call: QueryFind(root, 1, op=4, val=12)
                // op 4 = typedefByte==val. val=12 means weapon-slot kind 12?
                // Pass as flat filter list: {4, 12}
                static const int filters[] = {4, 12};
                v22_ptr = g_meisterLeaves->queryFind(containerSceneId, filters, 2);
            }
            // v22 = (__int16*)result  (the weapon slot node base)
            i16* v22 = reinterpret_cast<i16*>(v22_ptr);

            if (v22_ptr != nullptr) {
                // Found a weapon slot. Now search for matching stock item.
                // 'v25 = 2; v24 = &unk_B54490;'
                // The FA list search: iterate s_searchKeyA through g_sceneTypeListA[]
                // The decompile walks dword_B56FA8+v25+2 (the key scratch at +2)
                // and word_B56FAC[v25/2] (the list entry).
                // v25 starts at 2 (i.e., first entry is at v25/2 = 1, BUT list[0] is the guard).
                // Wait: 'if (word_B56FAC[0])' guards the loop (list[0] must be nonzero).
                // Inside: 'word_B56FAC[v25/2]' where v25 starts at 2 => list[1].
                // And '*(dword_B56FA8+v25+2)' is written with *v22 (the slot's type word).
                // Then compared against 'dword_B5444E[v5/4u] >> 16' (stock type hi-word).
                //
                // The loop structure matches: for each FA list entry (starting at index 1),
                // write the current slot's type word as search key, then scan stock table
                // for a matching type. 'result=1' means found.
                //
                // Simplified faithful translation:
                // v4: matched-slot pointer. CRITICAL (disasm 0x45e47b/0x45e48c/0x45e50c):
                // the original does NOT point v4 at the matching stock row. v4 is captured
                // from v24 = unk_B54490 (= dword_B5444E + 0x42 = g_stockTable+66), advanced
                // by 0x40 (64) bytes PER FA/FC LIST ITERATION (not per matching stock row).
                // On a hit it stores that list-position pointer (v26=v24). So:
                //   v4 = g_stockTable + 0x42 + 64*k   (k = list iteration index, 0-based)
                // We model v4 as a byte offset into g_stockTable (-1 == none).
                constexpr int kUnkB54490Off = 0x42;  // unk_B54490 - dword_B5444E
                int v4off = -1;  // byte offset of v4 into g_stockTable

                // --- FA list pass ---
                // 'if (word_B56FAC[0])' — list is populated
                if (stlA_valid(0)) {
                    // v21 = dword_B56FE0 << 6 (stock table byte bound)
                    int v21 = g_stockRowCount << 6;  // = g_stockRowCount * 64
                    result = 0;
                    unsigned int v25 = 2;        // byte index into FA list (starts at word 1)
                    int v24off = kUnkB54490Off;  // v24 = &unk_B54490 (byte off into g_stockTable)

                    while (stlA_valid(v25 / 2)) {
                        // 'v6 = *v22' (the weapon slot's type word)
                        i16 v6 = *v22;
                        // 'write key: *(dword_B56FA8 + v25 + 2) = *v22'
                        s_searchKeyA = v6;
                        const int v26off = v24off;  // v26 = v24 captured at iteration start

                        if (v6 != 0) {
                            // Scan stock table for matching typeHi (signed >>16, both sides):
                            // 'dword_B5444E[v7/4u] >> 16 == *(int*)(dword_B56FA8+v25+2) >> 16'
                            // The written word at +2 makes the rhs == (i32)(i16)v6 (sign-extended).
                            i32 targetHi = (i32)v6;  // sar of word at +2 == sign-extended v6
                            int v7 = 0;
                            while (v7 < v21 && !result) {
                                i32 stockKeyHi = strd32(v7 / 64, kST_key) >> 16;  // signed sar
                                if (stockKeyHi == targetHi) {
                                    v4off = v26off;   // v4 = v26 (the list-position ptr)
                                    result = 1;
                                }
                                v7 += 64;
                            }
                        }

                        v25 += 2;
                        v24off += 64;     // v24 += 32 words = 64 bytes
                        if (result) break;
                    }
                }

                // --- FC list pass (only if FA list didn't find a match) ---
                if (!result) {
                    // 'v23 = &unk_B54490; v14 = 2; v20 = dword_B56FE0 << 6'
                    unsigned int v14 = 2;
                    int v20 = g_stockRowCount << 6;
                    int v23off = kUnkB54490Off;  // v23 = &unk_B54490

                    while (true) {
                        // 'if (!*(dword_B56FC4+v14+2) || !word_B56FC8[v14/2]) break'
                        if (!stlB_valid(v14 / 2)) break;

                        i16 v9 = *v22;
                        s_searchKeyB = v9;
                        const int v23offCap = v23off;  // captured for the hit store (v4 = v23)

                        if (v9 != 0) {
                            i32 targetHi = (i32)v9;  // signed
                            int v10 = 0;
                            while (v10 < v20 && !result) {
                                i32 stockKeyHi = strd32(v10 / 64, kST_key) >> 16;
                                if (stockKeyHi == targetHi) {
                                    result = 1;
                                    v4off = v23offCap;   // v4 = v23
                                }
                                v10 += 64;
                            }
                        }

                        v14 += 2;
                        v23off += 64;     // v23 += 32 words
                        if (result) break;
                    }
                }

                // --- If a matching slot found ---
                if (v4off >= 0) {
                    // v4 = g_stockTable + v4off. '*((_DWORD*)v4+10)' = dword at v4+40,
                    // '*v4' = i16 at v4+0.  (These land inside the +0x42-shifted view.)
                    u8* v4ptr = g_stockTable + v4off;
                    i32  countAtOff40 = rd32(v4ptr, 40);  // *((_DWORD*)v4 + 10)

                    if (countAtOff40 > 0) {
                        // Avatar check: VIBE_Avatar_LookupById(*v4) (type word at v4[0])
                        i16 typeWord = rd16(v4ptr, 0);  // *v4 = first word
                        u8* avatarRec = nullptr;
                        if (g_meisterLeaves && g_meisterLeaves->avatarLookupById)
                            avatarRec = g_meisterLeaves->avatarLookupById(typeWord);

                        if (avatarRec != nullptr) {
                            // Weapon slot compatibility check:
                            // 'VIBE_Inventory_IsWeaponSlotCompatible(v17, *v4)'
                            // v17 = meisterRec, *v4 = typeWord
                            int compat = 0;
                            if (g_meisterLeaves && g_meisterLeaves->isWeaponSlotCompatible)
                                compat = g_meisterLeaves->isWeaponSlotCompatible(meisterRec, typeWord);

                            if (compat) {
                                // VIBE_Crt_Sprintf_0(...) — DROPPED (debug log, no side effect).

                                // QueueRequest17 — emit via MeisterCommand sink:
                                // 'QueueRequest17(*(int*)((char*)dword_12CE914 + v12),
                                //                *(v16+2), 1, *v4, byte_6477A1, 0)'
                                // v12/v11 = ecx from stack = byte offset into g_personIds for person v19
                                // = g_personIds[v19]
                                // *(v16+2) = *(arg+2) — dword at arg+2 (building id from arg context)
                                i32 actorId  = g_personIds[v19];
                                // 'arg' (orig a2) is a record-handle; resolve + read +2.
                                u8* argRec = resolveHandle(arg);
                                i32 bldgIdFromArg = argRec ? rd32(argRec, 2) : -1;
                                // Emit as buyItem command (QueueRequest17 = sell/buy op):
                                MeisterCommand cmd{};
                                cmd.buyItem     = true;
                                cmd.actorId     = actorId;
                                cmd.buildingId  = bldgIdFromArg;
                                cmd.buyItemType = typeWord;
                                cmd.buyAmount   = 1;
                                cmd.buyPrice    = (i32)kCurrencyByte;  // byte_6477A1 = 0
                                if (g_meisterCmdSink)
                                    g_meisterCmdSink->push(cmd);

                                // '--*((_DWORD*)v4+10)' — decrement count at v4+40:
                                wr32(v4ptr, 40, countAtOff40 - 1);
                            }
                        }
                    }
                }

                // 'goto LABEL_11' (next person):
                goto next_person;
            }

            // No weapon slot found — scan stock table to sum freeCap:
            // 'if (dword_B56FE0 > 0) { do result += 64; while (result < dword_B56FE0 << 6); }'
            // This is the fallback when QueryFind returned null.
            if (g_stockRowCount > 0) {
                // Note: result was set to kStockStride*v19 at loop top.
                // The decompile's loop adds 64 repeatedly until result >= g_stockRowCount*64.
                // This is just an accounting sum; the real semantic is unclear but we mirror exactly.
                do {
                    result += 64;
                } while (result < (g_stockRowCount << 6));
            }
        }

next_person:
        ;
    }

    return result;
}

// ===========================================================================
// 0x45a62c  VIBE_MeisterAi_CollectStorageItems  (__usercall eax=a1, edx=a2)
//
// Populate the stock table (g_stockTable / dword_B5444E) from two sources:
//   1. The FA scene-type list (word_B56FAC[]): for each type in the list not
//      already in the stock table, add a new row with that type, price, stock
//      count, and free-capacity (via Inventory_FindItemStock /
//      Inventory_ComputeFreeCapacity on the a2 container).
//   2. The FC scene-type list (word_B56FC8[]): same procedure.
//   3. The He handler list: for each handler whose building id matches the
//      Meister's own building, look up the building's object by handler
//      building id (+172), call CollectProductionSlots on it, walk the
//      production slot info, and accumulate incoming-delivery counts into the
//      stock table's reserved/incoming field (+38) for matching type ids.
//
// a1 = meisterRec, a2 = container rec (the building's inventory container).
// gilde.exe 0x45a62c
// ===========================================================================
void MeisterCollectStorageItems(u8* meisterRec) {
    // NOTE: The original has a2 (edx) as a container rec ptr used for
    // Inventory_FindItemStock and Inventory_ComputeFreeCapacity. However,
    // the public API declares MeisterCollectStorageItems(u8* meisterRec)
    // without the container rec. In the original decompile, a2 is an
    // additional arg passed by the CALLER (the Farming/Production loop), not
    // from meisterRec itself. Since our public signature matches ai_meister.h,
    // we use meisterRec's building record's scene root (kM_bldgRec) as
    // the context. The container rec leaf calls go through g_meisterEquipLeaves.
    // When null, leaves return 0/null (no stock populated from FA/FC lists).

    i32 ownBldgRec = rd32(meisterRec, kM_bldgRec);  // *(meisterRec+364) handle
    u8* ownBldgPtr = resolveHandle(ownBldgRec);
    i32 meisterBldgId = ownBldgPtr ? rd32(ownBldgPtr, kB_id1) : -1;

    // The container rec for inventory calls (a2 in orig) is the meister's
    // building's inventory container. We pass the resolved building record as the
    // container base when calling inventory leaves (the bridge knows the container).
    u8* containerRec = ownBldgPtr;

    // --- Pass 1: FA scene-type list (word_B56FAC[]) ---
    // 'v3 = 0; if (word_B56FAC[0]) { do { ... } while (v14); }'
    // gilde.exe 0x45a62c: v14 = word_B56FAE[v3/2] (= FAC[v3/2+1], the NEXT entry)
    // then v3 += 2; while (v14). Loop terminates when the next entry is zero.
    if (stlA_valid(0)) {
        unsigned int v3 = 0;  // byte index into FA list
        i16 v14 = 1;          // termination sentinel (mirrors original v14 register; set inside loop)
        do {
            if (v3 / 2 >= (unsigned)kSceneTypeListCap) break; // bound the FA list scan
            i16 listEntry = g_sceneTypeListA[v3 / 2];
            i32 searchHi  = (u16)listEntry;  // typeHi = the list's scene type id

            // Search stock table for existing entry with same typeHi:
            // 'dword_B5444E[v5/4u] >> 16 == *(int*)((char*)&dword_B56FA8 + v3 + 2) >> 16'
            // The key written at B56FA8+v3+2 is word_B56FAC[v3/2] = listEntry.
            // So typeHi comparison = stockKeyHi == (u16)listEntry.
            s_searchKeyA = listEntry;
            int v4_idx = g_stockRowCount;  // assume not found until match
            {
                int v5b = 0;
                for (int si = 0; si < g_stockRowCount; ++si, v5b += 64) {
                    i32 stHi = (i32)((u32)strd32(si, kST_key) >> 16);
                    if (stHi == searchHi) {
                        v4_idx = si;
                        break;
                    }
                }
            }

            // 'if (v4 >= dword_B56FE0)' — not found, add new row:
            if (v4_idx >= g_stockRowCount) {
                // Compute byte offset into stock table: v6 = dword_B56FE0 << 6
                int newRow = g_stockRowCount;

                // '*(word_B5444E + v6 + 2) = word_B56FAC[v3/2]'
                // i.e. set typeHi in new stock row: key dword HIWORD = listEntry
                // The orig writes: *(_WORD*)((char*)dword_B5444E + v6 + 2) = word_B56FAC[v3/2]
                // That's the HIWORD of the key dword (+2 in a LE dword = the upper 2 bytes).
                {
                    i32 keyDw = strd32(newRow, kST_key);
                    // write listEntry as the hi-word:
                    keyDw = (keyDw & 0x0000FFFF) | ((i32)(u16)listEntry << 16);
                    stwr32(newRow, kST_key, keyDw);
                }
                // Init other fields:
                // '*(dword_B54454 + v6) = -1'  -> backIndex = -1
                stwr32(newRow, kST_backIdx, -1);
                // '*(dword_B54458 + v6) = 0'   -> sourceBuilding = 0
                stwr32(newRow, kST_srcBldg, 0);
                // '*(dword_B54460 + v6) = 0'   -> field18 = 0
                stwr32(newRow, kST_field18, 0);
                // '*(dword_B54464 + v6) = 0'   -> required = 0
                stwr32(newRow, kST_required, 0);
                // '*(dword_B54468 + v6) = 0'   -> field26 = 0
                stwr32(newRow, kST_field26, 0);

                // Market price: 'VIBE_Building_LookupCachedMarketPrice(HIWORD(key), byte_6477A1)'
                float priceF = 0.0f;
                if (g_meisterEquipLeaves.lookupCachedMarketPrice) {
                    double p = g_meisterEquipLeaves.lookupCachedMarketPrice((i16)listEntry, kCurrencyByte);
                    priceF = (float)p;
                }
                // CRITICAL (disasm 0x45a6d4 'xor ecx,ecx' before the call, then the
                // stores use ecx): the decompile's `v9` is just ecx == 0 (a register the
                // decompiler lost). So the FA block sets reserved=0, stock=0, bits=0 —
                // identical to the FC block, NOT (int)price. Verified:
                //   0x45a6ea mov B54474[eax],ecx   (=0)
                //   0x45a6f2 mov B54478[eax],ecx   (=0)
                //   0x45a6f0 xor esi,ecx; 0x45a70d mov word_B5448C[eax],si   (=0)
                stwr32(newRow, kST_reserved, 0);          // B54474 = 0
                stwr32(newRow, kST_stock,    0);          // B54478 = 0 (overwritten below)
                stwr32(newRow, kST_flags50,  129);        // B54480 = 0x81
                stwrflt(newRow, kST_price,   priceF);     // flt_B5446C
                stwr16(newRow, kST_bits, 0);              // word_B5448C = 0

                // 'dword_B54478[16*dword_B56FE0] = VIBE_Inventory_FindItemStock(a2, SHIWORD(v10))'
                // SHIWORD(v10) = signed hi-word of key = (i16)listEntry
                // FindItemStock returns the current stock count for that item type.
                if (g_meisterEquipLeaves.findItemStock && containerRec != nullptr) {
                    i32 stockCount = g_meisterEquipLeaves.findItemStock(containerRec, (i16)listEntry);
                    stwr32(newRow, kST_stock, stockCount);  // B54478
                }

                // 'v12 = VIBE_Inventory_ComputeFreeCapacity(a2, HIWORD(key), v11, 1000000)'
                // Returns free capacity as a pointer (actually the capacity count in an int).
                // We model it returning an int directly (see MeisterAiLeaves::inventoryFreeCapacity).
                i32 freeCap = 0;
                if (g_meisterLeaves && g_meisterLeaves->inventoryFreeCapacity && containerRec != nullptr) {
                    freeCap = g_meisterLeaves->inventoryFreeCapacity(containerRec, (int)listEntry, 0, 1000000);
                }
                // '*(dword_B5447C + v13) = (int)v12'  where v13 = dword_B56FE0++ << 6
                stwr32(newRow, kST_freeCap, freeCap);  // B5447C

                ++g_stockRowCount;  // 'dword_B56FE0++'
            }

            // Advance to next FA list entry:
            // 'v14 = word_B56FAE[v3/2]; v3 += 2;'
            // word_B56FAE[k] = word_B56FAC[k+1], so v14 = the NEXT entry after current.
            // Loop continues while the next entry is nonzero.
            v14 = (v3 / 2 + 1 < kSceneTypeListCap) ? g_sceneTypeListA[v3 / 2 + 1] : 0;
            v3 += 2;
        } while (v14 != 0);
    }

    // --- Pass 2: FC scene-type list (word_B56FC8[]) ---
    // Exactly mirrors Pass 1 but for g_sceneTypeListB[]:
    if (stlB_valid(0)) {
        unsigned int v15 = 0;
        do {
            if (v15 / 2 >= (unsigned)kSceneTypeListCap) break; // bound the FC list scan
            i16 listEntry = g_sceneTypeListB[v15 / 2];
            i32 searchHi  = (u16)listEntry;
            s_searchKeyB  = listEntry;

            int v4_idx = g_stockRowCount;
            {
                int v17b = 0;
                for (int si = 0; si < g_stockRowCount; ++si, v17b += 64) {
                    i32 stHi = (i32)((u32)strd32(si, kST_key) >> 16);
                    if (stHi == searchHi) {
                        v4_idx = si;
                        break;
                    }
                }
            }

            if (v4_idx >= g_stockRowCount) {
                int newRow = g_stockRowCount;
                {
                    i32 keyDw = strd32(newRow, kST_key);
                    keyDw = (keyDw & 0x0000FFFF) | ((i32)(u16)listEntry << 16);
                    stwr32(newRow, kST_key, keyDw);
                }
                stwr32(newRow, kST_backIdx, -1);
                stwr32(newRow, kST_srcBldg, 0);
                stwr32(newRow, kST_field18, 0);
                stwr32(newRow, kST_required, 0);
                stwr32(newRow, kST_field26, 0);

                float priceF = 0.0f;
                if (g_meisterEquipLeaves.lookupCachedMarketPrice) {
                    double p = g_meisterEquipLeaves.lookupCachedMarketPrice((i16)listEntry, kCurrencyByte);
                    priceF = (float)p;
                }
                // FC block: reserved=0, stock=0 (not (int)price like FA block)
                stwr32(newRow, kST_reserved, 0);   // B54474 = 0 for FC
                stwr32(newRow, kST_stock,    0);   // B54478 = 0
                stwr32(newRow, kST_flags50,  129); // B54480 = 129
                stwrflt(newRow, kST_price, priceF);
                stwr16(newRow, kST_bits, 0);       // bits = 0 for FC

                if (g_meisterEquipLeaves.findItemStock && containerRec != nullptr) {
                    i32 stockCount = g_meisterEquipLeaves.findItemStock(containerRec, (i16)listEntry);
                    stwr32(newRow, kST_stock, stockCount);
                }

                i32 freeCap = 0;
                if (g_meisterLeaves && g_meisterLeaves->inventoryFreeCapacity && containerRec != nullptr) {
                    freeCap = g_meisterLeaves->inventoryFreeCapacity(containerRec, (int)listEntry, 0, 1000000);
                }
                stwr32(newRow, kST_freeCap, freeCap);

                ++g_stockRowCount;
            }

            v15 += 2;
            i16 v24 = (v15 / 2 < kSceneTypeListCap) ? g_sceneTypeListB[v15 / 2] : 0;
            if (!v24) break;
        } while (true);
    }

    // --- Pass 3: He handler list ---
    // 'VIBE_He_FindFirstHandlerByFilter(2, 0, 11, 3, *(bldgRec+1))'
    // Filter 11, mode 3, key = meister's building id (at bldgRec+1).
    u8* handlerRec = nullptr;
    if (g_meisterLeaves && g_meisterLeaves->heFindFirst) {
        handlerRec = g_meisterLeaves->heFindFirst(2, 0, 11, 3, meisterBldgId);
    }

    while (handlerRec != nullptr) {
        // 'ObjectById = VIBE_Object_FindObjectById(*(handler+172))'
        // handler+172 = 43*4 = the matched building id field per leaf_signatures.md
        i32 handlerBldgId = rd32(handlerRec, 172);
        u8* objRec = nullptr;
        if (g_meisterEquipLeaves.objectFindById)
            objRec = g_meisterEquipLeaves.objectFindById(handlerBldgId);

        if (objRec != nullptr) {
            // 'VIBE_Inventory_CollectProductionSlots(ObjectById, (int)v39)'
            // outBuf is a 46-dword (184-byte) buffer; v39[0] = slot count.
            i32 outBuf[46] = {};
            if (g_meisterEquipLeaves.collectProductionSlots)
                g_meisterEquipLeaves.collectProductionSlots(objRec, outBuf);

            int slotCount = outBuf[0];  // 'v39[0]'

            if (slotCount > 0) {
                // Walk production slot type list: 'v42 = 2 * v39[0]' (byte bound)
                // The slot types are at 'v39[18] + offset' (word stride).
                // Per decompile: '*(_WORD*)((char*)&v39[18] + v27)' where v27 is byte offset.
                // v39[18] = the 18th dword = byte offset 72 of outBuf. The types start there.
                // 'v45 = 0' (word offset into slot types); 'v27 = 0' (byte offset into types).
                int v42 = 2 * slotCount;  // byte bound for slot-type array
                int v27 = 0;              // byte offset into types (advances by 2)
                int v45 = 0;              // dword offset (advances by 4 = 1 dword per slot)

                i8* slotTypeBase   = reinterpret_cast<i8*>(&outBuf[18]);  // &v39[18]
                i8* slotCountBase  = reinterpret_cast<i8*>(&outBuf[26]);  // &v39[26]

                while (v27 < v42) {
                    // '*(_WORD*)((char*)&v39[18] + v27)' — the slot's scene type word
                    i16 slotType = 0;
                    std::memcpy(&slotType, slotTypeBase + v27, 2);

                    if (slotType != 0) {
                        // Check if slot type matches any handler output type:
                        // The handler has output items at 'i + 206' (word stride × 3):
                        // 'v28 = i; v30 = 0; do { if (*(int*)(v28+206)>>16 == ...) v29=1; v28+=2; ++v30; } while (!v29 && v30<3)'
                        // i = (int)handlerRec; handler+206 = handler's item-type words (3 items, word stride 2).
                        // '*(int*)(v28+206)>>16' = hi-word of dword at handler+206+k*2.
                        // That hi-word matches against '*(int*)((char*)&v39[17]+v27+2)>>16'
                        // = hi-word of dword at (slotTypeBase - 4 + v27 + 2) = ...
                        // Actually: '&v39[17]' = &outBuf[17] = byte offset 68. Then +v27+2.
                        // So slot item id = dword at outBuf bytes [68 + v27 + 2 .. 71+v27+2]>>16.
                        // This is the hi-word of the slot's item key (similar to stock table key).
                        i8* slotItemKeyBase = reinterpret_cast<i8*>(&outBuf[17]);  // &v39[17]
                        i32 slotItemKeyDw = 0;
                        std::memcpy(&slotItemKeyDw, slotItemKeyBase + v27 + 2, 4);
                        i32 slotItemHi = (i32)((u32)slotItemKeyDw >> 16);

                        bool v29 = false;
                        for (int k = 0; k < 3 && !v29; ++k) {
                            // handler+206+k*2 as dword >> 16:
                            i32 hDw = 0;
                            std::memcpy(&hDw, handlerRec + 206 + k * 2, 4);
                            i32 hHi = (i32)((u32)hDw >> 16);
                            if (hHi == slotItemHi)
                                v29 = true;
                        }

                        if (!v29 && g_stockRowCount > 0) {
                            // Accumulate into stock table's incoming/reserved (+38):
                            // 'v33=0; do { if (v34==dword_B5444E[v33/4]>>16) B54474[v33/4]+=v39[26+v45/4]; v33+=64; } while v33<v32'
                            // v34 = slotItemHi, v32 = g_stockRowCount<<6
                            // dword at outBuf at slot-count position:
                            // '*((_DWORD*)((char*)&v39[26] + v31))' where v31=v45 (dword offset)
                            i32 addCount = 0;
                            std::memcpy(&addCount, slotCountBase + v45, 4);

                            for (int si = 0; si < g_stockRowCount; ++si) {
                                i32 stHi = (i32)((u32)strd32(si, kST_key) >> 16);
                                if (stHi == slotItemHi) {
                                    i32 cur = strd32(si, kST_reserved);
                                    stwr32(si, kST_reserved, cur + addCount);  // B54474 += count
                                }
                            }
                        }
                    }

                    v27 += 2;
                    v45 += 4;
                }
            }

            // 'v35=i; v44=i; v40 = i+6; do { if (*(_WORD*)(v35+214) && ...) } while (v35!=v40)'
            // This is an additional loop over 3 handler output slots starting at handler+214 (word stride 2):
            // handler+212 as dword >> 16 = output item hi-word.
            // handler+220 as dword = output count per slot.
            // The loop: v35 advances by 2 (word), v44 by 4 (dword), while v35 != i+6 (3 iterations).
            {
                i8* h = reinterpret_cast<i8*>(handlerRec);
                for (int k = 0; k < 3; ++k) {
                    // '*(_WORD*)(v35+214)' — nonzero check on handler+214+k*2
                    i16 outWord = 0;
                    std::memcpy(&outWord, h + 214 + k * 2, 2);
                    if (outWord != 0 && g_stockRowCount > 0) {
                        // 'dword at v35+212' >> 16 = output item hi-word:
                        i32 outKeyDw = 0;
                        std::memcpy(&outKeyDw, h + 212 + k * 2, 4);
                        i32 outItemHi = (i32)((u32)outKeyDw >> 16);

                        // 'dword at v44+220' = the output count:
                        i32 outCount = 0;
                        std::memcpy(&outCount, h + 220 + k * 4, 4);

                        for (int si = 0; si < g_stockRowCount; ++si) {
                            i32 stHi = (i32)((u32)strd32(si, kST_key) >> 16);
                            if (stHi == outItemHi) {
                                i32 cur = strd32(si, kST_reserved);
                                stwr32(si, kST_reserved, cur + outCount);  // B54474 += count
                            }
                        }
                    }
                }
            }
        }

        // Next handler: 'VIBE_He_FindNextMatchingHandler()'
        handlerRec = nullptr;
        if (g_meisterLeaves && g_meisterLeaves->heFindNext)
            handlerRec = g_meisterLeaves->heFindNext();
    }
}

// ===========================================================================
// 0x45c10c  VIBE_MeisterAi_GatherRequiredItems  (__usercall eax=a1, ebx=a2)
//
// For each assigned staff member of this Meister's building: QueryFind their
// container for a type-4 (weapon) node. If found, walk the FA/FC lists and
// mark matching stock-table entries as "required" (increment B54464, set bit 2
// of bits). Then for each stock row with required > 0, compute the NET shortfall
// (required - (reserved+stock)), call EvaluateStockNeeds to find sellers, and
// assign the best seller to B54458. Finally, for each FA/FC list entry, reduce
// the purchase quantity to what the budget (a2) can afford.
//
// a1 = meisterRec, a2 = budget (cash available for purchasing).
// gilde.exe 0x45c10c
// ===========================================================================
void MeisterGatherRequiredItems(u8* meisterRec, i32 neededId) {
    // a2 = neededId in the public API, but the orig 'a2@<ebx>' is the BUDGET
    // (cash available). The public signature uses 'neededId' but the orig decompile
    // shows it as the budget dword passed in ebx. We use it as budget.
    i32 budget = neededId;  // a2 = budget (orig ebx)

    i32 ownBldgRec = rd32(meisterRec, kM_bldgRec);

    // --- Phase 1: for each assigned staff, QueryFind weapon slot, mark required ---
    // 'for (i=0; i!=411648; i+=536)' => 411648/536 = 768 persons
    // Note: 411648 = 768 * 536.
    for (unsigned int i = 0; i < 411648u; i += 536) {
        int pidx = i / 536;
        // Check person is alive and employed by our building:
        // 'word_12CE910[i/2] != -1 && dword_12CEA7C[i/4] == *(a1+364) && byte_12CEA75[i]'
        if (rd16(pr(pidx), kP_marker) == (i16)(-1)) continue;
        if (rd32(pr(pidx), kP_employer) != ownBldgRec) continue;
        if (rd8(pr(pidx), kP_profByte) == 0) continue;

        // QueryFind the person's container for a weapon-slot node (type 4, val 12):
        i32 containerSceneId = rd32(pr(pidx), kP_container);
        u8* queryResult = nullptr;
        if (g_meisterLeaves && g_meisterLeaves->queryFind) {
            static const int filters[] = {4, 12};
            queryResult = g_meisterLeaves->queryFind(containerSceneId, filters, 2);
        }

        i16* v36 = reinterpret_cast<i16*>(queryResult);

        if (queryResult != nullptr) {
            // --- FA list search: find matching stock row and mark required ---
            // 'v15=0; v38=2; if (word_B56FAC[0]) { do { ... } while (word_B56FA8+v38+2); }'
            int v15 = 0;  // "found" flag
            unsigned int v38 = 2;

            if (stlA_valid(0)) {
                int v30 = g_stockRowCount << 6;  // byte bound
                do {
                    if (!stlA_valid(v38 / 2)) break;

                    i16 v16 = *v36;
                    s_searchKeyA = v16;  // write key at B56FA8+v38+2

                    if (v16 != 0) {
                        unsigned int v17 = 0;
                        do {
                            if ((int)v17 >= v30) break;
                            int si = v17 / 64;
                            i32 stHi = (i32)((u32)strd32(si, kST_key) >> 16);
                            i32 keyHi = (u16)s_searchKeyA;
                            if (stHi == keyHi) {
                                // '++dword_B54464[v17/4]' -> required++
                                i32 req = strd32(si, kST_required);
                                stwr32(si, kST_required, req + 1);
                                v15 = 1;
                                // 'LOBYTE(word_B5448C[v17/2]) |= 2u'
                                u16 bits = strdu16(si, kST_bits);
                                stwr16(si, kST_bits, bits | 0x0002);
                            }
                            v17 += 64;
                        } while (!v15);
                    }

                    v38 += 2;
                    if (v15) break;

                    // loop condition: '*(word_B56FA8+v38+2)' = next key; equivalent to list valid
                } while (stlA_valid(v38 / 2));
            }

            // --- FC list search (if FA didn't find a match) ---
            if (!v15) {
                unsigned int v37 = 2;
                int v18 = g_stockRowCount << 6;
                do {
                    if (!stlB_valid(v37 / 2)) break;
                    // 'if (!*(dword_B56FC4+v37+2) || !word_B56FC8[v37/2]) break'
                    // (after we write the key the first check passes; only the list guard matters)

                    i16 v19 = *v36;
                    s_searchKeyB = v19;

                    if (v19 != 0) {
                        unsigned int v20 = 0;
                        do {
                            if ((int)v20 >= v18) break;
                            int si = v20 / 64;
                            i32 stHi = (i32)((u32)strd32(si, kST_key) >> 16);
                            i32 keyHi = (u16)s_searchKeyB;
                            if (stHi == keyHi) {
                                u8 lo = rd8(st(si), kST_bits) | 2u;
                                i32 req = strd32(si, kST_required);
                                stwr32(si, kST_required, req + 1);
                                wr8(st(si), kST_bits, lo);
                                v15 = 1;
                            }
                            v20 += 64;
                        } while (!v15);
                    }

                    v37 += 2;
                } while (!v15);
            }
        } else {
            // QueryFind returned null; the decompile falls through to LABEL_9 (no-op).
            // However, for the 'else if (dword_B56FE0 > 0)' fallback:
            // When queryResult==null (v2==0) AND stockRowCount>0:
            // the orig does a direct stock-table walk comparing the first FA-list key
            // against stock entries and increments B54464 / bits for first match.
            // This is the 'else if' branch:
            if (g_stockRowCount > 0) {
                // 'while (*(int*)((char*)dword_B5444E + (_DWORD)v2)>>16 != *(int*)((char*)&dword_B56FA8+2)>>16)'
                // v2 = 0 (null queryResult = 0), so byte-offset into B5444E is 0.
                // The comparison: B5444E[0]>>16 != B56FA8+2>>16 (key at +2 = s_searchKeyA as hi-word).
                // This searches for the first FA-list key (s_searchKeyA from last iteration, or 0).
                // The decompile uses v2 as a byte offset starting at 0 and stepping by 32 words (64 bytes).
                // Actually: v2 is initialized to 0 at the start of the null-branch,
                // and the comparison uses v2 as a stock-table byte offset.
                // '*(int*)((char*)B5444E + v2)>>16' = hi-word of stock row at byte offset v2.
                // Compare with '*(int*)((char*)&B56FA8+2)>>16' = s_searchKeyA as hi-word.
                i32 targetHi = (u16)s_searchKeyA;
                int v2off = 0;
                while (v2off < (g_stockRowCount << 6)) {
                    int si = v2off / 64;
                    i32 stHi = (i32)((u32)strd32(si, kST_key) >> 16);
                    if (stHi == targetHi) {
                        // '++*(int*)((char*)B54464 + v2)' -> required++
                        i32 req = strd32(si, kST_required);
                        stwr32(si, kST_required, req + 1);
                        // '*((_BYTE*)word_B5448C + v2) |= 2u' -> lo byte of bits |= 2
                        u8 lb = rd8(st(si), kST_bits) | 2u;
                        wr8(st(si), kST_bits, lb);
                        break;
                    }
                    v2off += 64;
                }
            }
        }
    }

    // --- Phase 2: for each stock row with required > 0, net the shortfall ---
    // 'v34=0; if (dword_B56FE0>0) { v29=0; do { if (B54464[v29]) { ... } v29+=16; ++v34; } while v34<B56FE0 }'
    // v29 is a dword-word-array index: B54464[v29] where B54464 is a dword* => v29 increments as 'v29+=16'
    // wait: 'v29 += 16' but that's treating B54464 as a DWORD array with stride 16 dwords (64 bytes) = one stock row.
    // Actually: B54464 is dword_B54464, so B54464[v29] = *(dword_B54464 + v29*4)?
    // No — in the decompile, 'dword_B54464[v29]' where 'v29+=16' means v29 is an int-index that steps by 16.
    // That would be 16 dwords per step = 64 bytes = 1 stock row. So 'dword_B54464[16*i]' = stock row i required.
    // This matches: B54464 offset in stock row = +22. 'dword_B54464[v29]' with v29+=16: B54464+v29*4 = B5444E + 22 + v29*4.
    // For i=0: v29=0 -> B54464 + 0 = B54464 (+22 from base). For i=1: v29=16 -> B54464 + 64 = row 1 required. Correct!
    for (int v34 = 0; v34 < g_stockRowCount; ++v34) {
        i32 req = strd32(v34, kST_required);  // B54464[16*v34] = required at row v34
        if (req == 0) continue;

        // Find matching FA or FC list entry and subtract (reserved+stock):
        int v3 = 0;  // "found" flag
        int v4_iter = 0;

        // FA list scan:
        if (stlA_valid(0)) {
            do {
                i32 stHi = (i32)((u32)strd32(v34, kST_key) >> 16);
                // '*(int*)((char*)&dword_B56FA8 + v4*2 + 2)>>16' = g_sceneTypeListA[v4]
                i32 listHi = (u16)g_sceneTypeListA[v4_iter];
                if (stHi == listHi) {
                    // 'v5 = B54464[v29] - (B54474[v29] + B54478[v29])'
                    i32 res = strd32(v34, kST_reserved);
                    i32 stk = strd32(v34, kST_stock);
                    i32 v5  = req - (res + stk);
                    if (v5 < 0) v5 = 0;
                    stwr32(v34, kST_required, v5);
                    v3 = 1;
                }
                ++v4_iter;
            } while (!v3 && stlA_valid(v4_iter));
        }

        // FC list scan (if FA didn't find):
        if (!v3) {
            int v7 = 0;
            do {
                if (!stlB_valid(v7)) break;
                i32 stHi = (i32)((u32)strd32(v34, kST_key) >> 16);
                i32 listHi = (u16)g_sceneTypeListB[v7];
                if (stHi == listHi) {
                    i32 res = strd32(v34, kST_reserved);
                    i32 stk = strd32(v34, kST_stock);
                    i32 v8  = req - (res + stk);
                    if (v8 < 0) v8 = 0;
                    stwr32(v34, kST_required, v8);
                    v3 = 1;
                }
                ++v7;
            } while (!v3);
        }

        // Now re-read required (may have been updated):
        req = strd32(v34, kST_required);

        // 'if (B54464[v29])' — if still nonzero, find sellers via EvaluateStockNeeds:
        if (req != 0) {
            i32 itemHi = (i32)((u32)strd32(v34, kST_key) >> 16);

            // Call EvaluateStockNeeds for this item type:
            int sellerCount = 0;
            i32 sellerBuilding[kMaxStockSellers] = {};
            i32 sellerHasObj [kMaxStockSellers] = {};
            i32 sellerDeficit[kMaxStockSellers] = {};

            if (g_meisterEquipLeaves.evaluateStockNeeds) {
                g_meisterEquipLeaves.evaluateStockNeeds(
                    (i16)itemHi, &sellerCount,
                    sellerBuilding, sellerHasObj, sellerDeficit);
            }
            // On null hook: sellerCount = 0.

            // 'if (dword_13CE278 > 0) { v10=0; v11=24*dword_13CE278; do { if (B56D8[v10/4] && B56DC[v10/4]>=1) B54458[v29]=B56D0[v10/4]; v10+=24; } while v10<v11 }'
            // Selects the best seller (last one that has object AND deficit >= 1) -> store in B54458 (sourceBuilding).
            for (int k = 0; k < sellerCount; ++k) {
                if (sellerHasObj[k] != 0 && sellerDeficit[k] >= 1) {
                    stwr32(v34, kST_srcBldg, sellerBuilding[k]);  // B54458 = building ptr
                }
            }
        }

        // 'if (!B54458[v29]) B54464[v29] = 0'
        // If no seller was assigned, clear required:
        if (strd32(v34, kST_srcBldg) == 0)
            stwr32(v34, kST_required, 0);
    }

    // --- Phase 3: Budget clamp for FA list items ---
    // 'v12=0; v39=0; if (word_B56FAC[0]) { ... } '
    // For each FA-list item, find matching stock rows with required>0 and
    // reduce qty while (budget < qty*price + accumCost).
    int v39 = 0;  // running cost accumulator (v39 in orig)
    if (stlA_valid(0)) {
        int v12 = 0;  // FA list iteration index
        int v33 = g_stockRowCount << 6;
        do {
            if (g_stockRowCount > 0) {
                i32 listHi = (u16)g_sceneTypeListA[v12];
                for (int si = 0; si < g_stockRowCount; ++si) {
                    i32 stHi = (i32)((u32)strd32(si, kST_key) >> 16);
                    if (stHi == listHi) {
                        i32 req = strd32(si, kST_required);
                        if (req != 0) {
                            float unitPrice = stflt(si, kST_price);
                            // 'while (B54464 > 0 && (double)a2 < (double)B54464*flt_B5446C + (double)v39) --B54464'
                            while (req > 0 && (double)budget < (double)req * (double)unitPrice + (double)v39)
                                --req;
                            stwr32(si, kST_required, req);
                            // 'v22 = (double)B54464*flt_B5446C + (double)v39; ConvertX(); v39 = (int)v22'
                            double v22 = (double)req * (double)unitPrice + (double)v39;
                            v39 = (int)v22;  // ConvertX = trunc
                        }
                    }
                }
            }
        } while (stlA_valid(++v12));
    }

    // --- Phase 4: Budget clamp for FC list items ---
    if (stlB_valid(0)) {
        int v24 = 0;
        int v32 = g_stockRowCount << 6;
        (void)v32;
        do {
            if (g_stockRowCount > 0) {
                i32 listHi = (u16)g_sceneTypeListB[v24];
                for (int si = 0; si < g_stockRowCount; ++si) {
                    i32 stHi = (i32)((u32)strd32(si, kST_key) >> 16);
                    if (stHi == listHi) {
                        i32 req = strd32(si, kST_required);
                        if (req != 0) {
                            float unitPrice = stflt(si, kST_price);
                            while (req > 0 && (double)budget < (double)req * (double)unitPrice + (double)v39)
                                --req;
                            stwr32(si, kST_required, req);
                            double v27 = (double)req * (double)unitPrice + (double)v39;
                            v39 = (int)v27;  // ConvertX
                        }
                    }
                }
            }
        } while (stlB_valid(++v24));
    }
}

// ===========================================================================
// Static helper: call VIBE_MeisterAi_EvaluateStockNeeds and copy results
// into local seller arrays.
// ===========================================================================
static void evalStockNeeds(i16 itemId, int* cnt, i32 bldg[], i32 hasObj[], i32 deficit[]) {
    *cnt = 0;
    if (g_meisterEquipLeaves.evaluateStockNeeds)
        g_meisterEquipLeaves.evaluateStockNeeds(itemId, cnt, bldg, hasObj, deficit);
}

// ===========================================================================
// 0x45bd68  VIBE_MeisterAi_ReserveWorkstationItems  (__usercall eax=a1, edx=a2)
//
// Walk the 4 input slots of a workstation order record (a2, an i16* pointing at
// a workstation row base), and for each slot with a valid stock-table back-index
// (v5[1] >= 0): cap the stock-row's required field to the order's max (a2[18]),
// set the "reserve" flag bit 0x02 in the stock bits, and handle the recursive
// sub-order case (back-index != -1 means the input is itself a produced item —
// recurse). Also handles the food-type slots (452/453/454/449/450/451) with a
// capacity-check.
//
// a1 = meisterRec, a2 = order rec ptr (i16* into workstation row).
// Returns 1 always (orig int return; used as success flag by callers).
// gilde.exe 0x45bd68
// ===========================================================================
void MeisterReserveWorkstationItems(u8* meisterRec, u8* order) {
    i16* a2 = reinterpret_cast<i16*>(order);
    i16* v22 = a2;

    // Walk 4 input slots: 'for (i=0; i!=8; i+=2)' (4 iterations, i16 pairs)
    for (int i = 0; i != 8; i += 2) {
        // 'v4 = *((_DWORD*)v22+1)' = dword at v22+4 (i.e. *(a2+i*sizeof(i16) + 4) as dword)
        // v22 starts at a2, advances by 2 words (4 bytes = 1 dword) per iteration.
        // So v4 = v22[2] as dword (the "back-index" = which stock-row this input uses).
        i32* v22dw = reinterpret_cast<i32*>(v22);
        i32 v4 = v22dw[1];  // *((_DWORD*)v22 + 1)

        if (v4 >= 0) {
            // 'v5 = (__int16*)&dword_B5444E[16*v4] + 1'
            // = stock row v4, starting at byte +2 (the "id" field).
            // '&dword_B5444E[16*v4]' = st(v4), then +1 as __int16* = +2 bytes.
            u8*  stockRow = st(v4);  // row v4 of stock table
            i16* v5 = reinterpret_cast<i16*>(stockRow + 2);  // +2 offset

            // 'v6 = *((_DWORD*)v5+12)' = dword at v5+48 = stockRow + 2 + 48 = stockRow + 50
            // = kST_flags50 (offset 50 in stock row):
            i32 v6 = rd32(stockRow, kST_flags50);  // = B54480[v4]

            // 'if (v6 >= *((_DWORD*)a2+18))' -> cap v6 to a2[18] (max required):
            // '*((_DWORD*)a2+18)' = a2 as dword* +18 = a2 + 72 bytes. In __int16* terms: a2[36] as dword.
            i32* a2dw = reinterpret_cast<i32*>(a2);
            i32 maxReq = a2dw[18];  // *(a2+72) as dword
            if (v6 >= maxReq)
                v6 = maxReq;

            // '*((_DWORD*)v5+12) = v6' -> write back flags50:
            wr32(stockRow, kST_flags50, v6);

            // 'v7 = *v5' = first word of v5 = stock row byte +2 = the item type word:
            i16 v7 = *v5;  // stock row +2 = item id word

            // '*((_BYTE*)v5+60) |= 2u' = byte at stockRow+2+60 = stockRow+62 = kST_bits lo byte:
            u8 bitLo = rd8(stockRow, kST_bits) | 2u;
            wr8(stockRow, kST_bits, bitLo);

            // Check for food-slot types: 452/453/454/449/450/451
            bool isFoodSlot = (v7 == 452 || v7 == 453 || v7 == 454 ||
                               v7 == 449 || v7 == 450 || v7 == 451);

            if (isFoodSlot) {
                // '4*(B564A0[22*v5[1]] + B564A4[22*v5[1]]) < B564A8[22*v5[1]]'
                // v5[1] = *(v5+2) as i16 = stock row byte 4 = backIndex (kST_backIdx lo-word).
                // Wait: v5 = stockRow+2 as i16*, so v5[1] = *(v5+1) = *(stockRow+4) as i16.
                // But the decompile uses '*((_DWORD*)v5+1)' which = dword at v5+4 = stockRow+6 = kST_backIdx.
                // For the food check: '*((_DWORD*)v5+1)' = backIndex dword.
                i32 woBackIdx = rd32(stockRow, kST_backIdx);  // v5[+4] as dword

                if (woBackIdx >= 0 && woBackIdx < kMaxWorkOrders) {
                    // Check workstation capacity for food:
                    // '4*(B564A0[22*woBackIdx] + B564A4[22*woBackIdx]) < B564A8[22*woBackIdx]'
                    // = 4*(incoming + stock) < freeCap
                    i32 incoming = word32(woBackIdx, kWO_incoming);
                    i32 stock    = word32(woBackIdx, kWO_stock);
                    i32 freeCap  = word32(woBackIdx, kWO_freeCap);

                    bool capOk = (4 * (incoming + stock) < freeCap);

                    // 'and *(v5+10)+*(v5+9) < 4 * *(u16*)(i + 65**a2 + dword_13CE27C + 38)'
                    // v5+9 as dword = stockRow+2+36 = stockRow+38 = kST_reserved
                    // v5+10 as dword = stockRow+2+40 = stockRow+42 = kST_stock
                    // *(u16*)(i + 65**a2 + dword_13CE27C + 38) = itemTypeDef16(*a2, 38+i)
                    // *a2 = first word of order = order scene type id.
                    i32 res  = rd32(stockRow, kST_reserved);
                    i32 stk  = rd32(stockRow, kST_stock);
                    u16 needWord = itemTypeDef16(*a2, 38 + i);

                    bool needOk = ((res + stk) < (i32)(4 * (u32)needWord));

                    if (capOk && needOk) {
                        // '*((_BYTE*)v5+60) |= 0x10u' -> set bit 4 of bits:
                        u8 lb = rd8(stockRow, kST_bits) | 0x10u;
                        wr8(stockRow, kST_bits, lb);
                    }
                }
            } else {
                // Non-food slot: '*((_DWORD*)v5+1)' = backIndex:
                i32 v9 = rd32(stockRow, kST_backIdx);  // v5[1] as dword

                if (v9 == -1) {
                    // Item not produced by a workstation (raw material):
                    // Check if need > (res+stk) and capacity available:
                    // '*(v5+10)+*(v5+9) < 4**(u16*)(i + dword_13CE27C + 65**a2 + 38)'
                    i32 res  = rd32(stockRow, kST_reserved);
                    i32 stk  = rd32(stockRow, kST_stock);
                    u16 needWord = itemTypeDef16(*a2, 38 + i);

                    if ((res + stk) < (i32)(4 * (u32)needWord)) {
                        // EvaluateStockNeeds to find sellers:
                        // 'VIBE_MeisterAi_EvaluateStockNeeds(*v5)'
                        // *v5 = stock row byte +2 = item id word
                        i16 itemId = *v5;
                        int sellerCount = 0;
                        i32 sellerBldg[kMaxStockSellers]  = {};
                        i32 sellerHasObj[kMaxStockSellers] = {};
                        i32 sellerDeficit[kMaxStockSellers]= {};
                        evalStockNeeds(itemId, &sellerCount, sellerBldg, sellerHasObj, sellerDeficit);

                        // 'v27=0; if (dword_13CE278>0) { v12=0; do { ... } while v27<count }'
                        i32 ownBldgRecP = rd32(meisterRec, kM_bldgRec);
                        for (int k = 0; k < sellerCount; ++k) {
                            u8* bldgRec = resolveHandle(sellerBldg[k]); // handle column
                            // 'v13 = dword_12CD6D0[v12]' = building ptr
                            // 'if (v13 != *(a1+364) && MapTypeToCategory(*v13)!=2 && B56D8[k] && need<=B56DC[k])'
                            if (bldgRec == nullptr) continue;
                            if (sellerBldg[k] == ownBldgRecP) continue;

                            // MapTypeToCategory(*bldgRec) != 2:
                            int cat = 0;
                            if (g_meisterEquipLeaves.mapTypeToCategory)
                                cat = g_meisterEquipLeaves.mapTypeToCategory(rd8(bldgRec, kB_typeByte));
                            if (cat == 2) continue;

                            // dword_12CD6D8[k] != 0 (has object):
                            if (sellerHasObj[k] == 0) continue;

                            // '*(u16*)(i+65**a2+13CE27C+38) <= dword_12CD6DC[k]'
                            // SIGNED cmp (disasm 0x45bfcc 'cmp eax,deficit; jg').
                            if ((i32)(u32)needWord > sellerDeficit[k]) continue;

                            // Set sourceBuilding: '*((_DWORD*)v5+2) = dword_12CD6D0[k]'
                            wr32(stockRow, kST_srcBldg, sellerBldg[k]);

                            // Check if same owner as Meister:
                            // 'if (*(u16*)(dword_12CD6D0[k]+39) == *(u16*)(*(a1+364)+39))'
                            u8* ownBldgPtr = resolveHandle(ownBldgRecP);
                            u16 ownOwner  = (ownBldgPtr != nullptr) ? rdu16(ownBldgPtr, kB_owner39) : 0;
                            u16 sellOwner = rdu16(bldgRec, kB_owner39);

                            if (ownOwner == sellOwner) {
                                // '*((_DWORD*)v5+5) = *((_DWORD*)v5+11)'
                                // v5+5*4 = stockRow+2+20 = stockRow+22 = kST_required
                                // v5+11*4 = stockRow+2+44 = stockRow+46 = kST_freeCap
                                i32 fc = rd32(stockRow, kST_freeCap);
                                wr32(stockRow, kST_required, fc);
                            } else {
                                // Cross-owner purchase: compute affordable qty
                                // 'v15 = a2[16]>>1; if (v15<1) v15=1'
                                i32* a2dw2 = reinterpret_cast<i32*>(a2);
                                i32 halfMax = a2dw2[16] >> 1;
                                if (halfMax < 1) halfMax = 1;

                                // 'v16 = halfMax * needWord - (res+stk)'
                                i32 v16 = halfMax * (i32)(u32)needWord - (res + stk);

                                // 'if (v16 <= (v5[11]>>1) - v5[9]) v16 = (v5[11]>>1) - v5[9]'
                                // v5[11] = dword at stockRow+46 = kST_freeCap; v5[9] = kST_reserved
                                i32 fc2  = rd32(stockRow, kST_freeCap);
                                i32 res2 = rd32(stockRow, kST_reserved);
                                i32 halfFc = fc2 >> 1;
                                if (v16 <= halfFc - res2)
                                    v16 = halfFc - res2;

                                // 'if (v5[11] - v5[9] - 1 < v16) v16 = v5[11] - v5[9] - 1'
                                i32 maxRoom = fc2 - res2 - 1;
                                if (maxRoom < v16)
                                    v16 = maxRoom;

                                // Budget-based qty: 'v23 = (float)(a1+440); v24 = 1.0/flt_v5[7]'
                                // v5[7] = dword at stockRow+2+28 = stockRow+30 = kST_price (float)
                                float unitP = stflt(v4, kST_price);
                                float budget_f = (float)rd32(meisterRec, kM_budget);
                                float invPrice = (unitP != 0.0f) ? 1.0f / unitP : 0.0f;
                                float budgetQty = budget_f * invPrice;
                                float v16f = (float)v16;

                                float qty_f = (budgetQty >= (double)v16f) ? v16f : budgetQty;
                                int v20 = (int)qty_f;  // ConvertX = trunc

                                // 'v20 = min(v20, dword_12CD6DC[k])'
                                if (sellerDeficit[k] < v20)
                                    v20 = sellerDeficit[k];

                                // 'if (v20 >= needWord) { if (v20 < v5[5]) v20=v5[5]; v5[5]=v20; }'
                                // v5[5] = dword at stockRow+22 = kST_required
                                if (v20 >= (i32)(u32)needWord) {
                                    i32 curReq = rd32(stockRow, kST_required);
                                    if (v20 < curReq) v20 = curReq;
                                    wr32(stockRow, kST_required, v20);
                                }
                            }
                        }
                    }
                } else {
                    // v9 != -1: input is a produced item (back-index into WO table)
                    // 'if (B564B0[22*v9] >= a2[18]) v10 = a2[18]; else v10 = B564B0[22*v9]'
                    if (v9 < kMaxWorkOrders) {
                        i32 woPlanned = word32(v9, kWO_planned);
                        i32* a2dw3 = reinterpret_cast<i32*>(a2);
                        i32 orderMax = a2dw3[18];
                        i32 v10 = (woPlanned >= orderMax) ? orderMax : woPlanned;

                        // 'dword_B564B0[22**((_DWORD*)v5+1)] = v10'
                        wowr32(v9, kWO_planned, v10);

                        // 'LOBYTE(word_B564BC[44**((_DWORD*)v5+1)]) |= 2u'
                        orWoBitsLo(v9, 2u);

                        // 'v11 = 88 * *((_DWORD*)v5+1)'
                        // '4*(B564A0[v11/4] + B564A4[v11/4]) < B564A8[v11/4]'
                        i32 inc2 = word32(v9, kWO_incoming);
                        i32 stk2 = word32(v9, kWO_stock);
                        i32 fc2  = word32(v9, kWO_freeCap);

                        if (4 * (inc2 + stk2) < fc2) {
                            // 'if (*(u16*)(65*(*(int*)(B56464[v11/4]+2)>>16)+13CE27C+54) < B564A8[v11/4])'
                            // The WO key hi-word at v9: dword at WO+0, hi-word = scene type.
                            i32 woKey = word32(v9, kWO_key);
                            i32 woTypeHi = (i32)((u32)woKey >> 16);
                            // '*(u16*)(65*woTypeHi + 13CE27C + 54)' = itemTypeDef16(woTypeHi, 54)
                            u16 defWord = itemTypeDef16((i16)woTypeHi, 54);
                            if ((u32)defWord < (u32)fc2) {
                                // 'LOBYTE(word_B564BC[v11/2]) |= 8u'
                                orWoBitsLo(v9, 8u);
                            }

                            // Recursive call:
                            // 'VIBE_MeisterAi_ReserveWorkstationItems(a1, (__int16*)&B56468[22**(DWORD*)v5+1])'
                            // B56468[22*v9] = WO row v9, offset 4 (B56468 = B56464+4).
                            u8* subOrder = wo(v9) + kWO_subKey;
                            MeisterReserveWorkstationItems(meisterRec, subOrder);
                        }
                    }
                }
            }
        }

        // 'v22 += 2' (advance i16* by 2 words = 4 bytes = 1 dword)
        v22 += 2;
    }
    // Original returns 1 always; public API is void — the int result is not used by callers.
}

// ===========================================================================
// 0x45ba84  VIBE_MeisterAi_CheckWorkstationCapacity  (__usercall eax=a1, edx=a2, ebx=a3)
//
// Recursive feasibility check: walk the workstation order's input slots (4 slots,
// i=0..6 step 2). For each slot with a valid stock back-index and whose needed
// count (from item-type-def) exceeds the slot's incoming+stock: if topLevel (a3!=0)
// return 0 (infeasible). Otherwise check: has max supply (flags50 != 0)? is no
// stock already reserved? if item type is not 449..454 (special), look up the
// back-index: if -1 (raw material), scan EvaluateStockNeeds sellers; if a produced
// item, recurse. Returns 1 if all slots are satisfiable.
//
// a1 = meisterRec, a2 = order ptr (int*), a3 = topLevel flag (0 or 1).
// gilde.exe 0x45ba84
// ===========================================================================
bool MeisterCheckWorkstationCapacity(u8* meisterRec, u8* order, int mode) {
    int* a2 = reinterpret_cast<int*>(order);
    int* v19 = a2;
    int v20 = 0;  // i = slot byte index (0, 2, 4, 6)

    while (true) {
        // 'v5 = v19[1]' = second dword of current v19 = the stock back-index
        int v5 = v19[1];

        if (v5 >= 0) {
            // Stock row back-index is valid.
            // 'v6 = (char*)&dword_B5444E[16*v5] + 2' = st(v5) + 2
            u8* stockRow = st(v5);
            i8* v6 = reinterpret_cast<i8*>(stockRow + 2);

            // '*(u16*)(v20 + 13CE27C + 65**(__int16*)a2 + 38) > *((int*)v6+10)'
            // v20 = slot byte index; *(__int16*)a2 = first word of order = order type id
            // itemTypeDef16(orderTypeId, 38+v20) = needed count for this slot
            // *(int*)(v6+40) = stock row +2+40 = stock row +42 = kST_stock (B54478)
            i16 orderTypeId = *reinterpret_cast<i16*>(a2);
            u16 needCount = itemTypeDef16(orderTypeId, 38 + v20);
            i32 curStock = rd32(stockRow, kST_stock);  // v6+40 = +42 in stockRow

            // SIGNED compare (disasm 0x45bace 'cmp eax,[ebx+28h]; jle'): the u16 need
            // is zero-extended to int and compared signed against the (signed) stock.
            if ((i32)(u32)needCount > curStock) {
                // Need exceeds current stock.
                if (mode != 0) {
                    // 'if (a3) return 0'
                    return false;
                }

                // 'v7 = *((_DWORD*)v6+11)' = dword at v6+44 = stockRow+46 = kST_freeCap
                i32 v7 = rd32(stockRow, kST_freeCap);
                if (v7 == 0) return false;

                // '!*((_DWORD*)v6+9)' = dword at v6+36 = stockRow+38 = kST_reserved
                i32 resv = rd32(stockRow, kST_reserved);
                if (resv != 0) {
                    goto next_slot;
                }

                {
                    // Check item type (not food/special):
                    // 'v9 = *(_WORD*)v6' = word at v6 = stockRow+2 = item id
                    i16 v9 = *reinterpret_cast<i16*>(v6);
                    bool isFood = (v9 == 452 || v9 == 453 || v9 == 454 ||
                                   v9 == 449 || v9 == 450 || v9 == 451);

                    if (!isFood) {
                        // 'v10 = *((_DWORD*)v6+1)' = dword at v6+4 = stockRow+6 = kST_backIdx
                        i32 v10 = rd32(stockRow, kST_backIdx);

                        if (v10 == -1) {
                            // Raw material: call EvaluateStockNeeds and scan sellers.
                            // 'VIBE_MeisterAi_EvaluateStockNeeds(v9)'
                            int sellerCount = 0;
                            i32 sellerBldg[kMaxStockSellers]   = {};
                            i32 sellerHasObj[kMaxStockSellers]  = {};
                            i32 sellerDeficit[kMaxStockSellers] = {};
                            evalStockNeeds(v9, &sellerCount, sellerBldg, sellerHasObj, sellerDeficit);

                            // 'v27=0; v26=0; if (dword_13CE278>0) { ... }'
                            bool v27 = false;
                            i32 ownBldgRec = rd32(meisterRec, kM_bldgRec);
                            u8* ownBldgPtrL = resolveHandle(ownBldgRec);
                            u16 ownOwner = ownBldgPtrL ? rdu16(ownBldgPtrL, kB_owner39) : 0;

                            for (int k = 0; k < sellerCount && !v27; ++k) {
                                u8* bldgRec = resolveHandle(sellerBldg[k]);
                                if (bldgRec == nullptr) continue;

                                // 'v13 != *(a1+364) && MapTypeToCategory(*v13)!=2 && B56D8[k] && needWord<=B56DC[k]'
                                if (sellerBldg[k] == ownBldgRec) continue;

                                int cat = 0;
                                if (g_meisterEquipLeaves.mapTypeToCategory)
                                    cat = g_meisterEquipLeaves.mapTypeToCategory(rd8(bldgRec, kB_typeByte));
                                if (cat == 2) continue;

                                if (sellerHasObj[k] == 0) continue;

                                // CRITICAL (disasm 0x45bbb7 'add eax,[esp+var_38]'): the need-word
                                // offset INSIDE the seller loop is v12 (the per-seller index,
                                // 0,2,4,... = 2*k), NOT the outer slot index v20. The decompile
                                // reads consecutive type-def words per seller.
                                const int v12 = 2 * k;
                                // 'needWord <= dword_12CD6DC[k]'  (SIGNED cmp: 0x45bbc4 jg)
                                u16 needW = itemTypeDef16(orderTypeId, 38 + v12);
                                if ((i32)(u32)needW > sellerDeficit[k]) continue;

                                // 'if (ownOwner == *(u16*)(bldgRec+39)) goto LABEL_25'
                                u16 sellOwner = rdu16(bldgRec, kB_owner39);
                                if (ownOwner == sellOwner) {
                                    v27 = true;
                                    break;
                                }

                                // Compute affordable qty and check:
                                i32* a2p = reinterpret_cast<i32*>(a2);
                                i32 halfMax = a2p[16] >> 1;
                                if (halfMax < 1) halfMax = 1;

                                // 'v14 = needW * halfMax - (res+stk)'
                                // Using v20 as slot index:
                                i32 res2  = rd32(stockRow, kST_reserved);
                                i32 stk2  = rd32(stockRow, kST_stock);
                                i32 v14   = (i32)(u32)needW * halfMax - (res2 + stk2);

                                // Clamp to freeCap range:
                                i32 fc    = rd32(stockRow, kST_freeCap);
                                i32 halfFc = fc >> 1;
                                if (v14 <= halfFc - res2)
                                    v14 = halfFc - res2;
                                if (v14 > fc - res2 - 1)
                                    v14 = fc - res2 - 1;

                                // Budget-based clamp:
                                float unitP = stflt(v5, kST_price);
                                float bgt_f = (float)rd32(meisterRec, kM_budget);
                                float invP  = (unitP != 0.0f) ? 1.0f / unitP : 0.0f;
                                float bgtQty = bgt_f * invP;
                                float v14f   = (float)v14;
                                float qty_f  = (bgtQty >= (double)v14f) ? v14f : bgtQty;
                                int v25out   = (int)qty_f;  // ConvertX

                                // 'v25 = min(v25, sellerDeficit[k])'
                                if (sellerDeficit[k] < v25out)
                                    v25out = sellerDeficit[k];

                                // 'if (needW <= v25) goto LABEL_25 (v27=1)'  (SIGNED: 0x45bd3f jg)
                                if ((i32)(u32)needW <= v25out) {
                                    v27 = true;
                                }
                            }

                            if (!v27) return false;

                            // LABEL_7 continues (fell through v27=true):
                            goto next_slot;
                        }

                        // v10 != -1: produced item — recurse:
                        // 'if (!CheckWorkstationCapacity(a1, &B56468[22*v10], 0)) return 0'
                        if (v10 >= 0 && v10 < kMaxWorkOrders) {
                            u8* subOrder = wo(v10) + kWO_subKey;
                            if (!MeisterCheckWorkstationCapacity(meisterRec, subOrder, 0))
                                return false;
                        }
                    }
                    // food/special slot: fall through to next_slot
                }
            }
        }

next_slot:
        // '++v19; v20 += 2; if (v20 >= 8) return 1'
        ++v19;
        v20 += 2;
        if (v20 >= 8)
            return true;
    }
}

} // namespace guild::sim
