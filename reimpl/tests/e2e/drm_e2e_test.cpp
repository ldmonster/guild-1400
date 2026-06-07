// End-to-end test for guild::drm.
//
// Replays the copy-protection check sequence the game performs at startup, in the
// order VIBE_Drm_Main (@0x1414b20) runs it, and asserts the whole flow reports
// "authorized" so control would fall through into the game. This is the behaviour
// we must preserve for the stub to be correct: an authentic-disc run never exits.
#include "drm/drm_stub.h"
#include "test.h"

using namespace guild;
using namespace guild::drm;

// Mirrors the original startup gate. In gilde.exe a failed check called
// VIBE_Crt_Exit(N) (terminating the process); here a failure means the
// corresponding stub returned a non-success value, which we surface as `false`.
static bool RunStartupProtectionSequence() {
    // 1. Protection bring-up: load/resolve, open device, decrypt overlay, install
    //    VM. In the original these run inside Main(); the overlay/VM steps are
    //    no-ops in the decrypted reconstruction.
    DecryptOverlay();
    DecryptKeyTable();

    // 2. Confirm the disc device + media are usable.
    if (VerifyPlaybackDevice(0) == 0) return false;
    if (EnumScsiDevices() == 0) return false;
    if (CheckMediaPresent(1) != kMediaPresent) return false;

    // 3. Anti-emulation: the drive must look physical (0 == clean).
    if (DetectVirtualDrive() != 0) return false;

    // 4. Drive/speed setup before timing measurement.
    SetSpeedParams();
    ProbeDriveGeometry();
    ScanScsiDevices();

    // 5. The actual disc-authenticity gate (Main calls these in this order).
    if (VerifyDiscSignature() == 0u) return false;            // Crt_Exit(7) on fail
    if (VerifySectorChecksum() == 0) return false;            // Crt_Exit(8) on fail

    int discResult = 0;
    if (DiscProtectVerifyDisc(1, 0u, 0, &discResult) == 0) return false;
    if (discResult != static_cast<int>(kDiscGenuineFlag)) return false;

    if (VerifyDisc(1, 0x1000, 0, 2) == 0) return false;

    // 6. Sector-timing fingerprint + signature compare.
    if (MeasureSectorTiming(0) == 0) return false;
    RunAuthentication();
    if (CompareSignature() != 1) return false;

    return true;
}

TEST(DrmE2E, StartupSequenceReportsAuthorized) {
    CHECK(RunStartupProtectionSequence());
}

TEST(DrmE2E, MainFallsThroughToGame) {
    // Whole-protection entry: Main() returns 0 (authorized) and never the
    // no-device path (11), so CRT startup continues into the game.
    int rc = Main(0x400000, 0, 0, 1);
    CHECK_EQ(rc, kDrmMainAuthorized);
    CHECK(rc != 11);
}

TEST(DrmE2E, SequenceIsRepeatable) {
    // Re-running the gate (e.g. a periodic re-check) must keep reporting authorized
    // and must not accumulate state.
    for (int i = 0; i < 3; ++i)
        CHECK(RunStartupProtectionSequence());
}
