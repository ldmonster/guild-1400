// =============================================================================
// guild::play — session_persons3d implementation. See the header for the chain
// (addresses) and the session call contract. Everything routes through the
// PUBLIC CityView3D person API; no owned module is edited.
//
// Wave 4 adds the LIVE WALKING placement chain the wave-3 header documented:
// movement tile (wire_npc_movement record pad +0x6C/+0x70, state +0x74) ->
// render::TileToWorld @0x5c65d4 over the session terrain heightmap -> the
// person's world placement; captured entrance-dummy seats (the genuine
// SpawnAtBuildingEntrance @0x57c8f0 defaults) are the static fallback.
// =============================================================================
#include "play/session_persons3d.h"

#include "play/wire_npc_movement.h"   // GetEntityMovePos (the live tile source)
#include "render/heightmap.h"         // TileToWorld @0x5c65d4

#include <cmath>
#include <map>

namespace guild::play {

namespace {

// Count the bound persons with a live pose (the status `posed` column).
int CountPosed(const CityView3D& view) {
    int n = 0;
    for (const CityView3D::BoundPerson& bp : view.boundPersons())
        if (bp.posed)
            ++n;
    return n;
}

SessionPersons3DStatus BindAndReport(CityView3D& view,
                                     const SessionPersons3DOptions& opt) {
    SessionPersons3DStatus st;
    st.animsMounted    = view.personAnimsMounted();
    st.bound           = view.BindPersons(opt.maxPersons);
    st.posed           = CountPosed(view);
    st.unplaced        = view.personsUnplaced();
    st.modelUnresolved = view.personModelUnresolved();
    return st;
}

// ---------------------------------------------------------------------------
// Movement-placement state (process-static, mirroring the sibling bridges):
// the captured static seats (person id -> the default entrance-dummy seat the
// view computed at BindPersons) + the terrain heightmap the tile placement
// samples. Cleared by ClearSessionPersonsMovePlacement().
// ---------------------------------------------------------------------------
std::map<i32, CityPlacement>& SeatStore() {
    static std::map<i32, CityPlacement> seats;
    return seats;
}
const guild::render::Heightmap* g_moveHm = nullptr;

// Does this person have a live movement tile (an active path, or an arrived
// destination whose tile it should stay at)? Fills the tile when so.
bool LiveMoveTile(i32 personId, int* tileX, int* tileZ) {
    const NpcMovePos mp = GetEntityMovePos(personId);
    if (!mp.found)
        return false;
    const bool arrivedAtDest = (mp.destX != 0 || mp.destZ != 0) &&
                               mp.curX == mp.destX && mp.curZ == mp.destZ;
    if (!mp.active && !arrivedAtDest)
        return false;
    *tileX = mp.curX;
    *tileZ = mp.curZ;
    return true;
}

// The movement-aware placement: live tile -> TileToWorld (real ground height),
// else the captured entrance-dummy seat, else unplaced (rule 8).
bool MovePlacement(const sim::Person& p, CityPlacement& out) {
    int tx = 0, tz = 0;
    if (g_moveHm && LiveMoveTile(p.id, &tx, &tz)) {
        float w[3];
        if (guild::render::TileToWorld(g_moveHm, tx, tz, w)) {
            out.pos[0] = w[0];
            out.pos[1] = w[1];
            out.pos[2] = w[2];
            // Zero euler — the dword_577A78 default spawn rotation (the walk
            // heading interpolation is the charaction runtime; named gap).
            out.euler[0] = out.euler[1] = out.euler[2] = 0.0f;
            return true;
        }
    }
    auto it = SeatStore().find(p.id);
    if (it == SeatStore().end())
        return false;             // no captured seat either -> unplaced
    out = it->second;
    return true;
}

int CaptureSeats(CityView3D& view) {
    int captured = 0;
    for (const CityView3D::BoundPerson& bp : view.boundPersons()) {
        // Keep the FIRST captured seat per person (the default entrance-dummy
        // bind); a later capture of a person already walking would otherwise
        // turn its transient tile position into the static fallback.
        if (SeatStore().emplace(bp.id, bp.place).second)
            ++captured;
    }
    return captured;
}

} // namespace

SessionPersons3DStatus WireSessionPersons3D(CityView3D& view,
                                            shim::IFileSystem* fs,
                                            const SessionPersons3DOptions& opt) {
    if (opt.mountAnims && fs)
        view.InitPersonAnims(fs, opt.animsArchive);

    // Install the option hooks into the view's PERSON slots only — the
    // building/object hooks the session already installed are preserved.
    CityView3DHooks h = view.hooks();
    h.resolvePersonAnchor    = opt.anchorProvider;       // empty == default
    h.resolvePersonPlacement = opt.placementOverride;    // empty == default
    h.resolvePersonModel     = opt.modelOverride;        // empty == 0x57c1e8
    view.SetHooks(std::move(h));

    return BindAndReport(view, opt);
}

int UpdateSessionPersons3D(CityView3D& view, float stepTicks) {
    return view.AdvancePersonPoses(stepTicks);
}

SessionPersons3DStatus RebindSessionPersons3D(CityView3D& view,
                                              const SessionPersons3DOptions& opt) {
    return BindAndReport(view, opt);
}

void UnwireSessionPersons3D(CityView3D& view) {
    view.UnbindPersons();
    CityView3DHooks h = view.hooks();
    h.resolvePersonAnchor    = nullptr;
    h.resolvePersonPlacement = nullptr;
    h.resolvePersonModel     = nullptr;
    view.SetHooks(std::move(h));
}

// ===========================================================================
// LIVE WALKING POSITIONS (wave 4 — see the header).
// ===========================================================================

int InstallSessionPersonsMovePlacement(CityView3D& view,
                                       SessionPersons3DOptions& opt,
                                       const guild::render::Heightmap* hm) {
    g_moveHm = hm;
    const int captured = CaptureSeats(view);

    opt.placementOverride = &MovePlacement;
    CityView3DHooks h = view.hooks();
    h.resolvePersonPlacement = &MovePlacement;
    view.SetHooks(std::move(h));
    return captured;
}

int UpdateSessionPersons3DPositions(CityView3D& view,
                                    const SessionPersons3DOptions& opt) {
    if (!g_moveHm)
        return 0;
    int changed = 0;
    for (const CityView3D::BoundPerson& bp : view.boundPersons()) {
        int tx = 0, tz = 0;
        if (!LiveMoveTile(bp.id, &tx, &tz))
            continue;
        float w[3];
        if (!guild::render::TileToWorld(g_moveHm, tx, tz, w))
            continue;
        const float dx = w[0] - bp.place.pos[0];
        const float dy = w[1] - bp.place.pos[1];
        const float dz = w[2] - bp.place.pos[2];
        if (std::fabs(dx) > 1e-4f || std::fabs(dy) > 1e-4f ||
            std::fabs(dz) > 1e-4f)
            ++changed;
    }
    if (changed > 0) {
        // Re-run the bind so the next frame draws the moved persons at their
        // live tile positions (the documented rebind-on-move stand-in; the
        // cheap per-instance move is the CityView3D handoff — see the header).
        RebindSessionPersons3D(view, opt);
    }
    return changed;
}

int RecaptureSessionPersonsSeats(CityView3D& view) {
    return CaptureSeats(view);
}

SessionPersons3DStatus RebindSessionPersonsAfterSimChange(
    CityView3D& view, SessionPersons3DOptions& opt) {
    if (!g_moveHm)
        return RebindSessionPersons3D(view, opt);   // movement not installed
    // 1. Default entrance-dummy bind (clear the person-placement hook only),
    //    so new persons' genuine seats are computed by the view itself.
    CityView3DHooks h = view.hooks();
    h.resolvePersonPlacement = nullptr;
    view.SetHooks(std::move(h));
    BindAndReport(view, opt);
    CaptureSeats(view);
    // 2. Re-install the movement placement and rebind with it (moving persons
    //    back at their live tiles; everyone else at the captured seats).
    SessionPersons3DOptions o = opt;
    o.mountAnims = false;                           // archive already mounted
    o.placementOverride = &MovePlacement;
    opt.placementOverride = &MovePlacement;
    return WireSessionPersons3D(view, nullptr, o);
}

void ClearSessionPersonsMovePlacement() {
    SeatStore().clear();
    g_moveHm = nullptr;
}

} // namespace guild::play
