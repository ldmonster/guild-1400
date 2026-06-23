#include "sim/history_parse.h"

#include "sim/command_recon4_resolve.h"  // the 10 reconstructed ResolveTarget*
#include "world/city.h"                  // UtilParseInt (VIBE_Util_ParseInt @0x5dc070)
#include "world/history_full.h"          // kHistoryPrefixTable / kHistoryRoleNames (byte-exact)

#include <cstdio>
#include <cstring>

// history_parse — faithful port of VIBE_History_ParseContext (gilde.exe 0x4fd44c)
// plus the funcs_4FD5D2 dispatch table (gilde.exe 0x6343d8). The disassembly is
// the reference: the decompile's v17>>24 gymnastics are simply the byte locals
// var_14 (replacement index) / var_18 (prefix mode) read back sign-extended.

namespace guild::sim {

// ===========================================================================
// Hooks for the 5 unreconstructed slots (inert defaults: return 0).
// ===========================================================================
namespace {

// 0x4f8fac / 0x4f90e0 / 0x4fa178 / 0x4fa290 / 0x4fa3bc — not yet reconstructed.
int DefResolveSlot(u8, u8*, const char*, int, char*) { return 0; }

const HistoryParseHooks kHistDefaults = {
    &DefResolveSlot, &DefResolveSlot, &DefResolveSlot, &DefResolveSlot, &DefResolveSlot,
};

HistoryParseHooks g_histHooks = kHistDefaults;

// Trampolines so the table entries stay stable across SetHistoryParseHooks.
int HookGuard(u8 k, u8* p, const char* n, int i, char* o)        { return g_histHooks.resolveGuard(k, p, n, i, o); }
int HookOfficial(u8 k, u8* p, const char* n, int i, char* o)     { return g_histHooks.resolveOfficial(k, p, n, i, o); }
int HookBestThief(u8 k, u8* p, const char* n, int i, char* o)    { return g_histHooks.resolveBestThief(k, p, n, i, o); }
int HookSelectedStat(u8 k, u8* p, const char* n, int i, char* o) { return g_histHooks.resolveSelectedStat(k, p, n, i, o); }
int HookBuildingStat(u8 k, u8* p, const char* n, int i, char* o) { return g_histHooks.resolveBuildingStat(k, p, n, i, o); }

// ===========================================================================
// gilde.exe 0x6343d8 — funcs_4FD5D2 (15 entries, 4-byte stride). Recovered via
// get_bytes: ac8f4f00 e0904f00 38924f00 18954f00 9c984f00 209c4f00 749f4f00
//            78a14f00 90a24f00 bca34f00 0ca54f00 18a84f00 b8aa4f00 80ac4f00
//            54af4f00. Slots 0,1,7,8,9 -> hook trampolines (targets pending);
// the other 10 -> the reconstructed guild::sim::ResolveTarget*.
// ===========================================================================
const HistoryResolverFn kHistoryResolverTable[kHistoryResolverSlotCount] = {
    &HookGuard,                       //  0  0x4f8fac VIBE_Command_ResolveTargetGuard (pending)
    &HookOfficial,                    //  1  0x4f90e0 VIBE_Command_ResolveTargetOfficial (pending)
    &ResolveTargetClergy,             //  2  0x4f9238
    &ResolveTargetPersonByName,       //  3  0x4f9518
    &ResolveTargetPersonAlt,          //  4  0x4f989c
    &ResolveTargetPersonScoped,       //  5  0x4f9c20
    &ResolveTargetBestRated,          //  6  0x4f9f74
    &HookBestThief,                   //  7  0x4fa178 VIBE_Command_ResolveTargetBestThief (pending)
    &HookSelectedStat,                //  8  0x4fa290 VIBE_Command_ResolveTargetSelectedStat (pending)
    &HookBuildingStat,                //  9  0x4fa3bc VIBE_Command_ResolveTargetBuildingStat (pending)
    &ResolveTargetCraftWorker,        // 10  0x4fa50c
    &ResolveTargetByStatGroup,        // 11  0x4fa818
    &ResolveTargetWoundedPerson,      // 12  0x4faab8
    &ResolveTargetByProfessionRange,  // 13  0x4fac80
    &ResolveTargetRandomCarried,      // 14  0x4faf54
};

} // namespace

void SetHistoryParseHooks(const HistoryParseHooks* h) {
    if (!h) { g_histHooks = kHistDefaults; return; }
    g_histHooks = *h;
    if (!g_histHooks.resolveGuard)        g_histHooks.resolveGuard = kHistDefaults.resolveGuard;
    if (!g_histHooks.resolveOfficial)     g_histHooks.resolveOfficial = kHistDefaults.resolveOfficial;
    if (!g_histHooks.resolveBestThief)    g_histHooks.resolveBestThief = kHistDefaults.resolveBestThief;
    if (!g_histHooks.resolveSelectedStat) g_histHooks.resolveSelectedStat = kHistDefaults.resolveSelectedStat;
    if (!g_histHooks.resolveBuildingStat) g_histHooks.resolveBuildingStat = kHistDefaults.resolveBuildingStat;
}
const HistoryParseHooks& GetHistoryParseHooks() { return g_histHooks; }

HistoryResolverFn HistoryResolverSlot(int idx) {
    if (idx < 0 || idx >= kHistoryResolverSlotCount) return nullptr;
    return kHistoryResolverTable[idx];
}

// ===========================================================================
// gilde.exe 0x4fd44c — VIBE_History_ParseContext
//   (__usercall, eax = (token@eax, out@edx, params@ebx)).
// ===========================================================================
int HistoryParseContext(const char* token, char* out, u8* params) {
    char buf[256];           // v12 — ParseInt scratch / dead error-format buffer
    i32  slot = -1;          // v13   (var_24, init 0xFFFFFFFF @0x4fd45f)
    i8   repl = 15;          // var_14 (init 0x0F @0x4fd464/0x4fd478)
    u8   mode = 4;           // var_18 (init 4 @0x4fd46f/0x4fd47f)

    if (!token || !out)                                   // 0x4fd486..0x4fd490
        return 0;

    // 0x4fd49a..0x4fd4af — match the token's first dword against dword_6343B8
    // entries 0..2 ("_NEW"/"_USE"/"_REL", 5-byte stride). All 3 are compared;
    // a match sets the mode and the scan continues (loc_4FD607).
    for (int i = 0; i < 3; ++i) {
        if (std::strncmp(token, guild::world::kHistoryPrefixTable[i], 4) == 0)
            mode = static_cast<u8>(i);
    }

    std::memset(buf, 0, sizeof(buf));                     // 0x4fd4b1..0x4fd4d2

    const char* tail = token;                             // v3 (ebp)
    if (mode != 4) {                                      // 0x4fd4d4 jz loc_4FD51F
        // 0x4fd4de..0x4fd4f1 — the group row must exist and be marked valid.
        u32 groupValid = 0;
        if (params) std::memcpy(&groupValid, params, 4);
        if (!params || !groupValid) {
            std::snprintf(buf, sizeof(buf), "%s", kHistErrWrongGroup);  // 0x4fd61d (dead store)
            return 0;
        }
        // 0x4fd4f7..0x4fd507 — slot digit: v12[0] = token[5]; ParseInt(v12).
        buf[0] = token[5];
        slot = guild::world::UtilParseInt(buf);
        tail = token + 6;                                 // lea ebp,[esi+6]
        if (slot < 0 || slot >= 8) {                      // 0x4fd510/0x4fd519 (signed)
            std::snprintf(buf, sizeof(buf), kHistErrWrongSlot, slot);   // 0x4fd644 (dead store)
            return 0;
        }
    }

    if (mode == 1) {                                      // 0x4fd51f jz loc_4FD659
        // loc_4FD659 — _USE reads the role index byte back from the group row:
        // mov al, [params + 8*slot + 8].
        repl = static_cast<i8>(params[8 * slot + 8]);
    } else {
        // 0x4fd52d..0x4fd56e — scan the 15-entry role-name table (64-byte stride
        // @0x633ff8); the scan pointer is tail+1 (inc ebp skips the separator /
        // the leading '_' of a literal token). memcmp over strlen(name).
        const char* p = tail + 1;
        for (int i = 0; i < 15; ++i) {
            const char* name = guild::world::kHistoryRoleNames[i];
            size_t len = std::strlen(name);
            if (std::strncmp(p, name, len) == 0) {
                tail = p + len;                           // add ebp, strlen
                repl = static_cast<i8>(i);                // var_14 = bl
                if (mode == 0)                            // 0x4fd580 — _NEW writes it back
                    params[8 * slot + 8] = static_cast<u8>(i);  // mov [eax+8], bl
                break;
            }
        }
        // not found: repl stays 15 -> Unknown Replacement below (loc_4FD67B path).
    }

    if (repl >= 15) {                                     // 0x4fd598 (signed byte cmp)
        std::snprintf(buf, sizeof(buf), "%s", kHistErrUnknownRepl);     // 0x4fd697 (dead store)
        return 0;
    }
    if (repl < 0) {
        // Original quirk: a _USE row byte >= 0x80 sign-extends negative, passes
        // the `jge 15` gate and indexes funcs_4FD5D2 BEFORE the table — a wild
        // call (undefined behavior; the engine only ever stores 0..14 via _NEW).
        // Not reproducible; fail the token instead. Documented divergence.
        return 0;
    }

    // 0x4fd5a6..0x4fd5d2 — dispatch funcs_4FD5D2[repl]:
    //   al = mode, edx = params, ecx = tail, ebx = slot, stack = out.
    int result = kHistoryResolverTable[repl](mode, params, tail, slot, out);
    if (!result && params) {                              // 0x4fd5dd..0x4fd5ea
        u32 zero = 0;
        std::memcpy(params, &zero, 4);                    // *params = 0
    }
    return result;                                        // 0x4fd5ef
}

// ===========================================================================
// Rule-13 wiring: adapt ParseContext to the HistorySubstResolver hook consumed
// by guild::world::HistoryParseTextSecondPass (the reconstructed 0x4fdcec, the
// only original caller — call sites 0x4fdf1f / 0x4fe04e).
// ===========================================================================
guild::world::HistorySubstResolver MakeHistorySubstResolver(u8* params) {
    return [params](const std::string& tok) {
        guild::world::HistorySubstResult r;
        char text[1024];                 // the original's v48/v51 1024-byte scratch
        text[0] = '\0';
        r.ok = HistoryParseContext(tok.c_str(), text, params) != 0;
        text[sizeof(text) - 1] = '\0';
        r.text = text;
        return r;
    };
}

} // namespace guild::sim
