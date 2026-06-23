#pragma once
// guild::play — Theatre "stage an event" menu (cutscene_recon2 cluster).
//
// gilde.exe 0x536a30 — VIBE_Theatre_RunEventMenu (__usercall, edi = arg0).
//
// The theatre lets the player trigger one of ten staged "events" (a wedding, a
// death, a bankruptcy, an execution, a tenancy, a birth, a plague, a fire, a storm,
// a duel). The routine:
//   * opens the "misc\theatre" form, centres its children, selects window 0;
//   * registers ten menu entries via VIBE_Text_RenderRichString("%ia[<Event>]$N")
//     -> VIBE_Form_GetChildObjectId, capturing each entry's child-object id;
//   * runs the game frame loop (VIBE_GameLogic_RunFrameLoop(415687,...)); each
//     turn, if a child was clicked (dword_62D22C == one of the captured ids) and a
//     selection target exists (dword_75BF38 != -1), it assembles a cutscene/command
//     record for that event and dispatches it (VIBE_Cutscene_ExecMainFunc or the
//     VIBE_Command_QueueRequest* path);
//   * on exit destroys the form.
//
// The load-bearing reconstruction here is the EVENT DISPATCH: the per-button event
// codes, the participant-gathering strides, and the RNG-driven date offsets. The
// form/render/command side effects are inert hooks (UI is out-of-scope here; the
// cutscene/command records they build are the testable artifact).
//
// Event codes written into the cutscene record's "kind" byte (v35):
//   Hochzeit(wedding)=6  Tod(death)=8  Pleite(bankruptcy)=9  Hinrichtung(exec)=5
//   Pacht(tenancy)=10    Geburt(birth)=7  Duell(duel)=4
// Disaster events go through the command-queue record's "kind" byte (v50):
//   Pest(plague)=89  Brand(fire)=78  Sturm(storm)=80
//
// Person-record stride: dword_12CE914 is the person/master table; the entry stride
// is 134 dwords (536 bytes). byte_12CEA76 is its "type/role" byte column (offset
// 0x162 == 354 from the dword base; indexed as byte_12CEA76[idx*4] over dwords, or
// byte_12CEA76[idx] over the *byte* stride 536). word_12CE910 is the id column;
// word_63CC5C is the local player's master index.

#include "guild/common/types.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace guild::play {

using guild::i32;
using guild::u8;
using guild::u16;

// --- Form-build constants. ---
inline constexpr const char* kTheatreFormPath = "misc\\theatre";
inline constexpr int kTheatreFrameLoopTag = 415687;   // VIBE_GameLogic_RunFrameLoop arg

// The ten menu entries, in build order, with their event "kind" codes. The string
// is the %ia[...]$N rich-string template fed to VIBE_Text_RenderRichString.
enum class TheatreEvent {
    Wedding,      // %ia[Hochzeit]$N   code 6
    Death,        // %ia[Tod]$N        code 8
    Bankruptcy,   // %ia[Pleite]$N     code 9
    Execution,    // %ia[Hinrichtung]$N code 5
    Tenancy,      // %ia[Pacht]$N      code 10
    Birth,        // %ia[Geburt]$N     code 7
    Plague,       // %ia[Pest]$N       code 89 (disaster path)
    Fire,         // %ia[Brand]$N      code 78 (disaster path)
    Storm,        // %ia[Sturm]$N      code 80 (disaster path)
    Duel,         // %ia[Duell]$N      code 4
};

// Event "kind" code written into the cutscene record for the non-disaster events,
// matching the constants in the decompile (v35).
inline constexpr u8 kTheatreCodeExecution  = 5;   // Hinrichtung
inline constexpr u8 kTheatreCodeWedding    = 6;   // Hochzeit
inline constexpr u8 kTheatreCodeBirth      = 7;   // Geburt
inline constexpr u8 kTheatreCodeDeath      = 8;   // Tod
inline constexpr u8 kTheatreCodeBankruptcy = 9;   // Pleite
inline constexpr u8 kTheatreCodeTenancy    = 10;  // Pacht
inline constexpr u8 kTheatreCodeDuel       = 4;   // Duell
// Disaster "kind" codes written into the command-queue record (v50).
inline constexpr u8 kTheatreCodePlague     = 89;  // Pest
inline constexpr u8 kTheatreCodeFire       = 78;  // Brand
inline constexpr u8 kTheatreCodeStorm      = 80;  // Sturm

// Person-table geometry (dword_12CE914 family).
inline constexpr int kPersonStrideDwords = 134;     // 536-byte record stride
inline constexpr int kPersonTableDwords  = 102912;  // loop bound (768 records * 134)
inline constexpr int kPersonStrideBytes  = 536;     // 0x218
inline constexpr int kPersonTableBytes   = 411648;  // 768 * 536

// A game-time triple (qword_13CE852 family): the current world clock the records
// snapshot. We model it as {day, hourPacked, extra} so the date-offset math is
// faithful without pinning the original packed-qword layout.
struct TheatreClock {
    i32 packedLo = 0;   // qword_13CE852 low dword
    i32 packedHi = 0;   // qword_13CE852 high dword (WORD2 == months-ish, compared vs 0x17)
    i32 word85A  = 0;   // unk_13CE85A
    i32 word85E  = 0;   // unk_13CE85E
    i32 word85C  = 0;   // unk_13CE85C
};

// The cutscene-event record assembled for the non-disaster events (the stack block
// v34..v48 handed to VIBE_Cutscene_ExecMainFunc). Only the fields the dispatch sets
// are modeled; offsets noted from the ebp-relative slots.
struct TheatreCutsceneRecord {
    u8  kind = 0;          // v35  (+8)   : event code (5/6/7/8/9/10)
    u8  subKind = 0;       // v41[0] (+0x26): 32 normally, 34 for tenancy/duel
    u8  count = 0;         // LOBYTE(v42) (+0x30): participant count / mode
    i32 targetId = -1;     // v37  (+0x14): selected person id (-1 == none)
    i32 master = 0;        // v36/v43 (+0xC/+0x34): local master id
    i32 master2 = 0;       // v44  (+0x38): second master / partner id
    i32 master3 = 0;       // v45  (+0x3C)
    i32 master4 = 0;       // v46  (+0x40)
    i32 extra = 0;         // v47  (+0x7C): random/role index or partner id
    i32 amount = 0;        // v48  (+0x80): money amount (e.g. 16000 for tenancy)
    std::vector<i32> ids;  // gathered participant ids (the v42[] tail)
};

// The disaster-event record assembled for Pest/Brand/Sturm (the stack block
// v49..v58 handed to VIBE_Command_QueueRequestSlotReset28 / op via the command
// queue). Mirrors v50..v58.
struct TheatreDisasterRecord {
    u8  kind = 0;          // v50  (+0x10? slot): 89/78/80
    i32 srcId = 0;         // v51  : *(dword_6498E4+4) (the issuing master)
    i32 target = -1;       // v52  : -1
    u8  subKind = 0;       // v56[0]: 1 (fire) / 2 (plague/storm)
    i32 v57 = -1;          // v57  : -1, or 3*rand(3) for plague
    i32 v58 = 0;           // v58  : duration (2000 fire / 1000 storm)
    // The date offset applied via VIBE_GameTime_Advance(rec, months, ?, days):
    i32 advanceDays   = 0; // v13  : 30*rand(2)
    i32 advanceMonths = 0; // v25  : rand(4) (+7 when clock.hi WORD2 >= 0x17)
};

// Inert hooks for the engine surface the menu drives. UI/command side effects are
// out-of-scope; the records above are the reconstructed, testable output.
struct TheatreHooks {
    // RNG: VIBE_Math_RandomModulo(n) -> [0,n). Deterministic injection for tests.
    std::function<u16(u32 n)> randMod;
    // Person query: VIBE_Person_QueryBegin(...) -> pointer-ish id record. We return
    // a person id (or -1 / 0 when none) so the records can be populated.
    std::function<i32(int roleA, int roleB, int city)> personQuery;
};

// --- Pure helpers (the load-bearing logic), testable in isolation. ---

// gilde.exe 0x536e70-0x536ea4 — gather up to 4 ids whose role byte is in {4,5,6,7}
// from the person table (stride 536, capped at v21<32 i.e. 8 dword slots / 768
// records). `roleBytes[i]`/`idCol[i]` are the role and id columns indexed per
// record. Returns the gathered ids (the v42[] tail) and sets `count` (v20).
std::vector<i32> Theatre_GatherTenancyParticipants(const std::vector<u8>& roleBytes,
                                                   const std::vector<i32>& idCol,
                                                   int& count);

// gilde.exe 0x537275-0x5372f7 — gather up to ~7 ids of valid masters other than the
// local player (id != -1, role byte nonzero, id != localMaster). Capped at v29<8.
//
// IMPORTANT (1:1, fixed wave-16): the binary uses TWO distinct columns here:
//   v32 = word_12CE910[268*rec]                 -> the COMPARE key (id word)
//   *(...&v42 + v29) = dword_12CE914[134*rec]    -> the STORED value (master dword)
// The id-word column gates the pick (id != -1, role nonzero, id != localMaster) but
// the value pushed into the participant tail is the dword_12CE914 master column, NOT
// the id word. The 5-arg form takes both columns; the 4-arg form keeps the historical
// behavior of using `idCol` for both (compare == store) for callers/tests where the
// two columns coincide.
std::vector<i32> Theatre_GatherDuelParticipants(const std::vector<i32>& idCol,
                                                const std::vector<u8>& roleBytes,
                                                const std::vector<i32>& masterCol,
                                                i32 localMaster, int& count);
std::vector<i32> Theatre_GatherDuelParticipants(const std::vector<i32>& idCol,
                                                const std::vector<u8>& roleBytes,
                                                i32 localMaster, int& count);

// gilde.exe 0x536c44-0x536c8c & co — find the person record whose role byte equals
// `role` (first match), returning its master/id dword (dword_12CE914[idx]); returns
// `fallback` (the dword at index 0) if none found before the table end. This is the
// wedding-event partner lookup for roles 13/15/14.
i32 Theatre_FindByRole(const std::vector<u8>& roleBytes, const std::vector<i32>& dwords,
                       u8 role, i32 fallback);

// gilde.exe 0x536fde-0x537216 / 0x5371a7-0x5371d3 — compute the disaster date
// offset. Past month 0x17 the event is pushed roughly half a year out:
//   days   = 30 * rand(2)
//   months = rand(4)        (clock.hi WORD2 <  0x17)
//   months = rand(4) + 7    (clock.hi WORD2 >= 0x17)
void Theatre_ComputeDisasterOffset(const TheatreHooks& h, int clockMonthWord,
                                   i32& outDays, i32& outMonths);

// Build the cutscene record for one of the non-disaster events. `localMaster` is
// word_63CC5C; `masterDword` is dword_12CE914[134*localMaster]. Returns false for
// disaster events (use BuildDisasterRecord instead).
//
// `weddingPartnerMaster` carries the wedding-only v44 = dword_12CE914[134*localMaster
// + 402] slot (a SEPARATE person record's master dword, +1608 bytes from the player's
// record — not derivable from `masterDword`). It defaults to `masterDword` for the
// other events that set v44 = v36 (Birth) or leave it unused.
bool Theatre_BuildCutsceneRecord(TheatreEvent ev, i32 localMaster, i32 masterDword,
                                 i32 partnerDword, const TheatreHooks& h,
                                 TheatreCutsceneRecord& out,
                                 i32 weddingPartnerMaster);
bool Theatre_BuildCutsceneRecord(TheatreEvent ev, i32 localMaster, i32 masterDword,
                                 i32 partnerDword, const TheatreHooks& h,
                                 TheatreCutsceneRecord& out);

// Build the disaster record (Plague/Fire/Storm). `srcMaster` is *(dword_6498E4+4).
bool Theatre_BuildDisasterRecord(TheatreEvent ev, i32 srcMaster, int clockMonthWord,
                                 const TheatreHooks& h, TheatreDisasterRecord& out);

} // namespace guild::play
