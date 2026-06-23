#pragma once
#include "guild/common/types.h"

#include "gui/text/textdb.h"
#include "shim/IFileSystem.h"

#include <cstddef>

// gilde.exe — first-name / dynasty-name string tables (namespace guild::sim).
//
// SUBSYSTEM: the three runtime first-name pointer tables read by the person /
// new-game code:
//
//   dword_8C400C  male first names      (191 entries, RandomModulo(0xBF))
//   dword_8C4320  female first names    (112 entries, RandomModulo(0x70))
//   dword_8C4508  dynasty / family-female names (149 entries, RandomModulo(0x95))
//
// These are NOT separate arrays: they are fixed-index *slices* of the one global
// localized-text pointer table `dword_8C36B0` (the "text array", modeled by
// guild::gui::text::TextDb). The literal table addresses prove it — relative to
// the canonical text base 0x8C36B0:
//
//   dword_8C400C = &dword_8C36B0[599]   ((0x8C400C-0x8C36B0)/4)
//   dword_8C4320 = &dword_8C36B0[796]   ((0x8C4320-0x8C36B0)/4)
//   dword_8C4508 = &dword_8C36B0[918]   ((0x8C4508-0x8C36B0)/4)
//
// (The disassembly of VIBE_Text_LoadTextFile @0x44dba0 stores each loaded string
// at `dword_8C36AC[baseId+1+i] == dword_8C36B0[baseId+i]`, the +4/-4 alias of the
// same array — so all three constants above are slots of `dword_8C36B0`.)
//
// READERS (the live call tree that consumes these slices):
//   VIBE_Person_CreateAndSpawn          @0x58da70  (0x58e368 male / 0x58f0fc female /
//                                                   0x58ee7b dynasty-female)
//   VIBE_Command_EnqueueInheritanceTransfer @0x5336f0 (parent first names;
//                                                   ParentFirstName in newgame_apply)
//   VIBE_GameLogic_CleanupTurnHandlers  @0x530e50  (NPC rename: male @0x530f6a,
//                                                   female @0x531063)
//   VIBE_MeisterAi_SelectMoodColor      @0x4664d8  (male @0x4665d7, female @0x466684)
//
// LOADER (how the slots fill — there is NO bespoke name loader; the names ship as
// ordinary text-resource strings):
//   VIBE_App_InitEngineAndScriptCommands @0x528560 builds "\project\game\gilde_text.def"
//     and calls VIBE_Text_LoadDefinitionFile @0x44b8f4, which parses the def's
//     quoted ("label" "file.ext") line pairs and, per entry, calls
//     VIBE_Text_LoadTextFile @0x44dba0 — opening "textbin_<lang>\\<file>.res"
//     through the VFS (a member of the shipped textbin_<lang>.BIN PKZIP archive)
//     and appending its strings into the global text array dword_8C36B0 at the
//     file's declared baseIndex. The first names live in `Text_C_Personen.res`.
//
// So the faithful reconstruction = load Text_C_Personen.res into a TextDb (via the
// already-reconstructed guild::gui::text::BuildTextArray) and read the slices at
// ids 599/796/918. The string CONTENT is whatever the install's textbin archive
// ships; the indices are fixed in the binary.
//
// The shipped Russian/patched install resolves the default Language="german" to
// `Resources/textbin_deutsch.BIN`, whose Text_C_Personen.res declares baseIndex
// 272 (so the male slice id 599 == file string index 327). The loader reads the
// baseIndex from the .res header, so it is correct for whichever textbin archive
// the host resolves.

namespace guild::sim {

using guild::u8;

// Slice anchors into the global text array dword_8C36B0 (FIXED in the binary).
inline constexpr int kNameMaleBase    = 599;   // dword_8C400C
inline constexpr int kNameMaleCount   = 191;   // RandomModulo(0xBF)
inline constexpr int kNameFemaleBase  = 796;   // dword_8C4320
inline constexpr int kNameFemaleCount = 112;   // RandomModulo(0x70)
inline constexpr int kNameDynastyBase = 918;   // dword_8C4508
inline constexpr int kNameDynastyCount = 149;  // RandomModulo(0x95)

// The .res member of the textbin archive that carries the first names.
inline constexpr const char* kNameResMember = "Text_C_Personen.res";

// Gender selector matching PersonCreateHooks::firstName / the three slices:
//   0 = male (dword_8C400C), 1 = female (dword_8C4320), 2 = dynasty (dword_8C4508).
enum NameKind : int { kNameMale = 0, kNameFemale = 1, kNameDynasty = 2 };

// Reset the loaded tables (drops the TextDb). Idempotent.
void NameTables_Reset();

// True once a Text_C_Personen.res has been parsed in and the slices are populated.
bool NameTables_Loaded();

// Parse a compiled Text_C_Personen.res buffer (the .res binary format handled by
// guild::gui::text::BuildTextArray) and populate the name slices. Returns true on
// a well-formed buffer that spans the name id ranges. Mirrors the runtime effect
// of VIBE_Text_LoadTextFile for this one file.
bool NameTables_LoadFromResBuffer(const u8* data, std::size_t len);

// Open the textbin archive `binPath` through `fs`, extract `Text_C_Personen.res`
// by name (the faithful VIBE_Zip_LocateFileByName + OpenCurrentFile path), and
// load it. Returns true on success. This is the runtime load path mirror for
// hosts that have the real install but no VFS directory mounts wired.
bool NameTables_LoadFromArchive(guild::shim::IFileSystem* fs, const char* binPath);

// NameAt(kind, index) — the slice read the original performs:
//   dword_8C400C[index] / dword_8C4320[index] / dword_8C4508[index].
// `index` is the already-drawn (RandomModulo) table index. Returns the string, or
// "" when the tables are not loaded or the index/slot is out of range/empty
// (matching the inert "" default the hooks fall back to).
const char* NameAt(int kind, int index);

// Convenience wrappers (the exact draw widths the call sites use).
inline const char* MaleNameAt(int index)    { return NameAt(kNameMale, index); }
inline const char* FemaleNameAt(int index)  { return NameAt(kNameFemale, index); }
inline const char* DynastyNameAt(int index) { return NameAt(kNameDynasty, index); }

// Access the underlying TextDb (for tests / advanced callers). Null until loaded.
const guild::gui::text::TextDb* NameTables_Db();

} // namespace guild::sim
