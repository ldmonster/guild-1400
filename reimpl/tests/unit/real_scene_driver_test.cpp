// Unit tests for src/app/real_scene_driver.cpp — the real scene/script driver,
// exercised over SMALL SYNTHETIC in-format bytes (no real assets needed).
//
// Two synthetic surfaces:
//   1) A synthetic .esc SOURCE script driven through DriveScriptSource: the real
//      strip+compile pass (sim::LoadScriptFromSource) parses it, the StepAllActive
//      context-table stepper runs main once, and the registered command is
//      invoked — asserting the parse/register/step counts deterministically.
//   2) A synthetic .esc BINARY-token blob driven through DriveBinaryTokenParse:
//      the reconstructed EscReadToken reader walks the byte stream and stops on
//      the EOF (0x2B) / error (0x27) control codes.
#include "test.h"

#include "app/real_scene_driver.h"
#include "sim/script_import.h"   // kEscTokEof / kEscTokError / kEscMaxToken

#include <string>
#include <vector>

using namespace guild;
using namespace guild::app;

// A tiny well-formed .esc source: a main() that evaluates a command argument and
// fires a command. Mirrors the shape of the real cutscene "create" scripts.
static const char* kSyntheticEsc =
    "int main()\n"
    "{\n"
    "  int h;\n"
    "  h = GetObjectHandle(1);\n"
    "  CreateCharacterAtDummy(h, 2);\n"
    "}\n";

TEST(RealSceneDriverUnit, DriveSyntheticSourceParsesAndSteps) {
    SetRealSceneDriverHooks(nullptr);   // inert command bodies (return 1)
    std::vector<std::string> cmds = {"GetObjectHandle", "CreateCharacterAtDummy"};

    RealSceneDriverResult r =
        DriveScriptSource("synthetic.esc", kSyntheticEsc, cmds, /*stepTicks=*/3);

    CHECK(r.assetsPresent);
    CHECK_EQ(r.escMembers, 1);
    CHECK_EQ(r.scriptsParsed, 1);     // strip+compile succeeded
    CHECK_EQ(r.scriptsWithMain, 1);   // has a main() entry
    CHECK_EQ(r.parseErrors, 0);
    // StepAllActive ran the runnable context exactly once (main completes, then
    // the slot clears its runnable flag -> later ticks no-op).
    CHECK_EQ(r.stepsExecuted, 1);
    // Both registered commands were invoked by the VM during the step.
    CHECK_EQ(r.commandsRegistered, 2);
}

// A script with no main() compiles but is not steppable (scriptsWithMain==0,
// stepsExecuted==0). The strip+compile pass still succeeds.
TEST(RealSceneDriverUnit, NoMainCompilesButDoesNotStep) {
    SetRealSceneDriverHooks(nullptr);
    const char* helperOnly = "int helper() { Sleep(1); }\n";
    RealSceneDriverResult r =
        DriveScriptSource("helper.esc", helperOnly, {"Sleep"}, 3);

    CHECK_EQ(r.scriptsParsed, 1);
    CHECK_EQ(r.scriptsWithMain, 0);
    CHECK_EQ(r.stepsExecuted, 0);
    CHECK_EQ(r.commandsRegistered, 0);   // main never ran -> no command fired
}

// An installed hook overrides the command body return value; the VM still steps.
TEST(RealSceneDriverUnit, InstalledHookOverridesCommandBody) {
    static int g_calls = 0;
    g_calls = 0;
    RealSceneDriverHooks h{};
    h.invokeCommand = [](const std::string&, std::vector<i32>&) -> i32 {
        ++g_calls; return 7;
    };
    SetRealSceneDriverHooks(&h);

    RealSceneDriverResult r = DriveScriptSource(
        "synthetic.esc", kSyntheticEsc,
        {"GetObjectHandle", "CreateCharacterAtDummy"}, 3);
    SetRealSceneDriverHooks(nullptr);

    CHECK_EQ(r.scriptsParsed, 1);
    CHECK_EQ(r.stepsExecuted, 1);
    CHECK(g_calls >= 2);              // the host forwarded to the installed body
}

// The .esc BINARY-token reader walks a synthetic blob until a control code.
// Bytes <= 0x3A pass through as tokens; 0x2B is EOF, 0x27 is error/end.
TEST(RealSceneDriverUnit, DriveBinaryTokenParseStopsAtEof) {
    // 4 in-range tokens, then EOF (0x2B).
    std::vector<u8> blob = {0x05, 0x10, 0x28, 0x2F, sim::kEscTokEof};
    int tokens = DriveBinaryTokenParse(blob);
    CHECK_EQ(tokens, 4);             // stopped on the 0x2B EOF byte
}

TEST(RealSceneDriverUnit, DriveBinaryTokenParseStopsAtError) {
    // A byte above kEscMaxToken (0x3A) makes EscReadToken return the error code
    // (0x27), which the driver treats as end-of-walk.
    std::vector<u8> blob = {0x05, 0x10, 0x3B /* >0x3A -> error */, 0x05};
    int tokens = DriveBinaryTokenParse(blob);
    CHECK_EQ(tokens, 2);             // two in-range tokens, then the error byte
}

TEST(RealSceneDriverUnit, DriveBinaryTokenParseEmptyIsImmediateEof) {
    std::vector<u8> blob;            // empty -> readByte hits short-read -> EOF
    CHECK_EQ(DriveBinaryTokenParse(blob), 0);
}
