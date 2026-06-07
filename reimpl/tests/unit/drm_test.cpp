// Unit tests for guild::drm — the intentional copy-protection stub.
//
// These verify that every public DRM entry point is callable and returns the
// success / "genuine disc present" value its original caller expected. There is
// no cryptographic behaviour to golden-vector here: the module is a documented
// stub (see src/drm/drm_stub.h). The contract under test is precisely the set of
// return sentinels the game checks against.
#include "drm/drm_stub.h"
#include "test.h"

using namespace guild;
using namespace guild::drm;

TEST(DrmStub, MainReportsAuthorized) {
    // 0 == authorized success path; the game must NOT take the 11 (no-device) path.
    CHECK_EQ(Main(0x400000, 0, 0, 1), kDrmMainAuthorized);
    CHECK(Main(0x400000, 0, 0, 1) != 11);
}

TEST(DrmStub, ProtectionMainCallable) {
    // VM trap handler is inert in the decrypted reconstruction; just callable.
    CHECK_EQ(ProtectionMain(), kDiscIoOk);
}

TEST(DrmStub, DiscVerifySetsGenuineFlag) {
    // Args were 32-bit pointers/handles in the original; the stub ignores them,
    // so plain placeholder integers exercise the contract.
    CHECK_EQ(VerifyDisc(1, 0x1000, 0, 2), static_cast<int>(kDiscGenuineFlag));

    int result = 0;
    int rv = DiscProtectVerifyDisc(1, 0u, 0, &result);
    CHECK_EQ(rv, static_cast<int>(kDiscGenuineFlag));
    CHECK_EQ(result, static_cast<int>(kDiscGenuineFlag));

    // out-pointer is optional; must not crash when null.
    CHECK_EQ(DiscProtectVerifyDisc(0, 0u, 0, nullptr),
             static_cast<int>(kDiscGenuineFlag));
}

TEST(DrmStub, SignatureChecksPass) {
    CHECK_EQ(VerifyDiscSignature(), kDiscGenuineFlag);
    CHECK(VerifyDiscSignature() != 0u);            // non-zero == genuine
    CHECK_EQ(VerifySectorChecksum(), kDiscIoOk);
    CHECK_EQ(CompareSignature(), 1);               // 1 == signatures equal
}

TEST(DrmStub, AuthAndOverlayAreNoOps) {
    // These return void; the test asserts they are callable and do not throw or
    // touch the (nonexistent) hardware/disc. Calling twice must be safe.
    RunAuthentication();
    RunAuthentication();
    DecryptOverlay();
    DecryptOverlay();
    ProbeDriveGeometry();
    SetSpeedParams();
    CHECK(true);
}

TEST(DrmStub, OverlayHelpersSucceed) {
    CHECK_EQ(DecryptKeyTable(), kDiscIoOk);
    CHECK_EQ(InitProtection(0, 0), kDiscIoOk);
}

TEST(DrmStub, HardwareProbesReportClean) {
    CHECK_EQ(MeasureSectorTiming(0), kDiscIoOk);
    CHECK_EQ(VerifyPlaybackDevice(0), kDiscIoOk);
    CHECK(EnumScsiDevices() != 0);                 // a drive "exists"
    CHECK_EQ(CheckMediaPresent(1), kMediaPresent);
    CHECK_EQ(ScanScsiDevices(), kDiscIoOk);
}

TEST(DrmStub, NoEmulatorDetected) {
    // The crucial anti-emulator check must report a clean physical drive (0),
    // otherwise the game would refuse to run under our (emulated) environment.
    CHECK_EQ(DetectVirtualDrive(), 0);
}
