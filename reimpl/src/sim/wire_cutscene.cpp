// See wire_cutscene.h. Binds the five cutscene leaf/driver hook bridges to their
// real reconstructed cross-cluster leaves. Glue only — no cutscene module logic.
//
// All command emits stage onto the SAME shared real CommandQueue that the rest of
// the sim wiring owns (RealCommandQueue(), real_hooks.h); the cmd28 speech builder
// also drives a process-lifetime real PendingState (the original's word_1077F60 /
// dword_11AA4A0 staging globals). Person resolves go through the real linear-scan
// PersonFindRecordById over the shared g_persons array (entity.h); the +2/+4/+9/+92
// field reads are byte-faithful to the recovered Person record layout (types.h)
// and to the originals these bodies translate (verified against the Hex-Rays of
// 0x4a7a64 CheckBirthParticipants / 0x4abf04 BuildSpeechPacket).
#include "sim/wire_cutscene.h"

#include "sim/cutscene_misc.h"      // CutsceneMiscHooks / Set/GetCutsceneMiscHooks
#include "sim/cutscene_misc2.h"     // Cutscene2Hooks / ...
#include "sim/cutscene_misc3.h"     // CutsceneMisc3Hooks / ...
#include "sim/cutscene_misc4.h"     // CutsceneMisc4Hooks / ...
#include "sim/cutscene_process.h"   // CutsceneProcHooks / ...
#include "sim/cutscene.h"           // CutsceneSlot

#include "sim/entity.h"             // PersonFindRecordById
#include "sim/types.h"              // Person (record layout)
#include "sim/real_hooks.h"         // RealCommandQueue()
#include "sim/command.h"            // CommandQueue
#include "sim/command_pending.h"    // PendingState
#include "sim/command_builders.h"   // RequestBuildOp88 / QueueRequestPair33
#include "sim/command_builders2.h"  // QueueRequestBuffer28
#include "sim/command_codec.h"      // QueueRequestCoord27
#include "util/math_random.h"       // util::RandomModulo

#include <cstring>

namespace guild::sim {

namespace {

CommandQueue& Q() { return *RealCommandQueue(); }

// The process-lifetime pending-staging state the cmd28 speech builder drives (the
// original's word_1077F60 / dword_11AA4A0 globals). One per process, like the
// engine's single staging region.
PendingState& Pending() {
    static PendingState s_pending;
    return s_pending;
}

// --- Person record field reads -------------------------------------------------
// The originals read the 536-byte Person record by raw byte offset; we mirror that
// exactly over the real record PersonFindRecordById returns.
inline u8  PersonByteAt(void* p, int off) {
    return p ? *(reinterpret_cast<const u8*>(p) + off) : 0;
}
inline i32 PersonDwordAt(void* p, int off) {
    return p ? *reinterpret_cast<const i32*>(reinterpret_cast<const u8*>(p) + off) : 0;
}

// VIBE_Person_FindRecordById(id) -> Person* (or null). Shared resolve.
void* WcPersonFind(i32 id) {
    return reinterpret_cast<void*>(PersonFindRecordById(id));
}
u8  WcPersonKind(void* p)       { return PersonByteAt(p, 2); }   // +0x02
u8  WcPersonIll(void* p)        { return PersonByteAt(p, 9); }   // +0x09 (gender/ill)
i32 WcPersonEntityId(void* p)   { return PersonDwordAt(p, 4); }  // +0x04 (id)
i32 WcPersonParentId(void* p)   { return PersonDwordAt(p, 92); } // +0x5C (relation[0])

// --- the CRT random-modulo leaf -------------------------------------------------
// VIBE_Math_RandomModulo(range) — uniform draw in [0, range).
u32 WcMathRandomModulo(u32 range) {
    return static_cast<u32>(util::RandomModulo(static_cast<u16>(range)));
}

// =========================================================================
// CutsceneProcHooks (cutscene_process.h) — ProcessActive command leaves.
// =========================================================================

// VIBE_Command_RequestBuildOp88(slotId) — opcode 88 "cutscene timed out" request,
// emitted by ProcessActive when a ready window expires with no step fn.
void WcRequestBuildOp88(i32 slotId) {
    RequestBuildOp88(Q(), slotId);
}

// VIBE_Cutscene_BuildSpeechPacket's tail emit (0x4abf04):
//   QueueRequestBuffer28(header248, lenTotal, lenB, body)
// The reconstructed proc hook surfaces it as queueSpeech28(slot, person, lenTotal,
// lenB, text). We assemble the 248-byte header carrying the speaker person id at
// +4 (the field the original sets as v21 = *(person+4), and the field
// QueueRequestBuffer28's personFound gate reads), stage the rich-text `text` as the
// body, and emit on the shared queue + real pending state. personFound is resolved
// through the real PersonFindRecordById(person), matching the original's
// early-out-with-(-1)-if-the-speaker-is-gone gate.
i32 WcQueueSpeech28(const CutsceneSlot* /*slot*/, i32 person,
                    u32 lenTotal, u32 lenB, const char* text) {
    u8 header[248];
    std::memset(header, 0, sizeof(header));
    header[4] = 17;                                  // v20[4] = 17 (the packet kind tag)
    *reinterpret_cast<i32*>(header + 8) = person;    // v21 = speaker person id (+8 dword slot)
    const char* body = text ? text : "";
    u16 bodyLen = static_cast<u16>(lenTotal);        // header carries the total body length
    const bool personFound = (PersonFindRecordById(person) != nullptr);
    (void)lenB;                                      // lenB is the body's NUL-trimmed length tag
    return QueueRequestBuffer28(Q(), Pending(), header, body, bodyLen, personFound);
}

// =========================================================================
// Cutscene2Hooks (cutscene_misc2.h) — birth-participant command + person leaves.
// =========================================================================

// CheckBirthParticipants (0x4a7a64): QueueRequestPair33(person.id, 1) when the
// parent is gone / deceased (kind 15). The hook carries the person id directly.
void WcQueueBirthFailure(i32 personId) {
    QueueRequestPair33(Q(), personId, 1);
}

// resolvePerson / personKind / personParentId — the three person leaves
// CheckBirthParticipants uses (verified offsets +2 kind, +92 parent).
void* WcResolvePerson(i32 id)        { return WcPersonFind(id); }
u8    WcCs2PersonKind(void* p)       { return WcPersonKind(p); }
i32   WcCs2PersonParentId(void* p)   { return WcPersonParentId(p); }

// =========================================================================
// CutsceneMisc3Hooks (cutscene_misc3.h) — per-type-main person + random leaves.
// =========================================================================
void* WcM3PersonFind(i32 id)   { return WcPersonFind(id); }
u8    WcM3PersonKind(void* p)  { return WcPersonKind(p); }   // +2
u8    WcM3PersonIll(void* p)   { return WcPersonIll(p); }    // +9

// =========================================================================
// CutsceneMisc4Hooks (cutscene_misc4.h) — RunParticipants person leaves.
// =========================================================================
void* WcM4PersonFind(i32 id)       { return WcPersonFind(id); }
u8    WcM4PersonKind(void* p)      { return WcPersonKind(p); }     // +2
i32   WcM4PersonEntityId(void* p)  { return WcPersonEntityId(p); } // +4

// --- process-lifetime wired hook tables (the global hook ptr references these) ---
CutsceneMiscHooks  g_misc{};
Cutscene2Hooks     g_misc2{};
CutsceneMisc3Hooks g_misc3{};
CutsceneMisc4Hooks g_misc4{};
CutsceneProcHooks  g_proc{};

}  // namespace

void InstallRealCutsceneWiring() {
    Q();  // force the shared real command queue to exist

    // --- CutsceneMiscHooks (cutscene_misc.h) ---------------------------------
    // Every leaf this table carries is a rule-3/4/5 boundary: Script_FindByHandle/
    // Finish (script VM, not reconstructed standalone), Character_ToggleAniPlayback
    // (render anim), VoiceQueue_FlushAll / Music_PlayCutsceneTrack / Music_Restore
    // (rule-5 audio), TimeBase_Register/Unregister (the frame-loop tick registry),
    // Object_SetEnabled/SetValue (rule-4 widget). No clean reconstructed callable ->
    // all stay inert. disabledGate stays 0 (the not-replay default). Seed-from-
    // defaults so the (all-null) inert table is the installed table — the bodies
    // null-check each leaf, so an inert install is faithful & crash-safe.
    g_misc = GetCutsceneMiscHooks();
    SetCutsceneMiscHooks(&g_misc);

    // --- Cutscene2Hooks (cutscene_misc2.h) -----------------------------------
    // Bind the CheckBirthParticipants person resolves + the birth-failure command;
    // the rest (pumpFrame, Script_*, Fade_*/Sky_* presentation, duel windows,
    // buildSpeechPacket [render+voice coupled], sceneTeardown, restoreBroadcast
    // [net packet wait]) are rule-3/4/5 / process-global -> inert.
    g_misc2 = GetCutscene2Hooks();
    g_misc2.resolvePerson     = &WcResolvePerson;
    g_misc2.personKind        = &WcCs2PersonKind;
    g_misc2.personParentId    = &WcCs2PersonParentId;
    g_misc2.queueBirthFailure = &WcQueueBirthFailure;
    SetCutscene2Hooks(&g_misc2);

    // --- CutsceneMisc3Hooks (cutscene_misc3.h) -------------------------------
    // Bind the person resolve/field reads (Death/Birth inheritance + ill-byte voice
    // base) and the CRT random-modulo (Birth geschrei burst). Everything else
    // (pumpFrame, all audio voiceLoadBank/PlaySample/music, scene/sky/fade/rain,
    // script load/alive/finish, runTimedScript, showMessageBox/familyTree/reload/
    // dialog/setupCallbacks, distributeInheritance/countAdultChildren, sumCurrency)
    // is a rule-3/4/5 boundary or a process-global subsystem -> inert.
    g_misc3 = GetCutsceneMisc3Hooks();
    g_misc3.personFind       = &WcM3PersonFind;
    g_misc3.personKind       = &WcM3PersonKind;
    g_misc3.personIll        = &WcM3PersonIll;
    g_misc3.mathRandomModulo = &WcMathRandomModulo;
    SetCutsceneMisc3Hooks(&g_misc3);

    // --- CutsceneMisc4Hooks (cutscene_misc4.h) -------------------------------
    // GetCutsceneMisc4Hooks() dereferences the installed pointer directly (no inert
    // fallback once a non-null is set) AND its packetStatus default must survive
    // (the LeaseWindow/RunParticipants wait loops poll it), so SEED from the module
    // inert table first, then bind only the person resolves/field reads. The rest
    // (pumpFrame, net queueFlagBlob/runWaitLoop/stagePendingBlock/requestSendCutInfo/
    // packetStatus/refreshGuildState, ui banner/participant table/dialog/light,
    // runTypeCallback, leaseRunRentSlider [Widget+Form], salonFadeOut/LoadScene/
    // ReregisterFade) are rule-3/4/5 / process-global -> inert (keep defaults).
    g_misc4 = GetCutsceneMisc4Hooks();
    g_misc4.personFind     = &WcM4PersonFind;
    g_misc4.personKind     = &WcM4PersonKind;
    g_misc4.personEntityId = &WcM4PersonEntityId;
    SetCutsceneMisc4Hooks(&g_misc4);

    // --- CutsceneProcHooks (cutscene_process.h) ------------------------------
    // Bind the op88 timeout request and the cmd28 speech emit to the real command
    // builders on the shared queue. prepareReady stays inert: ProcessActive's
    // built-in master/participant readiness SKELETON runs when it is null (the full
    // net protocol is a host override, not a standalone reconstructed leaf).
    g_proc = GetCutsceneProcHooks();
    g_proc.requestBuildOp88 = &WcRequestBuildOp88;
    g_proc.queueSpeech28    = &WcQueueSpeech28;
    SetCutsceneProcHooks(&g_proc);
}

}  // namespace guild::sim
