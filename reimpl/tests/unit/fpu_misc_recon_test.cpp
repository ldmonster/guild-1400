// Golden tests for VIBE_Fpu cluster (fpu_misc_recon).
#include "tests/framework/test.h"
#include "util/fpu_misc_recon.h"

using namespace guild;
using guild::util::FpuState;
using guild::util::Fpu_DetectNanType;
using guild::util::Fpu_Init;
using guild::util::Fpu_InstallSaveRestore;
using guild::util::FpuSaveFn;
using guild::util::FpuRestoreFn;

TEST(MiscReconFpu, DetectNanTypeIsThreeOnIeee) {
    // -inf != +inf holds on IEEE hardware -> 3.
    CHECK_EQ(Fpu_DetectNanType(), static_cast<u8>(3));
}

TEST(MiscReconFpu, InitFirstTimeResetsAndReturnsZero) {
    FpuState st{0, 1, 0}; // not initialised, hooks wanted, no external owner
    int r = Fpu_Init(st);
    CHECK_EQ(r, 0);
    CHECK_EQ(st.wantHooks, static_cast<u8>(0)); // cleared on first init
    CHECK_EQ(st.initialised, static_cast<u8>(0)); // !externalOwner -> reset to 0
}

TEST(MiscReconFpu, InitFirstTimeKeepsFlagsWhenExternalOwner) {
    FpuState st{0, 1, 1}; // externalOwner set
    int r = Fpu_Init(st);
    CHECK_EQ(r, 0);
    CHECK_EQ(st.wantHooks, static_cast<u8>(0)); // still cleared
    // externalOwner path does not re-zero initialised inside the inner branch.
}

TEST(MiscReconFpu, InitAlreadyInitialisedReturnsFlagInByte1) {
    FpuState st{1, 0, 0};
    int r = Fpu_Init(st);
    CHECK_EQ(r, 1 << 8); // BYTE1(result) = initialised
}

TEST(MiscReconFpu, InstallSaveRestoreWiresHooksWhenWanted) {
    FpuState st{0, 1, 0};
    FpuSaveFn save = nullptr;
    FpuRestoreFn restore = nullptr;
    u8 nan = Fpu_InstallSaveRestore(st, &save, &restore);
    CHECK(save != nullptr);
    CHECK(restore != nullptr);
    CHECK_EQ(nan, static_cast<u8>(3)); // returns DetectNanType
}

TEST(MiscReconFpu, InstallSaveRestoreSkipsHooksWhenNotWanted) {
    FpuState st{0, 0, 0};
    FpuSaveFn save = nullptr;
    FpuRestoreFn restore = nullptr;
    Fpu_InstallSaveRestore(st, &save, &restore);
    CHECK(save == nullptr);
    CHECK(restore == nullptr);
}
