#include "sim/animal_wander.h"

#include "sim/map.h"               // MapTraceLineOfSight (VIBE_Map_TraceLineOfSight @0x406f10)
#include "render/heightmap.h"      // TileToWorld / WorldToTileWithHeight (@0x5c65d4/@0x5c6644)
#include "util/math_random.h"      // RandomModulo (VIBE_Math_RandomModulo @0x58b89c)
#include "util/math.h"             // VectorWithinTolerance (VIBE_Math_VectorWithinTolerance @0x5caa4c)
#include "util/transform.h"        // PointThroughBoneChain (VIBE_Transform_PointThroughBoneChain @0x5c8b38)

#include <cstring>

// Faithful 1:1 port of the animal wander / herd / spawn-placement helpers, the
// per-species AI step and the model-handle table from gilde.exe. The render/scene
// leaves the spatial math depends on are translated SIBLINGS (called directly);
// the building-iteration / scene-actor / mesh leaves are routed through
// IAnimalSceneOps so the logic is testable in isolation.

namespace guild::sim {

// ---------------------------------------------------------------------------
// Scene-ops hook.
// ---------------------------------------------------------------------------
static IAnimalSceneOps g_defaultSceneOps;
static IAnimalSceneOps* g_sceneOps = &g_defaultSceneOps;
void SetAnimalSceneOps(IAnimalSceneOps* ops) {
    g_sceneOps = ops ? ops : &g_defaultSceneOps;
}
IAnimalSceneOps* AnimalSceneOps() { return g_sceneOps; }

// ---------------------------------------------------------------------------
// Model-handle table (gilde.exe dword_B59BC0 / unk_62EF48).
// ---------------------------------------------------------------------------
int g_animalModelHandles[kAnimalModelCapacity] = {0};  // dword_B59BC0
int g_animalModelCount = 0;                             // unk_62EF48

void ResetAnimalWander() {
    std::memset(g_animalModelHandles, 0, sizeof(g_animalModelHandles));
    g_animalModelCount = 0;
    g_sceneOps = &g_defaultSceneOps;
}

// ===========================================================================
// VIBE_Animal_BuildWanderPath  0x484200
// ---------------------------------------------------------------------------
//   v5 = off_649D64+44 (scene heightmap);
//   WorldToTileWithHeight(v5, start, &tileX, &h)  -> (v13=tileX, v14=tileY)
//   for i in 0..count:
//     ox = (u16)RandomModulo(10) - 4 + tileX
//     oy = (u16)RandomModulo(10) - 4 + tileY
//     ox = min(ox, size-2); ox = max(ox, 2)   (clamp order: hi then lo)
//     oy = min(oy, size-2); oy = max(oy, 2)
//     if TraceLineOfSight(v5, start, ox, start, oy, &ox, &oy, 600, 0): use (ox,oy)
//     else: use (tileX,tileY)
//     TileToWorld(v5, col, &out, row);  out += 4 floats
//   return count
// ===========================================================================
int Animal_BuildWanderPath(const render::Heightmap* hm, const float* startWorld,
                           int count, float* outWorld) {
    if (!hm)
        return count;  // original would deref a null map; we guard (no points written)

    int startCol = 0, startRow = 0;
    float h = 0.0f;
    // WorldToTileWithHeight(v5, start, &v13, &v15): v13=col(x tile), v14=row(y tile).
    render::WorldToTileWithHeight(hm, startWorld, &startCol, &startRow, &h);

    const int size = hm->size;
    for (int i = 0; i < count; ++i) {
        // ox/oy in [-4..+5] from the start tile (RandomModulo(10) - 4).
        int col = static_cast<u16>(util::RandomModulo(10)) - 4 + startCol;
        int row = static_cast<u16>(util::RandomModulo(10)) - 4 + startRow;

        // Clamp hi (size-2) then lo (2), matching the original's branch order.
        int hi = size - 2;
        if (hi < col)
            col = hi;
        if (col < 2)
            col = 2;
        if (hi < row)
            row = hi;
        if (row < 2)
            row = 2;

        int outCol = col, outRow = row;
        int chosenCol, chosenRow;
        if (MapTraceLineOfSight(hm, startWorld, col, startWorld, row,
                                &outCol, &outRow, 600, 0)) {
            chosenCol = outCol;
            chosenRow = outRow;
        } else {
            chosenCol = startCol;
            chosenRow = startRow;
        }
        render::TileToWorld(hm, chosenCol, chosenRow, outWorld + i * 4);
    }
    return count;
}

// ===========================================================================
// VIBE_Animal_CollectSpawnBuilding  0x48432c
//   if (!StrCmp(name, wanted)) { ctx.records[ctx.count++] = record; }
//   return ctx.count < 32;   // keep walking while under the 32 cap
// (StrCmp returns 0 on equality; the original tests !StrCmp.)
// ===========================================================================
bool Animal_CollectSpawnBuilding(SpawnBuildingCollector* ctx, const char* recordName,
                                 void* record) {
    if (recordName && ctx->wantedName && std::strcmp(recordName, ctx->wantedName) == 0) {
        int n = ctx->count;
        // HARDEN (wave-10): the records[] array holds 32 slots. The original
        // appends then returns `count < 32`, so the WalkAndInvoke caller stops the
        // moment count hits 32 — index n is always < 32 on the in-bounds path
        // (byte-identical). Guard the write so a direct call with count already at
        // the cap cannot scribble past records[31].
        if (n >= 0 && n < 32) {
            ctx->records[n] = record;
            ctx->count = n + 1;
        }
    }
    return ctx->count < 32;
}

// ===========================================================================
// VIBE_Animal_PickSpawnBuilding  0x484374
//   copy `name` into a local; WalkAndInvoke(scene, CollectSpawnBuilding, ...);
//   if (count) return records[RandomModulo(count)]; else return 0;
// The scene walk is provided via EnumBuildings; the name match runs through the
// CollectSpawnBuilding collector (cap 32, mirroring the original's bound).
// ===========================================================================
void* Animal_PickSpawnBuilding(int town, const char* name,
                               const char* (*nameOf)(void*)) {
    SpawnBuildingCollector col;
    col.wantedName = name;
    col.count = 0;

    BuildingAnchor cand[32];
    int n = g_sceneOps->EnumBuildings(town, cand, 32);
    for (int i = 0; i < n; ++i) {
        const char* recName = nameOf ? nameOf(cand[i].record) : nullptr;
        if (!Animal_CollectSpawnBuilding(&col, recName, cand[i].record))
            break;  // collector says stop (hit the 32 cap)
    }
    if (col.count)
        return col.records[static_cast<u16>(util::RandomModulo(static_cast<u16>(col.count)))];
    return nullptr;
}

// ===========================================================================
// VIBE_Animal_FindHerdGrouping  0x4839f0
// ---------------------------------------------------------------------------
//   if (rec[8] != -1) return rec;   // already computed
//   gather candidate pasture buildings (non-prod, non-storage, frame != 0)
//   anchor = candidates[RandomModulo(n)];
//   PointThroughBoneChain(anchor.frame, anchor.frame+76floats?, anchorPos);
//   for each candidate (while groupCount < 16):
//     PointThroughBoneChain(cand.frame, ..., candPos);
//     if (cand != anchor && VectorWithinTolerance(anchorPos, candPos, 6000)):
//       store candPos into herd buffer; groupCount += 3 (writes x,y,z slots)
//   rec[8] = groupCount;
//   return rec;
// The original stamps only the COUNT back into the record (the gathered points
// live on a stack buffer and are discarded). We mirror that; `outPoints` (if
// non-null) receives the gathered points for testability.
// ===========================================================================
// Helper exposed for tests: full grouping with an explicit candidate set + an
// optional out-buffer. The public Animal_FindHerdGrouping uses EnumBuildings.
static int HerdGroupFrom(const BuildingAnchor* cand, int n, float* outPoints) {
    if (n <= 0)
        return 0;

    // anchor = candidates[RandomModulo(n)]  (the original's v18).
    const BuildingAnchor& anchor = cand[static_cast<u16>(util::RandomModulo(static_cast<u16>(n)))];

    float anchorPos[4] = {0, 0, 0, 0};
    // PointThroughBoneChain(frame, frame+76bytes(==+19 floats), out).
    util::PointThroughBoneChain(anchor.frame, anchor.frame + 19, anchorPos);

    int group = 0;          // v9 (number of stored floats / triple-stride writes)
    int seen = 0;           // v7
    int idx = 0;            // v8
    while (seen < n && group < 16) {
        float candPos[4] = {0, 0, 0, 0};
        util::PointThroughBoneChain(cand[idx].frame, cand[idx].frame + 19, candPos);
        if (cand[idx].record == anchor.record ||
            !util::VectorWithinTolerance(anchorPos, candPos, 6000.0f)) {
            ++idx;
            ++seen;
        } else {
            if (outPoints) {
                outPoints[group + 0] = candPos[0];
                outPoints[group + 1] = candPos[1];
                outPoints[group + 2] = candPos[2];
            }
            group += 3;     // v9 advances by 3 (x,y,z slot writes), matching the original
            ++idx;
            ++seen;
        }
    }
    return group;
}

AnimalRec* Animal_FindHerdGrouping(AnimalRec* rec) {
    if (!rec)
        return rec;
    if (rec->herdCount != -1)   // *(rec+8) == -1 gate
        return rec;

    BuildingAnchor cand[256];
    int n = g_sceneOps->EnumBuildings(0, cand, 256);
    int group = HerdGroupFrom(cand, n, nullptr);
    rec->herdCount = group;     // *(rec+8) = v9
    return rec;
}

// ===========================================================================
// VIBE_Animal_FindDoorTarget  0x484160
// ---------------------------------------------------------------------------
//   gather candidate buildings (non-prod, non-storage, frame != 0);
//   pick = candidates[RandomModulo(n)];
//   door = Object_FindByHandle(pick.frame, 768, "dummy_TUER", 0, pick);
//   if (door) frame = door; else frame = pick.frame;
//   PointThroughBoneChain(frame, frame+76, outPos);  return 1;
// The original always indexes candidates[RandomModulo(n)] even when n==0
// (RandomModulo(0)==0 -> candidates[0], uninitialised); we guard n==0 -> return 0.
// ===========================================================================
int Animal_FindDoorTarget(int town, float* outPos) {
    BuildingAnchor cand[256];
    int n = g_sceneOps->EnumBuildings(town, cand, 256);
    if (n <= 0)
        return 0;

    const BuildingAnchor& pick =
        cand[static_cast<u16>(util::RandomModulo(static_cast<u16>(n)))];

    float* doorFrame = g_sceneOps->FindDoorFrame(pick.record);
    float* frame = doorFrame ? doorFrame : pick.frame;
    // Door object: anchor at frame+19 floats (the original used v9+19 for the door
    // object case and *(rec+97)+19 for the building case; both are frame+76 bytes).
    util::PointThroughBoneChain(frame, frame + 19, outPos);
    return 1;
}

// ===========================================================================
// VIBE_Animal_UpdateCat   0x483e34
// VIBE_Animal_UpdateSheep 0x483fd4
// ---------------------------------------------------------------------------
// Shared shape; the only differences are the wander odds (cat <=30, sheep <=20),
// the wander tag ("anm_UpdateCat" / "anm_UpdateSheep") and one extra cat-only
// d100 burn on the sound branch when the cat's kind byte (rec+4) is 0.
//   if (actionPending) return;            // actor+296 != 0 -> busy
//   if (RandomModulo(100) <= ODDS):
//       steps = RandomModulo(3) + 1;
//       n = BuildWanderPath(rec.pos, steps, path);
//       for each path point: if WorldToTile(point) -> issue walk action(col,row,tag)
//   else:
//       Cat (0x483e7b):   if (rec.kind==0) RandomModulo(100);  // conditional burn
//       Sheep (0x484015): RandomModulo(100);                   // UNCONDITIONAL burn
//       sound = RandomModulo(3) + 1; CreateSoundAction(actor, sound);
// RNG-draw count is load-bearing: the sheep sound branch ALWAYS spends an extra
// d100 (verified at 0x484015 — no guard), whereas the cat branch only spends it
// when the kind byte (rec+4) is 0 (0x483e7b). `soundBurn` selects the policy:
//   kCatSoundBurn  -> burn iff rec->kind == 0
//   kSheepSoundBurn-> always burn
// ===========================================================================
enum SoundBurnPolicy { kCatSoundBurn, kSheepSoundBurn };

static char UpdateAnimalAI(AnimalRec* rec, int actionPending, int wanderOdds,
                           const char* tag, SoundBurnPolicy soundBurn) {
    char ret = 0;
    if (actionPending)        // *(actor+296) != 0 -> idle-gate fails, return early
        return ret;

    if (static_cast<u16>(util::RandomModulo(100)) <= static_cast<unsigned>(wanderOdds)) {
        const render::Heightmap* hm = g_sceneOps->SceneHeightmap();
        int steps = static_cast<u16>(util::RandomModulo(3)) + 1;  // 1..3 (v3+1)
        float path[16 * 4] = {0};
        // The original passes (float*)rec + 4 (the record's spawn x,y,z at +16).
        // Copy out of the packed record into an aligned 3-float point.
        const float pos[3] = {rec->x, rec->y, rec->z};
        int n = Animal_BuildWanderPath(hm, pos, steps, path);
        for (int i = 0; i < n; ++i) {
            int col = 0, row = 0;
            float ph = 0.0f;
            // WorldToTileWithHeight returns true only for on-map points.
            if (hm && render::WorldToTileWithHeight(hm, path + i * 4, &col, &row, &ph)) {
                g_sceneOps->IssueWanderAction(rec, col, row, tag);
                ret = 1;
            }
        }
    } else {
        // Sound branch d100 burn: sheep ALWAYS burns (0x484015), cat burns only
        // when its kind byte (rec+4) is 0 (0x483e7b). This extra draw is part of
        // the RNG stream and must match exactly.
        if (soundBurn == kSheepSoundBurn || (soundBurn == kCatSoundBurn && rec->kind == 0))
            util::RandomModulo(100);
        int sound = static_cast<u16>(util::RandomModulo(3)) + 1;  // 1..3 (v4+1)
        g_sceneOps->IssueSoundAction(rec, sound);
        ret = static_cast<char>(sound);
    }
    return ret;
}

char Animal_UpdateCat(AnimalRec* rec, int actionPending) {
    return UpdateAnimalAI(rec, actionPending, 30, "anm_UpdateCat", kCatSoundBurn);
}

char Animal_UpdateSheep(AnimalRec* rec, int actionPending) {
    return UpdateAnimalAI(rec, actionPending, 20, "anm_UpdateSheep", kSheepSoundBurn);
}

// ===========================================================================
// VIBE_Animal_ResetModelHandles  0x484424
//   for (i = 0; i < count; ++i) { if (handles[i]) ReleaseStockObject(handles[i]);
//                                 handles[i+1] = 0; }   // off-by-one in original
// The original's write is dword_B59BBC[i+1] (== dword_B59BC0[i]); we zero index i
// and clear the count — observationally the table is emptied.
// ===========================================================================
void Animal_ResetModelHandles() {
    int count = g_animalModelCount;
    for (int i = 0; i < count; ++i) {
        if (g_animalModelHandles[i])
            g_sceneOps->ReleaseMesh(g_animalModelHandles[i]);
        g_animalModelHandles[i] = 0;
    }
    g_animalModelCount = 0;
}

// ===========================================================================
// VIBE_Animal_LoadModels  0x484468
//   if (count) reset();   // release any previously loaded set
//   handles[count++] = LoadMesh("hund_HUND");
//   handles[count++] = LoadMesh("katze_KATZE");
//   handles[count++] = LoadMesh("kuh_KUH");
//   handles[count++] = LoadMesh("pferd_PFERD");
//   handles[count++] = LoadMesh("schaf_SCHAF");
//   handles[count++] = LoadMesh("pferd_PFERD");
//   handles[count++] = LoadMesh("schwein_SCHWEIN");
//   return count;
// (The original's index/count bookkeeping is the messy dword_B59BC0/dword_B59BBC
// pair with +1 offsets; the net effect is the 7 meshes loaded in this order.)
// ===========================================================================
int Animal_LoadModels() {
    if (g_animalModelCount)
        Animal_ResetModelHandles();

    static const char* const kModelNames[7] = {
        "hund_HUND", "katze_KATZE", "kuh_KUH", "pferd_PFERD",
        "schaf_SCHAF", "pferd_PFERD", "schwein_SCHWEIN",
    };
    for (int i = 0; i < 7; ++i) {
        int handle = g_sceneOps->LoadMesh(kModelNames[i]);
        if (g_animalModelCount < kAnimalModelCapacity)
            g_animalModelHandles[g_animalModelCount] = handle;
        ++g_animalModelCount;
    }
    return g_animalModelCount;
}

// Test/integration helper: full herd grouping with an explicit candidate set.
// (Exposes HerdGroupFrom without forcing EnumBuildings; same math the public
// Animal_FindHerdGrouping uses internally.)
int Animal_HerdGroupFrom(const BuildingAnchor* cand, int n, float* outPoints) {
    return HerdGroupFrom(cand, n, outPoints);
}

}  // namespace guild::sim
