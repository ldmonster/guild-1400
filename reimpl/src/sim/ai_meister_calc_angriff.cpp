// ===========================================================================
// VIBE_Ai_CalcAngriff @0x4569a8 — attack/spy decision calculator.
//
// gilde.exe 0x4569a8 — __usercall, eax=a1(meisterRec), edx=a2(attackBudget),
//                      ebp=a3(scratchPtr/bestTarget). Returns int (1=decision, 0=fall-through).
//
// This is the shared "attack or spy" planner invoked by CalcMeisterWache,
// CalcMeisterDiebe, and CalcMeisterAmbush. It:
//   1. Validates (and may clear) the cached target building id at mr+448.
//   2. Scans the 8x8 city-tile danger grid to find the max danger score.
//   3. Sweeps the 256-slot object array (g_objects, 169-byte stride) for the
//      best attack/spy target, scoring by tile danger, building security, and RNG.
//   4. Checks if j (a QueryFind result for type-202 scene node under the Meister's
//      own building matching the chosen target) exists, and whether its +55 byte
//      (strength/flag) meets the attack threshold (>= RandomModulo(0x32)+50).
//   5. If attack threshold met AND attackBudget >= 3: builds an attack command
//      (cmdType=73) with a worker-id list. Emits only if >= 2 workers found
//      (v45 != -1 guard), after checking no existing attack/spy handler for target.
//   6. If attack threshold NOT met: builds a spy command (cmdType=64) with a
//      worker-id list. Always emits regardless of worker count.
//
// Exact scoring formula (0x456a49..0x456c84):
//   tileScore = dangerA[row][col] * 0.125f + dangerB[row][col]
//   maxDanger = max(tileScore) over 8x8 grid (= v59/v53)
//   if maxDanger == 0.0: maxDanger = 1.0  (LODWORD zero-test guard)
//   normScore  = 1.0 - (dangerB + dangerA * 0.125) / maxDanger
//   secScore   = (8.0f - securityLevel) * 0.5 * normScore
//   rng0       = RandomModulo(0x64)    [u16, cast to double]
//   finalScore = rng0 * 0.01 * secScore
//   winner: first object OR (assetWorth(new)*finalScore > assetWorth(best)*bestScore)
//
// Object sweep filter (0x456b1a..0x456ca3):
//   slot alive, type in {19, 4, 16} (decompile checks *v9 == 19 || v11 == 4 || v11 == 16),
//   owner word (+39) != Meister's own building owner word AND != 0xFFFF
//   AND != *(u16*)dword_6498E4 (the local-player city word),
//   flag byte at obj[90] bit0 == 0,
//   VIBE_CharAction_IsAnimalTargetBusy(v7) returns nonzero.
//
// Command structure (248-byte on stack, initialised by Light_SetGrayColorThunk):
//   +0x04  cmdType byte   (73=attack, 64=spy)
//   +0x08  actorId dword  (dword_12CE914[134 * meisterOwnerWord])
//   +0x0C  buildingId     (meister's own building +1)
//   +0x28  gametime qword (copy of qword_13CE852)
//   +0x2C  timeExtra dword
//   +0x2E  timeTail u16   (unk_13CE85E)
//   +0x32  mode byte 1
//   +0x34  workerIds (up to 8 dwords, terminated with -1 up to +0x53)
//   +0x58  targetBuildingId (a3+1)
//   +0x5C  srcBuildingId    (meister's own building +1)
//
// Emitted via g_meisterCmdSink->push(). Sprintf_0 calls are no-ops (dropped).
// The local-player city word (dword_6498E4) is modelled as g_angriffLocalPlayerWord
// (file-static, wired by bridge or test). See SPEC section "dword_6498E4".
// ===========================================================================
#include "sim/ai_meister.h"
#include "sim/ai_meister_internal.h"

#include "sim/entity.h"            // g_objects, g_personIds, g_persons, kPersonCapacity
#include "util/math_random.h"      // guild::util::RandomModulo

#include <cstring>

namespace guild::sim {

// ---------------------------------------------------------------------------
// gilde.exe dword_6498E4 — the "local player record" pointer. The filter reads
// *(u16*)dword_6498E4 (the first word of the pointed-to record, which is the
// owner/faction city word) to exclude the local player's own buildings from the
// target sweep. In the reimpl we expose this as a u16 set by the live bridge
// (or test). Zero (the zero-init default) acts as an impossible city word and
// is safe (it means "exclude city 0" which is never a valid faction word).
// ---------------------------------------------------------------------------
static u16 g_angriffLocalPlayerWord = 0;  // *(u16*)dword_6498E4 at scan time

// Public bridge setter so the live engine can install the real city word.
// (Not declared in the public header — only the bridge cpp needs it via
//  a companion header or extern call; tests set it directly via the internal
//  test accessor below.)
void CalcAngriff_SetLocalPlayerWord(u16 w) { g_angriffLocalPlayerWord = w; }
u16  CalcAngriff_GetLocalPlayerWord()      { return g_angriffLocalPlayerWord; }

// ---------------------------------------------------------------------------
// CalcAngriff — 1:1 reconstruction of VIBE_Ai_CalcAngriff @0x4569a8.
//
// Parameters (matching the __usercall convention):
//   meisterRec   = a1@eax : raw base of the Meister's 536-byte person record.
//   attackBudget = a2@edx : the remaining-action budget / available staff count.
// Returns: 1 = decision was reached (caller should stop); 0 = no target / fall-through.
//
// Record byte offsets are named kM_* / kB_* from ai_meister_internal.h.
// ---------------------------------------------------------------------------
int CalcAngriff(u8* meisterRec, int attackBudget) {
    using namespace guild::util;   // RandomModulo
    using namespace guild::sim::aimei;

    // v55 = a1 (meisterRec); v48 = a2 (attackBudget). v51 = 1 (result).
    // gilde.exe 0x4569b3, 0x4569ba, 0x4569d4.
    u8* mr     = meisterRec;  // v55
    int budget = attackBudget; // v48
    // v51 = 1 (default return "decision reached").
    // Note: v51 is only set to 0 at the "not enough budget" and "type mismatch" exits.

    // Sprintf_0 at 0x4569db: debug log "ai_CalcAngriff(): Meister %s ..." — dropped
    // (no sim side effect; SPEC §"VIBE_Crt_Sprintf_0 → DROP").

    // -----------------------------------------------------------------------
    // Target-validation block: if mr+448 != -1, verify the cached target
    // building still belongs to the same owner as the Meister's own building.
    // gilde.exe 0x4569f7..0x456a0b (entry), 0x456f7d..0x456fcd (BuildingFindById path).
    // -----------------------------------------------------------------------
    u8* bestTarget = nullptr; // a3 in the decompile; the best-target object rec.

    if (rd32(mr, kM_target) != -1) {
        // 0x456f7d: v25 = VIBE_Building_FindById(*(mr+448))
        i32 cachedTargetId = rd32(mr, kM_target);
        // Null hook → null (building not found → clear target, goto LABEL_3).
        u8* foundRec = nullptr;
        if (g_meisterLeaves && g_meisterLeaves->buildingFindById)
            foundRec = g_meisterLeaves->buildingFindById(cachedTargetId);

        // 0x456f82: a3 = (char*)v25  — the found rec IS bestTarget here!
        bestTarget = foundRec; // a3 = v25 (possibly null)

        if (foundRec) {
            // 0x456fc0: compare owner words.
            u8* mb = rdptr(mr, kM_bldgRec); // *(mr+364)
            u16 foundOwner   = rdu16(foundRec, kB_owner39); // *(u16*)(foundRec+39)
            u16 meisterOwner = mb ? rdu16(mb, kB_owner39) : 0u;
            if (foundOwner != meisterOwner) {
                // Different owner: goto LABEL_2 with bestTarget=foundRec, target still !=-1.
                // At LABEL_2: target != -1 → goto LABEL_26 (fast-path, skip scan).
                goto LABEL_2;
            }
            // 0x456fcd: same owner → clear target; fall to LABEL_3 (scan, a3 reset to 0 there)
            wr32(mr, kM_target, -1);
        } else {
            // 0x456f8f: building not found → clear target; bestTarget=null
            wr32(mr, kM_target, -1);
        }
        goto LABEL_3; // fall through to the grid scan (resets a3/bestTarget to 0)
    }

LABEL_2:
    // 0x456a0b: if (*(mr+448) != -1) goto LABEL_26 else fall to LABEL_3.
    if (rd32(mr, kM_target) != -1)
        goto LABEL_26;

LABEL_3: {
    // -----------------------------------------------------------------------
    // 8x8 city-tile danger grid scan.
    // Computes maxDanger = max over all 64 tiles of (dangerA * 0.125 + dangerB).
    // gilde.exe 0x456a11..0x456af5.
    //
    // Original: nested loops. Outer iterates rows (i=0..7); inner iterates cols.
    // The outer loop index `i` drives the starting byte-offset `v4 = 24*i` and
    // then the inner `do { v4 += 192; } while (v4 != v3)` advances by one row-stride
    // per col scan. This is equivalent to: for each column, read the tile at (row=i, col).
    // Actually: v4 = 24*i  selects column-offset within that outer "i" row.
    // Then the inner loop adds 192 each iteration, cycling through all 8 rows for
    // that column. v3 starts at 1536 and advances by 24 each outer iteration.
    //
    // Outer loop i=0..7 (columns); inner iterates rows 0..7 for column i.
    // -----------------------------------------------------------------------
    float v59 = 0.0f; // running max danger (starts 0)
    float v53 = 0.0f; // last computed tile score (used after loops for zero test)
    {
        int v3 = 1536; // terminator: 192*8 = 1536
        for (int i = 0; i < 8; ++i) {
            // v49 = 0 (not used externally), v4 = 24*i
            unsigned int v4 = static_cast<unsigned int>(24 * i);
            do {
                // 0x456a49: v60 = word_12349A2[v4/2]  (dangerB word)
                // 0x456a60: v60 = word_12349A0[v4/2]  (dangerA word, stored to v60 reuse)
                // Note: the decompile reads v60 twice; first into v57(double), then overwrites v60.
                u16 dangerB = 0, dangerA = 0;
                std::memcpy(&dangerB, g_cityTileGrid + v4 + 2, 2); // word_12349A2 = base+2
                std::memcpy(&dangerA, g_cityTileGrid + v4 + 0, 2); // word_12349A0 = base+0
                double v5  = static_cast<double>(dangerB);           // v5 (st7)
                double v57 = v5;                                      // v57 = (double)dangerB
                // 0x456a82: v58 = (double)dangerA * flt_619528 + v57
                //           = dangerA * 0.125 + dangerB   (verified: 0x3e000000 = 0.125f)
                float v58 = static_cast<float>(static_cast<double>(dangerA) *
                                               static_cast<double>(kTileBWeight) + v57);
                // 0x456a9a: v6 = (v59 <= (double)v58) ? v58 : v59
                float v6;
                if (static_cast<double>(v59) <= static_cast<double>(v58))
                    v6 = v58;   // 0x456fdc branch
                else
                    v6 = v59;   // 0x456aa0 branch
                v53 = v6;
                v4  += 192; // 0x456ab5: next row for same column
                v59  = v6;  // 0x456abb: update running max
            } while (v4 != static_cast<unsigned int>(v3)); // 0x456ac4
            // 0x456ad1: v49 = 8 (unused after loop)
            v3 += 24; // 0x456ad9: advance column terminator
        }
    }

    // 0x456af6: if ((LODWORD(v53) & 0x7FFFFFFF) == 0) v59 = 1.0
    // Checks if v53 (the max danger score) is ±0.0 (sign-bit clear or set, mantissa zero).
    {
        u32 lodword;
        std::memcpy(&lodword, &v53, 4);
        if ((lodword & 0x7FFFFFFFu) == 0)
            v59 = 1.0f; // 0x456fe8
    }

    // -----------------------------------------------------------------------
    // Object array sweep — find the best attack/spy target.
    // gilde.exe 0x456afc..0x456cb0 (256-slot scan, 169-byte stride).
    //
    // a3 (bestTarget) = 0; v8 = slot index; v63/v62 = includeBuildings flags.
    // -----------------------------------------------------------------------
    bestTarget = nullptr;
    int v8    = 0;
    u8  v63   = 0; // includeBuildings flag for the best candidate
    u8  v62   = 0; // includeBuildings flag for the current comparison candidate
    // v54 = best score; v52 = temp comparison score (float locals)
    float v54 = 0.0f;
    // v7 = cursor over object array at g_objectArrayBase (169 stride)
    u8* v7 = aimei::g_objectArrayBase; // = reinterpret_cast<u8*>(&g_objects[0])

    // Meister's own building base and owner word (read once for the loop).
    u8* meisterBldg   = rdptr(mr, kM_bldgRec); // *(mr+364)
    u16 meisterOwnerW = meisterBldg ? rdu16(meisterBldg, kB_owner39) : 0u;

    do {
        // 0x456b1a: if (*v7) — alive byte nonzero
        if (v7[0]) {
            // 0x456b44: v9 = 589 * *v7 + dword_13CE294  (type def record)
            u8* v9 = aimei::g_buildingTypeDefBase
                   ? aimei::g_buildingTypeDefBase + 589u * v7[0]
                   : nullptr;
            // 0x456b4e: v10 = *(u16*)(v7+39)  object owner word
            u16 v10 = rdu16(v7, kB_owner39); // unsigned __int16 v10

            // 0x456b7f: combined filter:
            //   (u16)v10 != meisterOwnerW
            //   && v10 != 0xFFFF
            //   && (v7[90] & 1) == 0
            //   && v10 != *(u16*)dword_6498E4
            if (v10 != meisterOwnerW
             && v10 != 0xFFFFu
             && (v7[90] & 1u) == 0
             && v10 != g_angriffLocalPlayerWord)
            {
                // 0x456b85: v11 = *v9; type code from AiPlayer def
                u8 v11 = v9 ? v9[0] : 0;
                // 0x456b92..0x457004: if type is 19 or 4 or 16
                if (v11 == 19 || v11 == 4 || v11 == 16) {
                    // 0x456b92: VIBE_CharAction_IsAnimalTargetBusy(v7)
                    // When hook is null, treat as "not busy" (null-path = skip object),
                    // matching the original's false-return → no candidate.
                    bool busy = false;
                    if (g_meisterLeaves && g_meisterLeaves->charActionIsAnimalTargetBusy)
                        busy = (g_meisterLeaves->charActionIsAnimalTargetBusy(v7) != 0);

                    if (busy) {
                        // 0x456b9f: v12 = *(v7+97)  (scene-node ptr / status dword)
                        i32 v12 = rd32(v7, 97);
                        float v56 = 0.0f; // normalized tile score for this object
                        u8* statusRec = resolveHandle(v12); // +97 holds a record handle
                        if (statusRec && g_meisterLeaves && g_meisterLeaves->worldToCityTile) {
                            // 0x456bbb: VIBE_Coord_WorldToCityTile((float*)(v12+76), &v49, &i)
                            int tRow = 0, tCol = 0;
                            float* pos = reinterpret_cast<float*>(statusRec + 76);
                            if (g_meisterLeaves->worldToCityTile(pos, &tRow, &tCol)) {
                                // 0x456bdb: v13 = 192*v49 + 24*i
                                int v13 = 192 * tRow + 24 * tCol;
                                // 0x456be6: v60 = *(u16*)((char*)word_12349A2 + v13)
                                u16 tDangerB = 0, tDangerA = 0;
                                std::memcpy(&tDangerB, g_cityTileGrid + v13 + 2, 2);
                                std::memcpy(&tDangerA, g_cityTileGrid + v13 + 0, 2);
                                // 0x456c1e: v56 = 1.0 - ((double)tDangerB + (double)tDangerA * 0.125) / v59
                                v56 = static_cast<float>(1.0 - (static_cast<double>(tDangerB)
                                    + static_cast<double>(tDangerA) * static_cast<double>(kTileBWeight))
                                    / static_cast<double>(v59));
                            } else {
                                v56 = 0.0f; // 0x457011 branch
                            }
                        } else if (v12 == 0) {
                            v56 = 0.0f; // 0x457011 null-scene path
                        } else {
                            // worldToCityTile hook absent: behave as "tile not found" → 0
                            v56 = 0.0f;
                        }

                        // 0x456c2c: SecurityLevel = VIBE_Building_GetSecurityLevel(v7)
                        // Null hook → 0 (null-path = zero security level).
                        int secLevel = 0;
                        if (g_meisterLeaves && g_meisterLeaves->securityLevel)
                            secLevel = g_meisterLeaves->securityLevel(v7);

                        // 0x456c58: v56 = (flt_619540 - (double)secLevel) * dbl_619530 * v56
                        //           = (8.0f - secLevel) * 0.5 * v56
                        v56 = static_cast<float>(
                            (static_cast<double>(kSecBase8) - static_cast<double>(secLevel))
                            * kSecScale05 * static_cast<double>(v56));

                        // 0x456c69: SecurityLevel = (u16)VIBE_Math_RandomModulo(0x64)
                        int randVal = static_cast<int>(
                            static_cast<u16>(RandomModulo(0x64u))); // 0..99

                        // 0x456c84: v56 = (double)randVal * dbl_619538 * v56
                        //           = randVal * 0.01 * v56
                        v56 = static_cast<float>(
                            static_cast<double>(randVal) * kRandScale001
                            * static_cast<double>(v56));

                        // 0x457078..0x456c9a: winner selection
                        // if !a3 (no best yet) OR (assetWorth(v7,v63)*v56 > assetWorth(a3,v62)*v54)
                        //   → a3=v7; v54=v56.
                        //
                        // The comparison at 0x45701d:
                        //   SecurityLevel = ComputeAssetWorth(v7, v63)
                        //   v52 = (double)SecurityLevel * v56
                        //   v60  = ComputeAssetWorth(a3, v62)
                        //   if ((double)v60 * v54 < v52) → take new candidate
                        //
                        // NOTE: v63 and v62 are byte flags. In the decompile both start 0
                        // and are never updated (no write found in the scan loop), so they
                        // are always 0 (= includeBuildings=false). This matches the decompile
                        // literal — the comparison always passes includeBuildings=0.
                        if (!bestTarget) {
                            // 0x457078: !a3 → unconditional take (skip asset comparison)
                            bestTarget = v7;
                            v54        = v56;
                        } else {
                            // 0x45701d: compare new candidate vs current best.
                            // Null hook → asset worth = 0 (null-path).
                            int assetNew = (g_meisterLeaves && g_meisterLeaves->computeAssetWorth)
                                         ? g_meisterLeaves->computeAssetWorth(v7, v63) : 0;
                            float v52 = static_cast<float>(
                                static_cast<double>(assetNew) * static_cast<double>(v56));

                            int assetBest = (g_meisterLeaves && g_meisterLeaves->computeAssetWorth)
                                           ? g_meisterLeaves->computeAssetWorth(bestTarget, v62) : 0;
                            // 0x45706e..0x457078: if (v60*v54 < v52) → new winner
                            if (static_cast<double>(assetBest) * static_cast<double>(v54)
                                < static_cast<double>(v52))
                            {
                                bestTarget = v7;
                                v54        = v56;
                            }
                        }
                    }
                }
            }
        }

        ++v8;
        v7 += 169; // 0x456ca4: advance to next object slot
    } while (v8 < 256); // 0x456cb0

    // 0x456cc0: *(mr+448) = *(a3+1)  — store best target building id.
    // This write happens unconditionally (even if bestTarget is null → UB in orig,
    // but we guard: if bestTarget is null, *(a3+1) would fault in the original too;
    // LABEL_26 checks !a3 and returns 0, so the write only matters if bestTarget != null).
    if (bestTarget) {
        i32 bldgId = rd32(bestTarget, kB_id1);
        wr32(mr, kM_target, bldgId);
    } else {
        // bestTarget null: a3+1 dereference in orig would fault; in practice the
        // LABEL_26 check (!a3) catches this. We mimic: write to +448 is the
        // dead-path; we'll hit the !a3 return below.
        // (The original writes before checking, but since bestTarget==null that's UB.)
    }
}

LABEL_26:
    // 0x456cc8: if (!a3) return 0.
    if (!bestTarget)
        return 0; // 0x457083

    // -----------------------------------------------------------------------
    // Type gate: bestTarget must be type 19, 4, or 16.
    // gilde.exe 0x456cf3..0x4570c1.
    // -----------------------------------------------------------------------
    {
        u8* typeDef = aimei::g_buildingTypeDefBase
                    ? aimei::g_buildingTypeDefBase + 589u * bestTarget[0]
                    : nullptr;
        u8 v14 = typeDef ? typeDef[0] : 0u; // *(_BYTE*)(589*(*a3) + dword_13CE294)
        if (v14 != 19 && v14 != 4 && v14 != 16) {
            // 0x4570a8: v51 = 0; return 0.
            return 0;
        }
    }

    // -----------------------------------------------------------------------
    // QueryFind: look for a type-202 scene node under the Meister's own building
    // that matches the target building id (node+21 == bestTarget+1).
    // gilde.exe 0x456d29..0x456d3d.
    //
    // Original: VIBE_GameObject_QueryFind(*(meisterBldg+93), 1, 0, 202)
    //   → filter count=1, filter pair: (op=0, val=202) → type==202 match.
    // Then iterate: if *(j+21) == *(a3+1) break.
    // -----------------------------------------------------------------------
    u8* meisterBldg2 = rdptr(mr, kM_bldgRec);
    i32 sceneRootId  = meisterBldg2 ? rd32(meisterBldg2, kB_sceneRoot93) : -1;
    i32 targetBldgId = rd32(bestTarget, kB_id1); // *(a3+1)

    // j = the found scene node (u8* base), or null.
    u8* j = nullptr;
    {
        // Null hook → j stays null (no QueryFind result → treat as spy path).
        u8* qfResult = nullptr;
        if (g_meisterLeaves && g_meisterLeaves->queryFind) {
            const int filts[2] = {0, 202};
            qfResult = g_meisterLeaves->queryFind(sceneRootId, filts, 1);
        }

        // Iterate to find node where *(node+42) == targetBldgId.
        // gilde.exe 0x456d2b: `mov eax, [edx+2Ah]` → BYTE offset 0x2A = 42.
        // (Hex-Rays declares j as `__int16*`, so its `*(j+21)` is 21*2 = byte 42.
        //  The match dword lives at j+42, NOT j+21.)
        while (qfResult) {
            i32 nodeId = rd32(qfResult, 42); // *(_DWORD*)((char*)j + 0x2A)
            if (nodeId == targetBldgId)
                break; // found match
            // advance
            if (g_meisterLeaves && g_meisterLeaves->queryIterNext)
                qfResult = g_meisterLeaves->queryIterNext();
            else
                qfResult = nullptr;
        }
        j = qfResult;
    }

    // -----------------------------------------------------------------------
    // Attack-vs-spy decision: 0x456d3e..0x456d65
    //   if j != null AND j[55] >= (u16)RandomModulo(0x32) + 50 → attack path
    //   else → spy path
    //
    // NOTE on RNG ordering: the RandomModulo(0x32) draw ALWAYS happens if j != null
    // (even if j[55] < threshold). If j == null, the branch at 0x456d40 jumps to
    // loc_456D65 (spy path) WITHOUT drawing the random. This matches the disasm:
    //   0x456d3a test eax, eax (j == null?)
    //   0x456d3c jnz short loc_456D2B (continue inner loop — not exit)
    //   -- wait, the disasm 0x456d42 mov eax, 32h is at loc_456D42.
    //   0x456d3e: "test edx, edx" where edx = j. If null → jz loc_456D65 (spy).
    //   0x456d47: RandomModulo(0x32) called. Result in eax masked &0xFFFF.
    //   0x456d51: dl = j[55] (unsigned byte); and edx, 0xFF.
    //   0x456d5a: add eax, 50. compare edx(j[55]) >= eax(rng+50).
    //   0x456d5f: jge loc_457158 (attack path) else fall to spy path.
    // -----------------------------------------------------------------------
    bool attackPath = false;
    if (j) {
        // 0x456d42: draw RandomModulo(0x32) = [0,49]
        int rnd2 = static_cast<int>(static_cast<u16>(RandomModulo(0x32u)));
        // 0x456d51: strength byte at j+55 (unsigned)
        u8 strength = j[55];
        // 0x456d5d: if strength >= (rnd2 + 50)
        if (static_cast<int>(strength) >= rnd2 + 50)
            attackPath = true;
    }
    // if j == null → spy path (no rng draw).

    // -----------------------------------------------------------------------
    // He handler list probe: check if a spy/attack action is already running
    // for the target building. Used by BOTH attack and spy paths.
    // The two paths share the same He-search logic; the attack path has an
    // additional budget check before it.
    // -----------------------------------------------------------------------

    // Common He-search helper lambda (inline in orig, repeated for spy and attack).
    // Searches handler lists for cmdType 64, 63, 73 (in that order) and checks
    // *(handler+0xAC) == targetBldgId.
    // Returns the first matching handler base, or null.
    auto heSearch = [&]() -> u8* {
        // Three sequential searches: cmdType 64 (spy), 63 (spy alt), 73 (attack).
        // Each: FindFirstHandlerByFilter(2, 0, type, 3, meisterBldgId) then iterate.
        i32 meisterBldgId = meisterBldg2 ? rd32(meisterBldg2, kB_id1) : -1;
        const int types[3] = {64, 63, 73};
        for (int t = 0; t < 3; ++t) {
            u8* handler = nullptr;
            if (g_meisterLeaves && g_meisterLeaves->heFindFirst) {
                handler = g_meisterLeaves->heFindFirst(2, 0, types[t], 3, meisterBldgId);
            }
            // Iterate to find matching handler (*(handler+0xAC) == targetBldgId)
            while (handler) {
                i32 handlerField = rd32(handler, 0xAC); // *((_DWORD*)k + 43) = offset 172
                if (handlerField == targetBldgId)
                    break; // found match
                u8* next = nullptr;
                if (g_meisterLeaves && g_meisterLeaves->heFindNext)
                    next = g_meisterLeaves->heFindNext();
                handler = next;
            }
            if (handler)
                return handler; // found an existing handler for this target
        }
        return nullptr;
    };

    // -----------------------------------------------------------------------
    // ATTACK PATH (j != null AND strength threshold met).
    // gilde.exe 0x457158..0x457430.
    // -----------------------------------------------------------------------
    if (attackPath) {
        // 0x457158: if (attackBudget < 3) return 0.
        if (budget < 3)
            return 0; // 0x45717d

        // 0x4571a1..0x45724x: He handler check for existing attack/spy on target.
        {
            u8* existing = heSearch();
            if (existing) {
                // 0x457259..0x45742a: "hat aber noch eine Spionage" log → DROP.
                // Return v51 (=1, decision reached / abort caller).
                return 1;
            }
        }

        // 0x457259: Build attack command (cmdType = 73).
        {
            // Command struct fields (see layout comment at top of file).
            MeisterCommand cmd;
            cmd.cmdType = 73; // 0x4572be: v38 = 73 attack

            // 0x45728c: v39 = dword_12CE914[134 * *(u16*)(meisterBldg2+39)]
            // = g_personIds[ meisterOwnerW ]  (meisterOwnerW = *(meisterBldg2+39))
            {
                u16 ownerIdx = meisterBldg2 ? rdu16(meisterBldg2, kB_owner39) : 0u;
                // dword_12CE914[134*i] = g_personIds[i]  (stride 134*4=536=kPersonStride)
                cmd.actorId = (ownerIdx < kPersonCapacity) ? g_personIds[ownerIdx] : 0;
            }

            // 0x4572af: v40 = *(meisterBldg2+1)  (meister's building id)
            cmd.buildingId = meisterBldg2 ? rd32(meisterBldg2, kB_id1) : -1;

            // 0x4572b8..0x4572bb: gametime + trailing fields
            cmd.timePacked = (static_cast<u64>(g_meisterGameTime.day) |
                              (static_cast<u64>(g_meisterGameTime.hour)   << 32) |
                              (static_cast<u64>(g_meisterGameTime.minute) << 48));
            cmd.timeExtra  = g_meisterTimeExtra;
            cmd.timeTail   = g_meisterTimeTail;

            // 0x4572bd: v44 = 1  (mode byte)
            cmd.mode = 1;

            // 0x4572c7: v46 = *(a3+1)  (target building id)
            cmd.targetId = targetBldgId;

            // 0x4572e5: v47 = *(meisterBldg2+1)  (src = meister's own building)
            cmd.srcId = meisterBldg2 ? rd32(meisterBldg2, kB_id1) : -1;

            // 0x4572ef: cap workers at min(attackBudget, 8)
            int v28 = (budget >= 8) ? 8 : budget;
            int v29 = v28; // cap

            // 0x4572f9..0x4573d0: worker collection loop (over person array, 536 stride)
            // Collects up to v29 workers from g_persons where:
            //   word_12CE910[v32/2] != -1     (slot alive, marker != -1)
            //   && dword_12CEA7C[v32/4] == *(mr+364)  (person's employer == meister's building)
            //   && byte_12CEA75[v32]           (profession byte nonzero)
            //   && !dword_12CEA8C[v32/4]       (not jail/busy)
            //   && dword_12CEA94[v32/4]        (has action object)
            //   && *(dword_12CEA94[v32/4] + 44) == *(dword_12CEA7C[v32/4] + 1)
            //     (action object's +44 == employer building's id)
            //
            // v32 = byte offset into person columns (stride 536 = kPersonStride).
            // 411648 = 536 * 768 = kPersonStride * kPersonCapacity.
            {
                // 0x4572f9: v30=count, v31=same, byte-offset v33 tracks buffer position.
                int v30 = 0; // workers found
                int v31 = 0; // same (drives the -1 pad loop start)
                i32 meisterBldgRecPtr = rd32(mr, kM_bldgRec); // *(mr+364)

                if (v28 > 0) {
                    for (unsigned int v32 = 0;
                         v30 < v29 && static_cast<int>(v32) < 411648;
                         v32 += static_cast<unsigned int>(kPersonStride))
                    {
                        int pidx = static_cast<int>(v32 / kPersonStride);
                        u8* personBase = reinterpret_cast<u8*>(&g_persons[pidx]);
                        // word_12CE910[v32/2] != -1: marker word != -1 (little-endian i16)
                        if (personBase[0] == 0xFF && personBase[1] == 0xFF)
                            continue; // marker == -1 → free slot
                        // dword_12CEA7C[v32/4] == *(mr+364): employer ptr == meister's bldgRec
                        i32 employer = rd32(personBase, kP_employer); // +0x16C
                        if (employer != meisterBldgRecPtr)
                            continue;
                        // byte_12CEA75[v32]: profession byte != 0
                        if (!rd8(personBase, kP_profByte)) // +0x165
                            continue;
                        // dword_12CEA8C[v32/4] == 0: not busy/jailed
                        if (rd32(personBase, kP_busy)) // +0x17C
                            continue;
                        // dword_12CEA94[v32/4]: action object ptr != 0
                        i32 v34 = rd32(personBase, kP_actionObj); // +0x184
                        if (!v34)
                            continue;
                        // *(v34+44) == *(dword_12CEA7C[v32/4]+1): action obj building id == employer+1
                        // v34/employer are handle columns -> resolve to record bases.
                        u8* aoRec  = resolveHandle(v34);
                        u8* empRec = resolveHandle(employer);
                        if (!aoRec || !empRec)
                            continue;
                        i32 actionBldgId = rd32(aoRec, 44);
                        i32 empBldgId    = rd32(empRec, kB_id1);
                        if (actionBldgId != empBldgId)
                            continue;

                        // Qualify: worker added to command worker list.
                        ++v31;
                        ++v30;
                        cmd.workerIds.push_back(g_personIds[pidx]); // dword_12CE914[v32/4]
                    }
                }

                // 0x457372: v45 guard. Stack layout (disasm-verified):
                //   &v43 = esp+0x434 ; v45 = esp+0x438 = &v43 + 4.
                // Worker writes (0x45735d `mov [esp+esi+434h], ebx`, esi starts 0 and
                // is +=4 BEFORE each write) land the FIRST worker id at esp+0x438 = v45.
                // The pad loop (0x457372) writes -1 starting at m=4*v31, but with its
                // increment-then-write it first touches esp+0x438 only when v31==0.
                // Therefore v45 != -1 IFF at least ONE worker was found (v31 >= 1).
                // 0x4573e0: if (v45 != -1) → emit the command and clear target;
                // else → skip emission, return v51=1.
                bool v45IsNeg1 = (v31 < 1);
                if (v45IsNeg1) {
                    return 1; // return v51 (=1)
                }
            }

            // 0x4573ed: VIBE_Command_QueueRequestSlotReset28(&v37, v30)
            // 0x4573f9: *(mr+448) = -1  (clear target after issuing attack)
            wr32(mr, kM_target, -1);
            if (g_meisterCmdSink)
                g_meisterCmdSink->push(cmd);

            return 1; // 0x457415
        }
    }

    // -----------------------------------------------------------------------
    // SPY PATH (j == null OR strength threshold NOT met).
    // gilde.exe 0x456d65..0x457130.
    // -----------------------------------------------------------------------
    {
        // He handler check: if existing spy/attack handler found → return v51 (no new cmd).
        // 0x456d88..0x456e09: same three-type search as above.
        {
            u8* existing = heSearch();
            if (existing) {
                // 0x457131..0x457145: "hat schon eine Spionage" log → DROP. Return v51.
                return 1;
            }
        }

        // 0x456e40: Build spy command (cmdType = 64).
        {
            MeisterCommand cmd;
            cmd.cmdType = 64; // 0x456e45: v38 = 64 spy

            // 0x456e74: actorId = dword_12CE914[134 * *(u16*)(meisterBldg2+39)]
            {
                u16 ownerIdx = meisterBldg2 ? rdu16(meisterBldg2, kB_owner39) : 0u;
                cmd.actorId = (ownerIdx < kPersonCapacity) ? g_personIds[ownerIdx] : 0;
            }

            // 0x456e97: buildingId = *(meisterBldg2+1)
            cmd.buildingId = meisterBldg2 ? rd32(meisterBldg2, kB_id1) : -1;

            // 0x456ea0..0x456ea3: gametime + trailing fields
            cmd.timePacked = (static_cast<u64>(g_meisterGameTime.day) |
                              (static_cast<u64>(g_meisterGameTime.hour)   << 32) |
                              (static_cast<u64>(g_meisterGameTime.minute) << 48));
            cmd.timeExtra  = g_meisterTimeExtra;
            cmd.timeTail   = g_meisterTimeTail;

            // 0x456ea5: v44 = 1 (mode byte); LOBYTE(v17) = 1 at 0x456e9e
            cmd.mode = 1;

            // 0x456eaf: v46 = *(a3+1) target building id
            cmd.targetId = targetBldgId;

            // 0x456ecd: v47 = *(meisterBldg2+1)
            cmd.srcId = meisterBldg2 ? rd32(meisterBldg2, kB_id1) : -1;

            // 0x456ed7: cap workers at min(attackBudget, 8)
            int v18 = (budget >= 8) ? 8 : budget;
            int v19 = v18;

            // 0x456ee7..0x457107: worker collection loop (same criteria as attack path)
            {
                int v20 = 0; // count of workers found
                int v21 = 0;
                int v17 = 0; // byte offset tracker (used in call to QueueRequestSlotReset28)

                i32 spyBldgRecPtr = rd32(mr, kM_bldgRec); // *(mr+364)
                if (v18 > 0) {
                    for (unsigned int v22 = 0;
                         v20 < v19 && static_cast<int>(v22) < 411648;
                         v22 += static_cast<unsigned int>(kPersonStride))
                    {
                        int pidx = static_cast<int>(v22 / kPersonStride);
                        u8* personBase = reinterpret_cast<u8*>(&g_persons[pidx]);
                        // word_12CE910[v22/2] != -1
                        if (personBase[0] == 0xFF && personBase[1] == 0xFF)
                            continue;
                        // dword_12CEA7C[v22/4] == *(mr+364)
                        i32 employer = rd32(personBase, kP_employer); // +0x16C
                        if (employer != spyBldgRecPtr)
                            continue;
                        // byte_12CEA75[v22]
                        if (!rd8(personBase, kP_profByte))
                            continue;
                        // !dword_12CEA8C[v22/4]
                        if (rd32(personBase, kP_busy))
                            continue;
                        // dword_12CEA94[v22/4] != 0
                        i32 v23 = rd32(personBase, kP_actionObj); // +0x184
                        if (!v23)
                            continue;
                        // *(v23+44) == *(dword_12CEA7C[v22/4]+1) (handle columns)
                        u8* aoRec2  = resolveHandle(v23);
                        u8* empRec2 = resolveHandle(employer);
                        if (!aoRec2 || !empRec2)
                            continue;
                        i32 actionBldgId = rd32(aoRec2, 44);
                        i32 empBldgId = rd32(empRec2, kB_id1);
                        if (actionBldgId != empBldgId)
                            continue;

                        v17 += 4;
                        ++v21;
                        ++v20;
                        cmd.workerIds.push_back(g_personIds[pidx]);
                    }
                }
                // v17 passes the buffer byte-offset to QueueRequestSlotReset28 in orig;
                // in the reimpl we carry the worker list in cmd.workerIds directly.
                (void)v17;
            }

            // 0x457119: VIBE_Command_QueueRequestSlotReset28(&v37, v17) — always emits
            // (no v45 guard for the spy path).
            if (g_meisterCmdSink)
                g_meisterCmdSink->push(cmd);

            return 1; // 0x45711e
        }
    }
}

} // namespace guild::sim
