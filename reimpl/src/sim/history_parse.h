#pragma once
#include "guild/common/types.h"

#include "world/history_text_pass.h"   // HistorySubstResolver (the 0x4fdcec caller hook)

// history_parse — VIBE_History_ParseContext (gilde.exe 0x4fd44c) and its resolver
// dispatch table funcs_4FD5D2 (gilde.exe 0x6343d8), namespace guild::sim.
//
// ParseContext is the chronicle substitution-token resolver: it classifies a
// "_..." token by its leading 4-byte prefix (table dword_6343B8, 5-byte stride —
// "_NEW"/"_USE"/"_REL", no match -> literal mode 4), extracts the 0..7 slot digit
// (token[5], via VIBE_Util_ParseInt 0x5dc070), matches the role name against the
// 15-entry / 64-byte-stride table at 0x633ff8 (aBuergermeister_3), and dispatches
// the matching funcs_4FD5D2 slot:
//
//   slot  keyword (0x633ff8+64*i)   target      gilde.exe symbol
//   ----  ------------------------  ----------  -------------------------------------------
//     0   BUERGERMEISTER            HOOK        0x4f8fac VIBE_Command_ResolveTargetGuard
//     1   BISCHOF                   HOOK        0x4f90e0 VIBE_Command_ResolveTargetOfficial
//     2   RND_GILDENMEISTER         RECON       0x4f9238 VIBE_Command_ResolveTargetClergy
//     3   RND_AMTSTRAEGERIN         RECON       0x4f9518 VIBE_Command_ResolveTargetPersonByName
//     4   RND_AMTSTRAEGER           RECON       0x4f989c VIBE_Command_ResolveTargetPersonAlt
//     5   RND_AMTSPERSON            RECON       0x4f9c20 VIBE_Command_ResolveTargetPersonScoped
//     6   REICHSTER_EINWOHNER       RECON       0x4f9f74 VIBE_Command_ResolveTargetBestRated
//     7   BESTES_WIRTSHAUS          HOOK        0x4fa178 VIBE_Command_ResolveTargetBestThief
//     8   GELD                      HOOK        0x4fa290 VIBE_Command_ResolveTargetSelectedStat
//     9   STADTKASSE                HOOK        0x4fa3bc VIBE_Command_ResolveTargetBuildingStat
//    10   RND_KIRCHENBERUF          RECON       0x4fa50c VIBE_Command_ResolveTargetCraftWorker
//    11   RND_REICH                 RECON       0x4fa818 VIBE_Command_ResolveTargetByStatGroup
//    12   RND_NPC_EINWOHNER         RECON       0x4faab8 VIBE_Command_ResolveTargetWoundedPerson
//    13   RND_HANDELSHERR           RECON       0x4fac80 VIBE_Command_ResolveTargetByProfessionRange
//    14   RND_SPIELER               RECON       0x4faf54 VIBE_Command_ResolveTargetRandomCarried
//
// RECON slots dispatch straight into the reconstructed guild::sim::ResolveTarget*
// (src/sim/command_recon4_resolve.h). HOOK slots route through HistoryParseHooks
// (inert default: return 0) until their targets are reconstructed.
//
// (The data after slot 14 — 0x634414 onward, 0x4fb0f0/0x4fb180/... — is a separate
// adjacent table; ParseContext indexes funcs_4FD5D2 with 0..14 only.)
//
// The original signature is __usercall: eax = token, edx = out text buffer,
// ebx = params (the per-group slot row from ParseCommandlineFirstPass). The
// dispatched resolvers receive (kind@al = prefix mode, params@edx, name@ecx =
// token tail past the matched keyword, index@ebx = slot, out on the stack).

namespace guild::sim {

// Resolver signature shared with the VIBE_Command_ResolveTarget* family.
using HistoryResolverFn = int (*)(u8 kind, u8* params, const char* name,
                                  int index, char* out);

// ---------------------------------------------------------------------------
// Hooks for the 5 funcs_4FD5D2 slots whose targets are NOT yet reconstructed.
// Inert defaults return 0 ("token unresolved"), which makes ParseContext zero
// params[0] and fail the surrounding text pass — the safe no-op.
// ---------------------------------------------------------------------------
struct HistoryParseHooks {
    HistoryResolverFn resolveGuard        = nullptr;  // slot 0  — 0x4f8fac
    HistoryResolverFn resolveOfficial     = nullptr;  // slot 1  — 0x4f90e0
    HistoryResolverFn resolveBestThief    = nullptr;  // slot 7  — 0x4fa178
    HistoryResolverFn resolveSelectedStat = nullptr;  // slot 8  — 0x4fa290
    HistoryResolverFn resolveBuildingStat = nullptr;  // slot 9  — 0x4fa3bc
};

void SetHistoryParseHooks(const HistoryParseHooks* h);  // null -> defaults
const HistoryParseHooks& GetHistoryParseHooks();

// The reconstructed funcs_4FD5D2 table (gilde.exe 0x6343d8): returns the handler
// wired into slot idx (0..14; RECON slots are the real ResolveTarget* functions,
// HOOK slots are trampolines into HistoryParseHooks). Null for idx out of range.
HistoryResolverFn HistoryResolverSlot(int idx);
constexpr int kHistoryResolverSlotCount = 15;

// The failure messages the original formats (VIBE_Crt_Sprintf_0 into a dead
// 256-byte stack buffer — no observable side effect; kept for documentation).
inline constexpr const char* kHistErrWrongGroup =
    "hst_ParseContext() failed: Wrong group reference";        // 0x620890
inline constexpr const char* kHistErrWrongSlot =
    "hst_ParseContext() failed: Wrong slot reference %i";      // 0x6208c4
inline constexpr const char* kHistErrUnknownRepl =
    "hst_ParseContext() failed: Unknown Replacement";          // 0x6208f8

// gilde.exe 0x4fd44c — VIBE_History_ParseContext
//   (__usercall, eax = (token@eax, out@edx, params@ebx)).
// `token` is the full substitution token (starts at the '_'); `out` is the text
// buffer the dispatched resolver renders into; `params` is the group slot row:
//   *(u32*)params           : group-valid gate (must be non-zero for _NEW/_USE/_REL)
//   params[8*slot + 4..7]   : the resolved entity id (resolver back-write, kind 0)
//   params[8*slot + 8]      : the replacement role index byte (written by _NEW,
//                             read back by _USE)
// Returns the resolver's result; 0 on any parse failure (and a failed resolve
// zeroes *(u32*)params, exactly like the original).
int HistoryParseContext(const char* token, char* out, u8* params);

// ---------------------------------------------------------------------------
// Rule-13 wiring to the in-tree caller. ParseContext's only original callers are
// inside VIBE_History_ParseTextSecondPass (0x4fdcec, call sites 0x4fdf1f /
// 0x4fe04e), reconstructed as guild::world::HistoryParseTextSecondPass with a
// HistorySubstResolver hook. This factory produces that hook from the real
// ParseContext: each token is resolved into a 1024-byte text buffer (the
// original's v48/v51 scratch) against the given group slot row.
// ---------------------------------------------------------------------------
guild::world::HistorySubstResolver MakeHistorySubstResolver(u8* params);

} // namespace guild::sim
