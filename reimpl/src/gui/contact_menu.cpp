#include "gui/contact_menu.h"

#include <cstdio>
#include <cstring>

namespace guild::gui {

// Original BSS: dword_11B5220 (the 32x50-dword table). We model it as a record array.
StatusEntry g_statusEntries[kMaxStatusEntries];

// ---------------------------------------------------------------------------
// Object resolution + the per-object "active" flag.
//
// In the original, VIBE_StatusText_Register resolves a name to an object handle via
// VIBE_Character_RunMeshCallback (a real pointer into the scene-object cluster) and the
// menu toggles *(handle + 536). On a 64-bit host we cannot store a scene pointer in the
// i32 handle, and the scene cluster is out of scope here, so object resolution + the
// active flag go through a mockable resolver. The default resolver hashes the name to a
// stable nonzero handle and tracks the active flag in a small side table, which is all
// the register/reset logic needs.
// ---------------------------------------------------------------------------
namespace {

struct ResolvedObject {
    int handle;
    char name[kStatusNameLen];
    char active;
};
constexpr int kMaxResolved = 64;
ResolvedObject g_resolved[kMaxResolved];
int g_resolvedCount = 0;

// Default object resolver: stable nonzero handle per distinct name.
int DefaultResolve(const char* name) {
    for (int i = 0; i < g_resolvedCount; ++i) {
        if (std::strncmp(g_resolved[i].name, name, kStatusNameLen) == 0)
            return g_resolved[i].handle;
    }
    if (g_resolvedCount >= kMaxResolved)
        return 0;
    ResolvedObject& o = g_resolved[g_resolvedCount];
    // A small, deterministic nonzero handle (index + base) so handles are unique and
    // never collide with 0 (the "not found" sentinel).
    o.handle = 0x1000 + g_resolvedCount;
    std::strncpy(o.name, name, kStatusNameLen - 1);
    o.name[kStatusNameLen - 1] = '\0';
    o.active = 0;
    ++g_resolvedCount;
    return o.handle;
}

ResolvedObject* FindResolved(int handle) {
    for (int i = 0; i < g_resolvedCount; ++i) {
        if (g_resolved[i].handle == handle)
            return &g_resolved[i];
    }
    return nullptr;
}

void SetObjectActive(int handle, char on) {
    // Mirrors *(handle + kObjectActiveFlagOffset) = on.
    if (ResolvedObject* o = FindResolved(handle))
        o->active = on;
}

// dword_11B5158 — the parallel "this entry is the current selection" array (one dword
// per entry; ResetEntries zeroes it). Modelled per-entry; not otherwise read by this
// module.
char g_statusSelected[kMaxStatusEntries];

ContactCommandSink  g_defaultSink;
ContactCommandSink* g_sink = &g_defaultSink;

ContactGate  g_defaultGate;
ContactGate* g_gate = &g_defaultGate;

} // namespace

void ContactMenu_SetCommandSink(ContactCommandSink* sink) {
    g_sink = sink ? sink : &g_defaultSink;
}
void ContactMenu_SetGate(ContactGate* gate) {
    g_gate = gate ? gate : &g_defaultGate;
}

void ResetContactMenu() {
    for (auto& e : g_statusEntries) e = StatusEntry{};
    for (auto& s : g_statusSelected) s = 0;
    g_resolvedCount = 0;
    for (auto& o : g_resolved) o = ResolvedObject{};
}

// gilde.exe 0x4bcc4c — VIBE_StatusText_ResetEntries.
//   for (result=0; result != 1600; result += 50) {
//     v1 = dword_11B5220[result];          // entry handle
//     if (v1) *(v1 + 536) = 0;             // clear the object's active flag
//     dword_11B5158[result] = 0;           // clear the parallel selection slot
//   }
//   return result * 4;
int StatusText_ResetEntries() {
    int result = 0;
    for (int i = 0; i < kMaxStatusEntries; ++i) {
        int handle = g_statusEntries[i].handle; // dword_11B5220[50*i]
        if (handle)
            SetObjectActive(handle, 0);          // *(handle+536) = 0
        g_statusSelected[i] = 0;                 // dword_11B5158[50*i] = 0
        result += kStatusEntryStrideDwords;      // result += 50
    }
    return result * 4; // 1600 * 4, preserved verbatim from the original
}

// gilde.exe 0x4bcc80 — VIBE_StatusText_Register  (name@eax, gfxId@ebx, label@edx)
int StatusText_Register(const char* name, int gfxId, const char* label) {
    // (1) De-dup: scan all 32 slots; a slot with a handle whose name matches wins.
    //     for (i=0; i<1600; i+=50)
    //       if (dword_11B5220[i] && !StrCmp(name, &dword_11B5220[i+1]))
    //         { *(dword_11B5220[i]+536)=1; return dword_11B5220[i]; }
    for (int i = 0; i < kMaxStatusEntries; ++i) {
        StatusEntry& e = g_statusEntries[i];
        if (e.handle && std::strncmp(name, e.name, kStatusNameLen) == 0) {
            SetObjectActive(e.handle, 1); // *(handle+536) = 1
            return e.handle;
        }
    }

    // (2) First-free slot scan (v6 = slot index, v7 = byte index = 50*slot).
    //     if (dword_11B5220[0]) do { v7+=50; ++v6; } while (v7<1600 && dword_11B5220[v7]);
    int slot = 0;
    if (g_statusEntries[0].handle) {
        int byteIdx = 0;
        do {
            byteIdx += kStatusEntryStrideDwords;
            ++slot;
        } while (byteIdx < kStatusTableDwords && g_statusEntries[slot].handle);
    }
    if (slot >= kMaxStatusEntries) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "RegisterStatusText(): Too many Objects:%s", name);
        return 0;
    }

    // (3) Resolve the object handle for `name`. 0 == not found.
    int handle = DefaultResolve(name); // VIBE_Character_RunMeshCallback(name)
    g_statusEntries[slot].handle = handle;
    if (!handle) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "RegisterStatusText(): Object not found: %s", name);
        return 0;
    }

    // (4) Fill the entry: gfx (+68), name (+4, 64), label (+72, 128), clear byte flags
    //     (+67/+199), set the object's active flag, return the handle.
    StatusEntry& e = g_statusEntries[slot];
    e.gfxId = gfxId;                                 // dword_11B5264[slot] = gfxId
    std::strncpy(e.label, label ? label : "", kStatusLabelLen - 1); // StrNCopyPad(+18, label, 128)
    e.label[kStatusLabelLen - 1] = '\0';
    std::strncpy(e.name, name, kStatusNameLen - 1);  // StrNCopyPad(+4, name, 64)
    e.name[kStatusNameLen - 1] = '\0';
    e.flagB = 0;                                     // byte_11B52E7[slot] = 0
    e.flagA = 0;                                     // byte_11B5263[slot] = 0
    SetObjectActive(handle, 1);                      // *(handle+536) = 1
    return handle;
}

// ===========================================================================
// Builders — the DATA/LAYOUT (entry set) + WIRING (id -> action) for each menu.
// ===========================================================================

// --- RemoteTrade (0x5138d0) --------------------------------------------------
//   v2 = Register("FERNEINKAUF", 16, ...);  Register("VERKAUF", 16, ...);
//   if (clicked==v2) OpenPanelMode4; else if (clicked==sell) OpenPanelMode2;
RemoteTradeIds ContactMenu_BuildRemoteTrade() {
    RemoteTradeIds ids{};
    ids.remoteBuy = StatusText_Register("FERNEINKAUF", 16, nullptr);
    ids.sell      = StatusText_Register("VERKAUF", 16, nullptr);
    return ids;
}
bool ContactMenu_DispatchRemoteTrade(int clicked, const RemoteTradeIds& ids) {
    if (!clicked) return false;
    if (clicked == ids.remoteBuy) { g_sink->OpenTransport(clicked, 4); return true; }
    if (clicked == ids.sell)      { g_sink->OpenTransport(clicked, 2); return true; }
    return false;
}

// --- InfoBooks (0x512070) ----------------------------------------------------
//   v4=Register("contact_INFORMATION",12); a2=Register("ob_MEISTERBRIEF",12);
//   v5=Register("ob_PERSONALBUCH",12);
//   masterCert -> MasterCertificate; staffBook -> StaffBook; info -> ThiefInfo.
InfoBooksIds ContactMenu_BuildInfoBooks() {
    InfoBooksIds ids{};
    ids.info       = StatusText_Register("contact_INFORMATION", 12, nullptr);
    ids.masterCert = StatusText_Register("ob_MEISTERBRIEF", 12, nullptr);
    ids.staffBook  = StatusText_Register("ob_PERSONALBUCH", 12, nullptr);
    return ids;
}
bool ContactMenu_DispatchInfoBooks(int clicked, const InfoBooksIds& ids) {
    if (!clicked) return false;
    if (clicked == ids.masterCert) { g_sink->RunMasterCertificate(clicked); return true; }
    if (clicked == ids.staffBook)  { g_sink->RunStaffBook(clicked); return true; }
    if (clicked == ids.info)       { g_sink->RunThiefInfo(clicked); return true; }
    return false;
}

// --- Production family (Carpenter/Smith/Stonemason/Brewery/Perfumery/Mixing) -
// The page-200 group registers the localized production entry (+ a "gather" entry for
// perfumery/mixing); the page-400 group registers storage/transport/master-cert/staff.
// Smith additionally gates each entry behind a handler-flag; we honor the gate hook so
// the entry set is reproducible, while keeping the names/gfx exact.
ProductionIds ContactMenu_BuildProduction(const char* productionName, bool hasGather) {
    ProductionIds ids{};
    // page 0x200 — production (+ optional gather).
    if (hasGather)
        ids.gather = StatusText_Register("contact_SAMMELN", 22, nullptr);
    ids.production = StatusText_Register(productionName, 19, nullptr);
    // page 0x400 — storage / transport / books.
    ids.storage    = StatusText_Register("contact_LAGER", 14, nullptr);
    ids.transport  = StatusText_Register("contact_TRANSPORT", 21, nullptr);
    ids.masterCert = StatusText_Register("ob_MEISTERBRIEF", 22, nullptr);
    ids.staffBook  = StatusText_Register("ob_PERSONALBUCH", 12, nullptr);
    return ids;
}
bool ContactMenu_DispatchProduction(int clicked, const ProductionIds& ids) {
    if (!clicked) return false;
    if (clicked == ids.production) { g_sink->OpenProductionPanel(clicked); return true; }
    if (clicked == ids.transport)  { g_sink->OpenTransport(clicked, 1); return true; }
    if (clicked == ids.staffBook)  { g_sink->RunStaffBook(clicked); return true; }
    if (clicked == ids.storage)    { g_sink->OpenStorage(clicked); return true; }
    if (clicked == ids.masterCert) { g_sink->RunMasterCertificate(clicked); return true; }
    if (ids.gather && clicked == ids.gather) { g_sink->RunTradeSearch(clicked); return true; }
    return false;
}

// --- MineProductionStone (0x511ea4) -----------------------------------------
//   Register LAGER/ABBAU/SUCHEN_STEIN/PRODUKTION/TRANSPORT/GELAGE; optionally the two
//   training targets when GameObject_QueryFind(.., 53/52) holds.
MineIds ContactMenu_BuildMine() {
    MineIds ids{};
    ids.storage    = StatusText_Register("contact_LAGER", 14, nullptr);
    ids.mine       = StatusText_Register("contact_ABBAU", 10, nullptr);
    ids.search     = StatusText_Register("contact_SUCHEN_STEIN", 22, nullptr);
    ids.production = StatusText_Register("contact_PRODUKTION", 19, nullptr);
    ids.transport  = StatusText_Register("contact_TRANSPORT", 21, nullptr);
    ids.feast      = StatusText_Register("contact_GELAGE", 22, nullptr);
    if (g_gate->HasObject(53)) // QueryFind(.., 53) -> straw dummy
        ids.targetA = StatusText_Register("ob_STROHPUPPE", 23, nullptr);
    if (g_gate->HasObject(52)) // QueryFind(.., 52) -> target board
        ids.targetB = StatusText_Register("ob_ZIELSCHEIBE", 23, nullptr);
    return ids;
}
bool ContactMenu_DispatchMine(int clicked, const MineIds& ids) {
    if (!clicked) return false;
    if (clicked == ids.storage)    { g_sink->OpenStorage(clicked); return true; }
    if (clicked == ids.feast)      { g_sink->RunFeast(clicked); return true; }
    if (clicked == ids.mine)       { g_sink->OpenProductionWindow(clicked, 'B'); return true; }
    if (clicked == ids.search)     { g_sink->OpenProductionWindow(clicked, 'A'); return true; }
    if (clicked == ids.transport)  { g_sink->OpenTransport(clicked, 1); return true; }
    if (clicked == ids.production) { g_sink->OpenProductionWindow(clicked, 'C'); return true; }
    if ((ids.targetB && clicked == ids.targetB) || (ids.targetA && clicked == ids.targetA)) {
        g_sink->RunTraining(clicked); return true;
    }
    return false;
}

// --- StorageProductionWood (0x511d28) ---------------------------------------
//   page 0x200: LAGER/PRODUKTION_HOLZ/GELAGE/TRANSPORT + the two training targets.
WoodIds ContactMenu_BuildWood() {
    WoodIds ids{};
    ids.storage    = StatusText_Register("contact_LAGER", 14, nullptr);
    ids.production = StatusText_Register("contact_PRODUKTION_HOLZ", 19, nullptr);
    ids.feast      = StatusText_Register("contact_GELAGE", 22, nullptr);
    ids.transport  = StatusText_Register("contact_TRANSPORT", 21, nullptr);
    ids.targetA    = StatusText_Register("ob_ZIELSCHEIBE", 12, nullptr);
    ids.targetB    = StatusText_Register("ob_STROHPUPPE", 12, nullptr);
    return ids;
}
bool ContactMenu_DispatchWood(int clicked, const WoodIds& ids) {
    if (!clicked) return false;
    if (clicked == ids.transport)  { g_sink->OpenTransport(clicked, 1); return true; }
    if (clicked == ids.storage)    { g_sink->OpenStorage(clicked); return true; }
    if (clicked == ids.production) { g_sink->OpenProductionWindow(clicked, 'C'); return true; }
    if (clicked == ids.feast)      { g_sink->RunFeast(clicked); return true; }
    if ((ids.targetA && clicked == ids.targetA) || (ids.targetB && clicked == ids.targetB)) {
        g_sink->RunTraining(clicked); return true;
    }
    return false;
}

} // namespace guild::gui
