#pragma once
// Law (Gesetz) flow: enact-law request, apply-and-notify (law-table mutation),
// the (office-type, op) record lookup, and the save/load serialization of the
// law-threshold + crime + evidence tables. Faithful 1:1 port of the
// VIBE_Gesetz_* flow functions from gilde.exe.
//
// The law table itself is g_lawTable (law.h / law.cpp; gilde.exe unk_631E98,
// 36 B x 26). The threshold field the enact path mutates is dword_631EB0[9*id]
// == LawRecord::threshold (record byte +24). The crime/evidence tables are
// g_crimeTable / g_evidence* (crime.h). Mutations route through the same command
// hook style as the office module (RequestBuildOp70); save/load route through a
// byte-stream the caller supplies (the live engine uses VIBE_Vfs_*Stream).
//
// Translated functions:
//   VIBE_Gesetz_RequestApply       0x4c247c
//   VIBE_Gesetz_ApplyAndNotify     0x4c24f8
//   VIBE_Gesetz_FindRecordByPair   0x4c258c
//   VIBE_Gesetz_SaveState          0x4c25e0
//   VIBE_Gesetz_LoadState          0x4c28d8
#include <cstddef>

#include "guild/common/types.h"
#include "world/law_types.h"

namespace guild::world {

// ---------------------------------------------------------------------------
// Command hook (mock) — RequestApply commits the enact through build-op 70.
// ---------------------------------------------------------------------------
struct GesetzCommand {
    int opcode;     // 70
    i32 masterId;   // packed initiator id (-1 == "the city/no person")
    u8  lawId;      // law id (v7)
    i32 value;      // clamped enact value (v8)
};
using GesetzCommandHook = void (*)(const GesetzCommand& cmd, void* ctx);
void GesetzSetCommandHook(GesetzCommandHook hook, void* ctx);
void GesetzCommandLogReset();
const GesetzCommand* GesetzCommandLog(int* outCount);

// History notify hook (ApplyAndNotify fires NotifyLawChangeToMaster off-path).
using GesetzNotifyHook = void (*)(i32 masterId, u8 lawId, i32 newThreshold,
                                  void* ctx);
void GesetzSetNotifyHook(GesetzNotifyHook hook, void* ctx);
void GesetzNotifyLogReset();
struct GesetzNotifyEvent { i32 masterId; u8 lawId; i32 newThreshold; };
const GesetzNotifyEvent* GesetzNotifyLog(int* outCount);

// ---------------------------------------------------------------------------
// gilde.exe 0x4c247c — VIBE_Gesetz_RequestApply  (eax=person rec, dl=lawId, ebx=value).
// Clamps `value` into [record.penalty, record.threshold] (v5[1]/v5[2] == the law
// record's +4/+8 dwords) and emits the enact command. `person` is the initiator
// record view (valid + ownerId); a null/invalid person uses id -1. Returns the
// command result (0) on emit, -1 on a bad law id / invalid person.
struct GesetzPerson {
    i32  ownerId = -1; // +4
    bool valid   = false; // *a1 != 0xFFFF and a1 != 0
    bool present = false; // a1 != 0 (null pointer path -> id -1, still emits)
};
int GesetzRequestApply(u8 lawId, int value, const GesetzPerson& person);

// gilde.exe 0x4c24f8 — VIBE_Gesetz_ApplyAndNotify  (eax=packed apply cmd).
// Writes the new threshold into the law table (dword_631EB0[9*id]) and notifies
// the master if the initiator is not the local master and the initiator resolves.
//   cmd[+0] initiator id (dword)   cmd[+4] law id (byte)   cmd[+5] new threshold (dword)
// `localMasterId` models dword_12CE914[..] (the local player's office account).
// Returns 1 on apply (even when no notify), 0 if lawId >= 26.
struct GesetzApplyCmd { i32 initiatorId; u8 lawId; i32 newThreshold; };
int GesetzApplyAndNotify(const GesetzApplyCmd& cmd, i32 localMasterId,
                         bool initiatorResolves);

// gilde.exe 0x4c258c — VIBE_Gesetz_FindRecordByPair  (eax=person rec, edx=op).
// Scans the 26 law records for the first whose +28 byte (byte_631EB4) equals the
// person's +358 held office-type AND whose +29 byte (byte_631EB5) equals `op`.
// Copies the 36-byte record into *out and returns 1; 0 if none. `personOffice358`
// is the person record's +358 byte.
int GesetzFindRecordByPair(u8 personOffice358, u8 op, LawRecord* out);

// ---------------------------------------------------------------------------
// Save / load: a minimal sequential byte stream models VIBE_Vfs_*Stream.
// ---------------------------------------------------------------------------
struct GesetzStream {
    u8* data = nullptr;
    std::size_t size = 0;
    std::size_t pos  = 0;
};
// Writes `n` bytes; returns true on success (room available). Mirrors
// VIBE_Vfs_WriteStream returning nonzero on success.
bool GesetzStreamWrite(GesetzStream& s, const void* src, std::size_t n);
bool GesetzStreamRead(GesetzStream& s, void* dst, std::size_t n);

// gilde.exe 0x4c25e0 — VIBE_Gesetz_SaveState. Serializes (in order):
//   i32 lawCount(26); 26 x i32 law thresholds (dword_631EB0[9*i]); i32 marker
//   (dword_632240); i32 activeCrimeCount; i32 activeEvidenceCount; then each
//   active crime record's 9 fields; then each active evidence pair. Returns 1 on
//   success, 0 on any stream failure / null stream.
int GesetzSaveState(GesetzStream& s);

// gilde.exe 0x4c28d8 — VIBE_Gesetz_LoadState. Reverses SaveState; returns 1 on
// success, 0 on a header mismatch / stream failure. Reads back the law
// thresholds and replays the crime/evidence tables (clearing the remainder).
// `formatVersion` models dword_649D4C (>= 0x10041 => the count-prefixed crime/
// evidence section is present).
int GesetzLoadState(GesetzStream& s, u32 formatVersion);

} // namespace guild::world
