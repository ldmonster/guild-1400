// See wire_combat2.h. Binds the directly-bindable fields of the BrawlHooks and
// Recon2Hooks bridges to their real reconstructed leaves (rule 13). Glue only.
//
// The brawl free/find leaves operate on the SAME shared real He/HandlerEntry pool
// and CommandQueue the existing real-hooks waves own (RealHandlerTable() /
// RealCommandQueue()); HeRecord and HandlerRecord are raw POD blobs over the same
// record-base byte layout, so the reinterpret_cast is byte-faithful (exactly as
// real_hooks3 already casts HeRecord*<->HandlerRecord*). BrawlStep's victim resolve
// (VIBE_Person_FindRecordById @0x58bc6c) returns a Person* the rule only reads as an
// id/alive word, so a Person* surfaces fine through the hook's `void*`.
#include "sim/wire_combat2.h"

#include "sim/charaction_brawl.h"          // BrawlHooks / SetBrawlHooks / GetBrawlHooks
#include "sim/character_recon2_cmds.h"     // Recon2Hooks / SetRecon2Hooks / GetRecon2Hooks

// The four ZERO-bindable bridges are included only so the inert-field inventory is
// compile-checked against the real struct definitions (no field is bound here).
#include "sim/character_query.h"           // CharQueryHooks
#include "sim/combat_slots3.h"             // CombatSlots3Hooks
#include "sim/combat_slots4.h"             // CombatSlots4Hooks
#include "sim/command_apply9.h"            // CheckHooks

#include "sim/real_hooks.h"                // RealCommandQueue()
#include "sim/real_hooks3.h"               // RealHandlerTable()
#include "sim/command.h"                   // CommandQueue::GetPacketStatusById
#include "sim/handler_entry.h"             // HandlerTable / HandlerRecord
#include "sim/entity.h"                    // PersonFindRecordById
#include "sim/he.h"                        // HeRecord
#include "util/util_recon.h"               // guild::util::ReconStrCmp

#include <cstddef>
#include <cstring>                         // std::strlen

namespace guild::sim {

namespace {

CommandQueue& Q()   { return *RealCommandQueue(); }
HandlerTable& HeT() { return *RealHandlerTable(); }

// =========================================================================
// BrawlHooks leaf adapters -> the shared real pool / queue.
// =========================================================================

// VIBE_Command_GetPacketStatusById(handle) @0x4939d4 — the in-flight swing packet
// status (nonzero == resolved). BrawlStep only consults it when +132 != -1.
int WcBrawlPacketStatus(i32 handle) {
    return Q().GetPacketStatusById(static_cast<u32>(handle));
}

// VIBE_He_FreeHandlerEntry(record) @0x4c6144 — release the handler record on the
// terminal (-2/-1) brawl states. Hook shape: void(HeRecord*).
void WcBrawlFreeHandlerEntry(HeRecord* h) {
    HeT().FreeHandlerEntry(reinterpret_cast<HandlerRecord*>(h));
}

// VIBE_Person_FindRecordById(id) @0x58bc6c — the victim Person record (or null).
void* WcBrawlFindPersonById(i32 id) {
    return reinterpret_cast<void*>(PersonFindRecordById(id));
}

// =========================================================================
// Recon2Hooks leaf adapters.
// =========================================================================

// VIBE_Util_StrLen — the original guard leaf is literally strlen (CmdPlayAnimation-
// Script's < 0x5F length gate). CRT strlen is the byte-faithful equivalent.
std::size_t WcRecon2StrLen(const char* s) { return std::strlen(s); }

// --- process-lifetime wired hook tables (the bridges reference these) -----------
// Recon2 stores the POINTER (no copy), so this must outlive the install.
character_recon2::Recon2Hooks g_recon2{};

}  // namespace

void InstallRealCombat2Wiring() {
    HeT();   // force the shared real He pool to exist (composes with real_hooks3)
    Q();     // force the shared real command queue to exist

    // ---- BrawlHooks (charaction_brawl.h, 0x4d201c) --------------------------
    // BrawlStep null-checks each leaf, but we seed from the inert defaults and
    // override only the three clean binds (mirrors wire_combat.cpp's seed style).
    BrawlHooks brawl = GetBrawlHooks();
    brawl.packetStatus     = &WcBrawlPacketStatus;       // 0x4939d4
    brawl.freeHandlerEntry = &WcBrawlFreeHandlerEntry;   // 0x4c6144
    brawl.findPersonById   = &WcBrawlFindPersonById;     // 0x58bc6c
    // aggressorRecord (word_12CE910 table addressing) / adjustRelationByMood
    // (0x56840c, needs the native aggressor record) / registerApEvent (0x4c703c) /
    // sendDefeatMessage (0x4c5c54 + family bump) / bumpVictimFamilyDefeats
    // (0x58c408) / restorePoseAndRequeue (qword_13CE852 saved-pose snapshot +
    // 0x4949c4 QueueRequestEntity29): folded/native-record engine leaves with no
    // single clean reconstructed target -> inert (same as wire_combat.cpp).
    static BrawlHooks s_brawl;          // SetBrawlHooks copies, but keep stable storage
    s_brawl = brawl;
    SetBrawlHooks(&s_brawl);

    // ---- Recon2Hooks (character_recon2_cmds.h, take/drop/look/ani Cmds) ------
    // SetRecon2Hooks stores the pointer (not a copy) and the defaults are all-null,
    // so g_recon2 has process lifetime and every bound field is set explicitly.
    g_recon2 = character_recon2::Recon2Hooks{};   // start from the all-null inert table
    g_recon2.strCmp = &guild::util::ReconStrCmp;  // VIBE_Util_StrCmp 0x5d3f10
    g_recon2.strLen = &WcRecon2StrLen;            // CRT strlen guard
    // reportError / queueInsertEntry / unlinkEntry / insertActionArgs /
    // insertActionVararg / createTake|DropObjectAction(Alt) / objectFindByHandle /
    // applyAttachOffset / attachItemToBone / preloadAniSet / applyVisibilityState /
    // objectSetWorldTranslation / objectChangeTransparency / angleToTargetSigned /
    // snapVectorToAxis / vectorAngleBetween / pointThroughBoneChain /
    // rotateVectorByHierarchy / coordConvertX / lightSetGrayColor / recordField /
    // sceneRecordOf / transportOf / readByte / strNCopyPad: deep scene-graph /
    // matrix-transform / object / action-queue-builder / script-error engine leaves
    // (strNCopyPad's only reconstruction @0x5d9360 is file-local in save_browser.cpp,
    // not linkable) -> inert (rule 8).
    character_recon2::SetRecon2Hooks(&g_recon2);

    // ---- ZERO-bindable bridges (nothing wired, by design) -------------------
    // CharQueryHooks (setVisible/worldToTile/terrainAt: render+heightmap),
    // CombatSlots3Hooks (GameObject iterators/window/widget/frame-loop),
    // CombatSlots4Hooks (screen-project/text-label/command/blood-pool mesh),
    // CheckHooks (personQueryBeginFlag90 == VIBE_Person_QueryBegin, rule 8; the two
    // Office predicates need Office records): every field is a render / heightmap /
    // window / widget / command / iterator / QueryBegin / office-record engine leaf
    // with no clean reconstructed target. They keep their inert defaults.
    (void)sizeof(CharQueryHooks);
    (void)sizeof(CombatSlots3Hooks);
    (void)sizeof(CombatSlots4Hooks);
    (void)sizeof(CheckHooks);
}

}  // namespace guild::sim
