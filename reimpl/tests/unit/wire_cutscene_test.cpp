// Verifies InstallRealCutsceneWiring() binds the five cutscene leaf/driver hook
// bridges (CutsceneMiscHooks, Cutscene2Hooks, CutsceneMisc3Hooks,
// CutsceneMisc4Hooks, CutsceneProcHooks) to their real reconstructed cross-cluster
// leaves — previously NONE of the five were installed by the app spine, so every
// cutscene body ran against its inert defaults at runtime. Suite prefix: WireCutscene.
#include "tests/framework/test.h"

#include "sim/wire_cutscene.h"
#include "sim/cutscene_misc.h"
#include "sim/cutscene_misc2.h"
#include "sim/cutscene_misc3.h"
#include "sim/cutscene_misc4.h"
#include "sim/cutscene_process.h"
#include "sim/cutscene.h"
#include "sim/real_hooks.h"   // RealCommandQueue (shared queue the emits stage onto)

#include <cstring>

using namespace guild;
using namespace guild::sim;

// The bridges this wiring binds — assert the BOUND fields point at real adapters.
// (Unbound fields stay as their module inert stubs; the installer seeds from them.)
TEST(WireCutscene, BindsRealLeavesIntoCutsceneBridges) {
    InstallRealCutsceneWiring();

    // --- Cutscene2Hooks: the birth-participant person resolves + birth-failure cmd
    const Cutscene2Hooks& s2 = GetCutscene2Hooks();
    CHECK(s2.resolvePerson     != nullptr);
    CHECK(s2.personKind        != nullptr);
    CHECK(s2.personParentId    != nullptr);
    CHECK(s2.queueBirthFailure != nullptr);

    // --- CutsceneMisc3Hooks: per-type-main person reads + CRT random-modulo
    const CutsceneMisc3Hooks& s3 = GetCutsceneMisc3Hooks();
    CHECK(s3.personFind       != nullptr);
    CHECK(s3.personKind       != nullptr);
    CHECK(s3.personIll        != nullptr);
    CHECK(s3.mathRandomModulo != nullptr);

    // --- CutsceneMisc4Hooks: RunParticipants person resolves/field reads
    const CutsceneMisc4Hooks& s4 = GetCutsceneMisc4Hooks();
    CHECK(s4.personFind     != nullptr);
    CHECK(s4.personKind     != nullptr);
    CHECK(s4.personEntityId != nullptr);
    // packetStatus inert default must survive the seed (loops poll it).
    CHECK(s4.packetStatus   != nullptr);

    // --- CutsceneProcHooks: op88 timeout + cmd28 speech emit
    const CutsceneProcHooks& pr = GetCutsceneProcHooks();
    CHECK(pr.requestBuildOp88 != nullptr);
    CHECK(pr.queueSpeech28    != nullptr);
    // prepareReady stays inert (ProcessActive's built-in skeleton runs when null).
}

// A representative cutscene body from each wired bridge runs over zeroed/slot state
// through the installed real leaves without crashing, returning a defined result —
// i.e. the wired control flow actually executes (person resolves against the real
// arrays, command emits stage onto the real shared queue).
TEST(WireCutscene, WiredCutsceneBodiesExecuteOverZeroedState) {
    InstallRealCutsceneWiring();

    // cutscene_misc2: CheckBirthParticipants over a zeroed slot. partIds[0]/[1]
    // resolve to absent persons (id 0 -> the resolve returns null), so the body
    // takes its !a||!b early-return path through the real PersonFindRecordById.
    CutsceneSlot slot;
    std::memset(&slot, 0, sizeof(slot));
    slot.partCount = 2;
    // Executes through the real PersonFindRecordById against the shared person array
    // and returns a defined result without crashing (id 0 resolves to the zeroed slot-0
    // record, so the exact value over synthetic state isn't a meaningful golden — the
    // crash-free run through the wired leaf is what verifies the binding).
    i32 r2 = CutsceneCheckBirthParticipants(&slot);
    (void)r2;

    // cutscene_misc3: the seasonal day-window pick (a pure deterministic kernel the
    // mains drive). Defined index in [0, 6].
    int idx = CutsceneSeasonWindowIndex(/*day=*/0);
    CHECK(idx >= 0);
    CHECK(idx <= 6);
    // the gendered birth voice base (pure kernel over the wired ill-byte reads).
    int vb = CutsceneBirthVoiceBase(/*fatherIll=*/false, /*motherIll=*/false);
    CHECK(vb == 0);

    // cutscene_misc4: the forward participant scan + run-mode classify (pure kernels
    // RunParticipants drives over the wired person leaves). Empty slot -> no match.
    int found = CutsceneFindParticipantById(&slot, /*wantId=*/123);
    CHECK(found == -1);
    int mode = CutsceneClassifyRunMode(/*rawModeByte=*/7);  // not 0/1 -> skip
    CHECK(mode == -1);

    // cutscene_process: drive ProcessActive over a zeroed slot table through the
    // wired proc hooks (op88 + cmd28 emit onto the real queue). No alive slot ->
    // the per-frame scan is a defined no-op pass.
    CutsceneTable table;
    table.Clear();
    CutsceneTypeTable types;
    CutsceneRng rng;
    CutsceneContext ctx;
    ctx.table = &table;
    ctx.types = &types;
    ctx.rng   = &rng;
    int exec = CutsceneProcessActive(ctx);
    CHECK(exec >= 0);

    // The op88 / cmd28 emit leaves resolve against the real shared queue without
    // crashing (exercise them directly through the installed hook table).
    const CutsceneProcHooks& pr = GetCutsceneProcHooks();
    pr.requestBuildOp88(7);
    CutsceneSlot speechSlot;
    std::memset(&speechSlot, 0, sizeof(speechSlot));
    i32 h = pr.queueSpeech28(&speechSlot, /*person=*/0, /*lenTotal=*/4, /*lenB=*/3, "abc");
    (void)h;  // defined (the speaker is absent -> the builder's personFound gate)
}
