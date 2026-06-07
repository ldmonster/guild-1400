// tests/unit/slice_church_test.cpp — the CHURCH/RELIGION (donation) slice on a
// SYNTHETIC world (no assets). Proves:
//   * ClassifyChurchInteraction classifies the "Spenden" interaction and
//     ChurchCommand::Encode packs the REAL opcode-15 wire image
//     (EnqueueCmd15 0x494604 staged-buffer byte layout),
//   * the donation runs through the REAL sim::CommandQueue codec + the REAL
//     opcode-15 apply (ExRemapObjectPair 0x496978): currency moves donor->church
//     in the folded object money fields (object+77),
//   * the step sequence kSeed -> kCommand -> kDay: seed is the baseline, command
//     moves the world hash (the donation), the day moves it further,
//   * the whole sequence is DETERMINISTIC (identical per-step hashes across reruns).
#include "test.h"

#include "play/slice_church.h"
#include "play/world_digest.h"
#include "sim/command.h"
#include "sim/command_apply2.h"
#include "sim/entity.h"

#include <cstring>

using namespace guild;
using namespace guild::play;

namespace {

ChurchInteraction DefaultInteraction() {
    ChurchInteraction it;
    it.action       = ChurchAction::kDonate;
    it.churchId     = 5000;
    it.donorAccount = 6000;
    it.amount       = 250;
    it.currencyType = 0;
    return it;
}

} // namespace

// ---------------------------------------------------------------------------
// Classifier + packet encoder: the REAL opcode-15 wire layout.
// ---------------------------------------------------------------------------
TEST(SliceChurchUnit, ClassifyEmitsOpcode15WithRealLayout) {
    ChurchInteraction it = DefaultInteraction();
    ChurchCommand cmd = ClassifyChurchInteraction(it);

    CHECK(cmd.issued);
    CHECK_EQ((int)cmd.opcode, 15);                 // VIBE_Command_EnqueueCmd15 / jump[15]
    CHECK_EQ(cmd.churchId, 5000);
    CHECK_EQ(cmd.donorId, 6000);
    CHECK_EQ(cmd.amount, 250);
    CHECK_EQ((int)cmd.currency, 0);

    // The on-wire image: [0]=15, [+0x10]=church(dst) LE, [+0x14]=donor(src) LE,
    // [+0x1C]=currency byte, [+0x1D]=amount LE.
    sim::CommandPacket pkt = cmd.Encode();
    CHECK_EQ((int)pkt.bytes[0], 15);
    CHECK_EQ((int)pkt.get32(kDonationDstOff), 5000);
    CHECK_EQ((int)pkt.get32(kDonationSrcOff), 6000);
    CHECK_EQ((int)pkt.bytes[kDonationTypeOff], 0);
    i32 amt = 0;
    std::memcpy(&amt, pkt.bytes + kDonationAmtOff, 4);
    CHECK_EQ(amt, 250);
}

TEST(SliceChurchUnit, ClassifyRejectsNonDonationAndZeroAmount) {
    ChurchInteraction none = DefaultInteraction();
    none.action = ChurchAction::kNone;
    CHECK(!ClassifyChurchInteraction(none).issued);
    CHECK_EQ((int)ClassifyChurchInteraction(none).opcode, 0);

    ChurchInteraction zero = DefaultInteraction();
    zero.amount = 0;
    CHECK(!ClassifyChurchInteraction(zero).issued);
}

// ---------------------------------------------------------------------------
// The full step sequence: seed -> command (donation moves money) -> day.
// ---------------------------------------------------------------------------
TEST(SliceChurchUnit, StepSequencingMovesDonationThenDay) {
    ChurchStepHash h[3];
    i64 churchBefore = -1, churchAfter = -1;
    int n = RunChurchStepsSynthetic(/*seed=*/0x2468, DefaultInteraction(),
                                    h, 3, &churchBefore, &churchAfter);
    CHECK_EQ(n, 3);

    // The real apply credited the church money field (0 -> +amount).
    CHECK_EQ((long long)churchBefore, 0LL);
    CHECK_EQ((long long)churchAfter, 250LL);

    CHECK_EQ((int)h[0].step, (int)ChurchStep::kSeed);
    CHECK(!h[0].mutated);                  // seed is the baseline
    CHECK_EQ((int)h[1].step, (int)ChurchStep::kCommand);
    CHECK(h[1].mutated);                   // the opcode-15 apply moved the world hash
    CHECK_EQ((int)h[2].step, (int)ChurchStep::kDay);
    CHECK(h[2].mutated);                   // the game-day evolved the world further

    CHECK(h[2].hashAfter != h[0].hashAfter);
}

// ---------------------------------------------------------------------------
// Determinism: identical per-step hashes across two independent runs.
// ---------------------------------------------------------------------------
TEST(SliceChurchUnit, StepSequenceIsDeterministic) {
    ChurchStepHash a[3], b[3];
    RunChurchStepsSynthetic(0x1111, DefaultInteraction(), a, 3, nullptr, nullptr);
    RunChurchStepsSynthetic(0x1111, DefaultInteraction(), b, 3, nullptr, nullptr);
    for (int i = 0; i < 3; ++i)
        CHECK_EQ((long long)a[i].hashAfter, (long long)b[i].hashAfter);

    // A different seed yields a different evolved world (the day diverges).
    ChurchStepHash c[3];
    RunChurchStepsSynthetic(0x2222, DefaultInteraction(), c, 3, nullptr, nullptr);
    CHECK(c[2].hashAfter != a[2].hashAfter);
}

// ---------------------------------------------------------------------------
// Apply directly moves the expected folded object money fields donor->church
// through the REAL opcode-15 dispatch (CommandQueue + ExRemapObjectPair).
// ---------------------------------------------------------------------------
TEST(SliceChurchUnit, ApplyMovesCurrencyDonorToChurch) {
    sim::ResetEntityArrays();
    sim::ResetApply2State();

    ChurchInteraction it = DefaultInteraction();
    SeatChurchObject(it.donorAccount, /*money=*/1000);
    SeatChurchObject(it.churchId,     /*money=*/0);

    CHECK_EQ((long long)ReadObjectMoney(it.donorAccount), 1000LL);
    CHECK_EQ((long long)ReadObjectMoney(it.churchId), 0LL);

    sim::CommandQueue q;
    q.Init();
    InstallChurchCommandHandler(q);
    ChurchCommand cmd = ClassifyChurchInteraction(it);
    CHECK(cmd.issued);
    sim::CommandPacket pkt = cmd.Encode();
    i32 slot = q.EnqueuePacket(pkt);
    CHECK(slot >= 0);
    q.FlushSendQueue();
    q.ExecCommands();

    // The donation moved exactly `amount` donor -> church.
    CHECK_EQ((long long)ReadObjectMoney(it.donorAccount), 750LL);   // 1000 - 250
    CHECK_EQ((long long)ReadObjectMoney(it.churchId), 250LL);       // 0 + 250
}
