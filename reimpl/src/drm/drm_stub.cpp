#include "drm/drm_stub.h"

// =============================================================================
// guild::drm — stub implementations of the copy-protection cluster.
//
// Every function here is an INTENTIONAL no-op that returns the success /
// "genuine disc present" value its original caller checked for, so the game
// proceeds exactly as it would on an authentic disc. See drm_stub.h for the
// full rationale and the overlay-decryption finding (PLAN open-question #1).
//
// Nothing in this file touches the OS, SCSI/ASPI, a disc, or any decryption:
// there is no hardware and no encrypted payload in this preservation build, and
// the reconstructed game .text is already plaintext.
// =============================================================================

namespace guild::drm {

// gilde.exe 0x1414b20 — VIBE_Drm_Main
int Main(int /*hInstance*/, int /*prevInstance*/, int /*cmdLine*/, int /*showCmd*/) {
    // Original ran the whole protection bring-up then fell into the game.
    // Stub: authorized — proceed.
    return kDrmMainAuthorized;
}

// gilde.exe 0x1411260 — VIBE_Drm_ProtectionMain
int ProtectionMain() {
    // Original: opcode-VM trap handler. No VM in the decrypted reconstruction;
    // never reached. Return the generic success sentinel for completeness.
    return kDiscIoOk;
}

// gilde.exe 0x140c3a0 — VIBE_Drm_VerifyDisc
int VerifyDisc(int /*driveHandle*/, int /*outSig*/, int /*keyTable*/, int /*fileHandle*/) {
    return static_cast<int>(kDiscGenuineFlag);
}

// gilde.exe 0x1417dd0 — VIBE_DiscProtect_VerifyDisc
int DiscProtectVerifyDisc(int /*a1*/, u32 /*a2*/, int /*a3*/, int* outResult) {
    if (outResult)
        *outResult = static_cast<int>(kDiscGenuineFlag);
    return static_cast<int>(kDiscGenuineFlag);
}

// gilde.exe 0x141c720 — VIBE_CopyProtect_VerifyDiscSignature
u32 VerifyDiscSignature() {
    // Original returned (off_145A018 & 0x40000): non-zero == genuine.
    return kDiscGenuineFlag;
}

// gilde.exe 0x141c980 — VIBE_CopyProtect_VerifySectorChecksum
int VerifySectorChecksum() {
    // Original exited the process on mismatch; report a match instead.
    return kDiscIoOk;
}

// gilde.exe 0x1411eb0 — VIBE_Drm_CompareSignature
int CompareSignature() {
    // 1 == signatures equal / valid.
    return 1;
}

// gilde.exe 0x1419eb0 — VIBE_DiscProtect_RunAuthentication
void RunAuthentication() {
    // Nothing to authenticate.
}

// gilde.exe 0x140bc10 — VIBE_Drm_DecryptOverlay
void DecryptOverlay() {
    // Reconstruction is already plaintext (see drm_stub.h finding). No-op.
}

// gilde.exe 0x140ec90 — VIBE_Drm_DecryptKeyTable
int DecryptKeyTable() {
    return kDiscIoOk;
}

// gilde.exe 0x140fa60 — VIBE_Drm_InitProtection
int InitProtection(int /*a1*/, int /*a2*/) {
    return kDiscIoOk;
}

// gilde.exe 0x14139e0 — VIBE_Drm_MeasureSectorTiming
int MeasureSectorTiming(int /*a1*/) {
    return kDiscIoOk;
}

// gilde.exe 0x1411f20 — VIBE_Drm_VerifyPlaybackDevice
int VerifyPlaybackDevice(int /*a1*/) {
    return kDiscIoOk;
}

// gilde.exe 0x1412120 — VIBE_Disc_EnumScsiDevices
int EnumScsiDevices() {
    // Pretend a suitable drive exists (non-zero).
    return kDiscIoOk;
}

// gilde.exe 0x14136f0 — VIBE_Disc_CheckMediaPresent
int CheckMediaPresent(int /*driveHandle*/) {
    return kMediaPresent;
}

// gilde.exe 0x141e6a0 — VIBE_CopyProtect_DetectVirtualDrive
int DetectVirtualDrive() {
    // 0 == no emulator / virtual drive detected (the "clean" result).
    return 0;
}

// gilde.exe 0x141c540 — VIBE_CopyProtect_ProbeDriveGeometry
void ProbeDriveGeometry() {
}

// gilde.exe 0x141c3b0 — VIBE_CopyProtect_SetSpeedParams
void SetSpeedParams() {
}

// gilde.exe 0x141e0d0 — VIBE_CopyProtect_ScanScsiDevices
int ScanScsiDevices() {
    return kDiscIoOk;
}

} // namespace guild::drm
