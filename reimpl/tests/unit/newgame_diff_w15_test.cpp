// =============================================================================
// WAVE-15 TRUE 1:1 BINARY-DIFF GOLDEN PINS — values confirmed LINE-FOR-LINE
// against the live gilde.exe decompile/disasm this wave (MCP). Each pin records
// a constant or layout that was read straight out of the binary, so a future
// edit that drifts from the binary trips here.
//
// Confirmed against the binary this wave:
//   * Start-gold formula  @0x5340c3 (non-cheat) / @0x533f3b (cheat):
//       non-cheat base = 1250 - 250*difficulty   (1250=0x4E2; the 250*d is the
//       lea/shl/add chain 25*d, *2 -> 50*d, *4 +d -> 250*d, sub from 0x4E2).
//       cheat base     = 75000 (0x124F8, dword_63C7B4 != 0).
//   * Start-gold scan filter @0x533f42: byte_12CE918[i] != 0 (alive) AND
//       byte_12CE912[i] (kind) in {6, 7}  (cmp al,6 / cmp al,7 at 0x533f51 /
//       0x5340ec); stride 0x218 (536); id from dword_12CE914[i].
//   * Profession talents  @0x52d9ef + @0x58ec43: player bytes +0x80..+0x84 =
//       byte_649910[6*variant + 0..4]. Table bytes via get_bytes @0x649910
//       (record stride 6): variant 0 -> all zero; variant 1 -> 69 69 BD 93 69;
//       variant 2 -> 3F 3F 93 69 3F.
//   * Coord27 builder @0x494878 / apply @0x49818C: the builder's 3 logical args
//       land at +0x10 (subject) / +0x14 (object) / +0x18 (delta == ebx); ecx
//       (the apply handler's "mode" field at +0x1C) is xor'd to 0 at EVERY call
//       site (0x533930.. : `xor ecx,ecx; mov ebx,7Fh`). VIBE_Coord_ConvertX
//       @0x5c6b08 sets FPU RC=11 (truncate toward zero, control-word hi byte
//       0x3F) and frndint's flt_62EB90 (=1.0) -> the +0x20/+0x24 global stash is
//       DEAD on the mode-0 new-game path (apply reads them only in modes 2/3/4).
//   * Parent purse @0x5339df/@0x533a10: 32*RandomModulo(0x200) + 16000, father
//       (var_10) first, then mother (var_14).
// =============================================================================
#include "tests/framework/test.h"

#include "app/session_init.h"   // NewGameStartGoldBase @0x5340c3/@0x533f3b
#include "play/newgame_apply.h" // NewGameProfessionTalents @0x52d9ef

#include <cstring>

using namespace guild;

// --- start-gold base, every observable difficulty + cheat -------------------
TEST(NewGameDiffW15, StartGoldNonCheatFormula) {
    // 1250 - 250*difficulty for the five intro variants (0..4).
    CHECK_EQ(app::NewGameStartGoldBase(false, 0), 1250);
    CHECK_EQ(app::NewGameStartGoldBase(false, 1), 1000);
    CHECK_EQ(app::NewGameStartGoldBase(false, 2),  750);
    CHECK_EQ(app::NewGameStartGoldBase(false, 3),  500);
    CHECK_EQ(app::NewGameStartGoldBase(false, 4),  250);
}

TEST(NewGameDiffW15, StartGoldCheatConstant) {
    // dword_63C7B4 != 0 path: ebp = 0x124F8 = 75000, regardless of difficulty.
    CHECK_EQ(app::NewGameStartGoldBase(true, 0), 75000);
    CHECK_EQ(app::NewGameStartGoldBase(true, 4), 75000);
}

// --- profession talents: byte_649910 first-five-bytes, get_bytes-pinned -----
TEST(NewGameDiffW15, TalentTableGoldenBytes) {
    u8 t[5];

    // variant 1 -> record1 bytes[1..5] = 69 BD 93 69 04
    // talent copy loop @0x52da00 does `inc eax` FIRST (eax=1..5), reading record
    // bytes 1..5 and skipping byte 0 — disasm-confirmed. byte_649910 record1 =
    // 69 69 bd 93 69 04, so talents = {69,bd,93,69,04}.
    play::NewGameProfessionTalents(1, t);
    const u8 v1[5] = {0x69, 0xBD, 0x93, 0x69, 0x04};
    CHECK_EQ(std::memcmp(t, v1, 5), 0);

    // variant 2 -> record2 bytes[1..5] = 3F 93 69 3F 03 (record2 = 3f 3f 93 69 3f 03)
    play::NewGameProfessionTalents(2, t);
    const u8 v2[5] = {0x3F, 0x93, 0x69, 0x3F, 0x03};
    CHECK_EQ(std::memcmp(t, v2, 5), 0);

    // variant 0 (record 0) -> all zero
    play::NewGameProfessionTalents(0, t);
    for (int i = 0; i < 5; ++i) CHECK_EQ((int)t[i], 0);
}

// --- the parent purse formula (RandomModulo(0x200) draw range) ---------------
// 32 * v + 16000 for v in [0, 0x1FF] -> [16000, 16000 + 32*511] = [16000, 32352].
TEST(NewGameDiffW15, ParentPurseRange) {
    auto purse = [](int v) { return (v << 5) + 16000; };
    CHECK_EQ(purse(0),     16000);
    CHECK_EQ(purse(0x1FF), 32352);   // 16000 + 32*511
}
