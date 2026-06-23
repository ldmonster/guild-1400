#pragma once
#include "guild/common/types.h"

// command_recon4_resolve — the VIBE_Command_ResolveTarget* family of console /
// history target-resolution handlers (namespace guild::sim). Dispatched from the
// data table at gilde.exe 0x6343d8 by VIBE_History_ParseContext (0x4fd44c).
//
// Each handler shares one shape:
//   (kind a1@al, params a2@edx, name a3@ecx, index a4@ebx, out a5)
//   * StripNameTokens(name) folds '%' codes / strips up to two '-' tokens into a
//     scratch format string; the trailing integer (ParseInt) selects a "mode"
//     m in {0,1,2} (clamped: if token-count != 1 or value > 2 -> 1).
//   * kind==1  : confirm path — re-resolve a previously chosen person id taken
//                from params[index] (the 8*index+4 dword) via FindRecordById,
//                gate on record flags, then render. No table scan.
//   * kind==2  : (scoped/stat variants) reject — return 0.
//   * kind==0  : pick path — scan the 768-slot scene/person selection table for
//                a candidate matching the handler's predicate, write the chosen
//                entity id back into params[index] (8*index+4), then render.
//
// The 768-slot selection table (gilde.exe word_12CE910, 536-byte stride, 0x300
// records) plus the cross-module leaves (FindRecordById, ComputeTotalWealth,
// IsNotInSelectionList, RandomModulo, ParseInt, StripNameTokens, RenderMessage)
// are NOT reconstructed here — they live in already-reconstructed modules and in
// live game state — so they are routed through an installable hooks struct with
// inert defaults. The SCAN/SCORING/SELECTION control flow, the profession-range
// constants, the wealth comparison, the top-5 bubble-sort tie pool, the random
// start + stride iteration, and the back-write ARE reconstructed 1:1.

namespace guild::sim {

// ---------------------------------------------------------------------------
// Selection-table field accessors. The original reads each 536-byte record by
// fixed byte offset; we expose exactly the fields the resolvers touch:
//   typeWord  word_12CE910[268*i]  (+0x00)  -1 == empty slot
//   kind      byte_12CE912[536*i]  (+0x02)  status/type byte (people <10)
//   entityId  dword_12CE914[134*i] (+0x04)  game entity id (used in selection list)
//   alive     byte_12CE918[536*i]  (+0x08)  alive/active flag
//   flag9     dword_12CE919 LOBYTE (+0x09)  gender/role flag (ByName/Alt use ==1 / ==0)
//   prof74    byte_12CEA74[536*i]  (+0x164) profession class (CraftWorker / ProfRange)
//   prof76    byte_12CEA76[536*i]  (+0x166) office/role byte (ByName/Alt/Scoped/StatGroup)
//   prof79    byte_12CEA79[536*i]  (+0x169) clergy rank byte (Clergy: 0x1E..0x21)
// recId reads the record's +4 dword as the id back-written to params (== entityId).
// ---------------------------------------------------------------------------
struct Recon4Record {
    i16 typeWord = -1;
    u8  kind     = 0;
    i32 entityId = 0;
    u8  alive    = 0;
    u8  flag9    = 0;
    u8  prof74   = 0;
    u8  prof76   = 0;
    u8  prof79   = 0;
    // The "person record" base used by ComputeTotalWealth / sort-key reads in the
    // wealth handlers; opaque host pointer (only compared / passed through).
    void* personBase = nullptr;
};

struct Recon4ResolveHooks {
    // word_12CE910 et al. — read slot `i` (0..767). Return false if i out of range.
    bool (*readRecord)(int i, Recon4Record* out) = nullptr;

    // VIBE_Person_FindRecordById(id) -> opaque record base (or null). Used by the
    // kind==1 confirm path. The byte flags it gates on are exposed via the
    // recordFlag accessor below.
    void* (*findRecordById)(i32 id) = nullptr;
    // Read a single byte flag from a FindRecordById record at byte offset `off`
    // (+8 alive, +358 / +361 role bytes). Returns 0 if record null.
    u8   (*recordFlag)(void* rec, int off) = nullptr;
    // *(rec+4) — the entity id stored in a FindRecordById record (== first word
    // of the message-render call argument). Default 0.
    i32  (*recordTypeWord)(void* rec) = nullptr;

    // VIBE_Person_ComputeTotalWealth(slotIdx, scratch) -> wealth score.
    i32  (*computeTotalWealth)(u16 slotIdx) = nullptr;

    // VIBE_Entity_IsNotInSelectionList(entityId) -> nonzero if NOT selected.
    int  (*isNotInSelectionList)(i32 entityId) = nullptr;

    // VIBE_Math_RandomModulo(n) -> value in [0,n).
    int  (*randomModulo)(u16 n) = nullptr;

    // VIBE_Util_ParseInt over the name tail; the resolvers call StripNameTokens
    // first (folding the format string) and ParseInt over name[consumed..].
    // We collapse the pair into one hook that returns (tokenCount, value):
    //   tokenCount == *a3 result (count of stripped tokens)
    //   value      == ParseInt(name tail)
    // The format-string output (a5 render arg #2) is opaque; recorded via render.
    void (*parseTokens)(const char* name, int* tokenCount, i32* value) = nullptr;

    // VIBE_Text_RenderFormattedMessage(out, fmt, typeWord [, extra...]).
    // We record the chosen typeWord (the resolved candidate identity); `out` and
    // `fmt` are opaque. Called exactly where the original renders.
    void (*renderMessage)(char* out, const char* name, u16 typeWord) = nullptr;
};

void SetRecon4ResolveHooks(const Recon4ResolveHooks* h);
const Recon4ResolveHooks& GetRecon4ResolveHooks();

// params is the command parameter array base (a2); index (a4) selects the
// 8*index+4 dword slot the resolved id is written into (kind==0) or read from
// (kind==1). The originals pass it as raw `int`; we keep it as u8*.
//
// gilde.exe 0x4f9238 — VIBE_Command_ResolveTargetClergy
int ResolveTargetClergy(u8 kind, u8* params, const char* name, int index, char* out);
// gilde.exe 0x4f9518 — VIBE_Command_ResolveTargetPersonByName
int ResolveTargetPersonByName(u8 kind, u8* params, const char* name, int index, char* out);
// gilde.exe 0x4f989c — VIBE_Command_ResolveTargetPersonAlt
int ResolveTargetPersonAlt(u8 kind, u8* params, const char* name, int index, char* out);
// gilde.exe 0x4f9c20 — VIBE_Command_ResolveTargetPersonScoped
int ResolveTargetPersonScoped(u8 kind, u8* params, const char* name, int index, char* out);
// gilde.exe 0x4f9f74 — VIBE_Command_ResolveTargetBestRated
int ResolveTargetBestRated(u8 kind, u8* params, const char* name, int index, char* out);
// gilde.exe 0x4fa50c — VIBE_Command_ResolveTargetCraftWorker
int ResolveTargetCraftWorker(u8 kind, u8* params, const char* name, int index, char* out);
// gilde.exe 0x4fa818 — VIBE_Command_ResolveTargetByStatGroup
int ResolveTargetByStatGroup(u8 kind, u8* params, const char* name, int index, char* out);
// gilde.exe 0x4fac80 — VIBE_Command_ResolveTargetByProfessionRange
int ResolveTargetByProfessionRange(u8 kind, u8* params, const char* name, int index, char* out);
// gilde.exe 0x4faab8 — VIBE_Command_ResolveTargetWoundedPerson
int ResolveTargetWoundedPerson(u8 kind, u8* params, const char* name, int index, char* out);
// gilde.exe 0x4faf54 — VIBE_Command_ResolveTargetRandomCarried
int ResolveTargetRandomCarried(u8 kind, u8* params, const char* name, int index, char* out);

} // namespace guild::sim
