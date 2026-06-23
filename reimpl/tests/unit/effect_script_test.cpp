// Golden unit tests for the effect-script (.esc) command set + VM driver
// (src/sim/effect_script). Pins the byte-exact emitter field/scale recovery from
// gilde.exe's VIBE_Script_RegisterObjectCommands @0x440618 handler set, and runs
// a synthetic smoke-style effect script end-to-end through the reconstructed VM.
#include "test.h"
#include "sim/effect_script.h"
#include "sim/script_run.h"        // LoadScriptFromSource / LoadedScript
#include "sim/script_compiler.h"   // CompiledScript / ScriptExecutor
#include "sim/script_symbols.h"    // ScriptFunc / ScriptVar
#include "render/building_fx.h"   // W18-ESC wiring bridge (rule 13)
#include "render/particle_emitter_create.h"  // LiveSystems / DestroyAllSystems

#include <cmath>
#include <string>

using namespace guild;
using namespace guild::sim;

static bool fclose(float a, float b) { return std::fabs(a - b) < 1e-4f; }

// --- 1) the command name set covers the smoke script's commands -------------
TEST(EffectScript, CommandSetCoversSmokeCommands) {
    const auto& names = EffectCommandNames();
    auto has = [&](const char* n) {
        for (const auto& s : names) if (s == n) return true;
        return false;
    };
    CHECK(has("CreateEmitter"));
    CHECK(has("SetParticlePos"));
    CHECK(has("SetEmitterAmplitude"));
    CHECK(has("SetEmitterPhasespeed"));
    CHECK(has("SetEmitterAcceleration"));
    CHECK(has("SetEmitterDirection"));
    CHECK(has("SetEmitterSize"));
    CHECK(has("SetEmitterTimeAndAlpha"));
    CHECK(has("SetEmitterColor"));
    CHECK(has("KillEmitter"));
    CHECK(has("Sleep"));
}

// --- 2) CreateEmitter allocates a 1-based handle ----------------------------
TEST(EffectScript, CreateEmitterReturnsOneBasedHandle) {
    EffectVm vm;
    i32 h0 = vm.CreateEmitter(1, 0, 20, 30, "rauch", 4, 1);
    i32 h1 = vm.CreateEmitter(1, 0, 30, 25, "rauch", 100, 0);
    CHECK_EQ(h0, 1);
    CHECK_EQ(h1, 2);
    CHECK_EQ((int)vm.emitters.size(), 2);
    CHECK(vm.emitters[0].alive);
    CHECK_EQ(vm.emitters[0].userType, 4);
    CHECK_EQ(vm.emitters[1].userType, 100);
}

// --- 3) SetEmitterAmplitude: f[16..19]=arg*0.01, f44=arg5*0.01 --------------
TEST(EffectScript, SetAmplitudeScalesByHundredth) {
    EffectVm vm;
    i32 h = vm.CreateEmitter(1, 0, 20, 30, "rauch", 4, 1);
    std::vector<i32> a = {h, 200, 100, 200, 50, 0};
    vm.Invoke("SetEmitterAmplitude", a);
    EffectEmitter& e = vm.emitters[0];
    CHECK(fclose(e.f[16], 2.0f));    // 200 * 0.01
    CHECK(fclose(e.f[17], 1.0f));    // 100 * 0.01
    CHECK(fclose(e.f[18], 2.0f));    // 200 * 0.01
    CHECK(fclose(e.f[19], 0.5f));    //  50 * 0.01
    CHECK(fclose(e.ampSpread, 0.0f));
}

// --- 4) SetEmitterSize: NO scale (f[24..26] = raw args) ---------------------
TEST(EffectScript, SetSizeNoScale) {
    EffectVm vm;
    i32 h = vm.CreateEmitter(1, 0, 20, 30, "rauch", 4, 1);
    std::vector<i32> a = {h, 1, 15, 100};
    vm.Invoke("SetEmitterSize", a);
    EffectEmitter& e = vm.emitters[0];
    CHECK(fclose(e.f[24], 1.0f));
    CHECK(fclose(e.f[25], 15.0f));
    CHECK(fclose(e.f[26], 100.0f));
}

// --- 5) SetEmitterAcceleration: f[36..38] = arg*0.001 -----------------------
TEST(EffectScript, SetAccelerationScalesByThousandth) {
    EffectVm vm;
    i32 h = vm.CreateEmitter(1, 0, 20, 30, "rauch", 4, 1);
    std::vector<i32> a = {h, 1000, 2000, 0};
    vm.Invoke("SetEmitterAcceleration", a);
    EffectEmitter& e = vm.emitters[0];
    CHECK(fclose(e.f[36], 1.0f));    // 1000 * 0.001
    CHECK(fclose(e.f[37], 2.0f));    // 2000 * 0.001
    CHECK(fclose(e.f[38], 0.0f));
}

// --- 6) SetParticlePos: pos = (arg1,arg2,arg3) (disasm 0x4405c8.., no reorder) -
TEST(EffectScript, SetParticlePosXYZ) {
    EffectVm vm;
    i32 h = vm.CreateEmitter(1, 0, 20, 30, "rauch", 4, 1);
    std::vector<i32> a = {h, 100, 250, 400}; // x=100,y=250,z=400
    vm.Invoke("SetParticlePos", a);
    EffectEmitter& e = vm.emitters[0];
    CHECK(fclose(e.posX, 100.0f));   // v8[0] = arg1 (x)
    CHECK(fclose(e.posY, 250.0f));   // v8[1] = arg2 (y)
    CHECK(fclose(e.posZ, 400.0f));   // v8[2] = arg3 (z)
}

// --- 7) SetEmitterColor: +200=b, +201=g, +202=r, +203=a (BGR store) --------
TEST(EffectScript, SetColorStoresBGRA) {
    EffectVm vm;
    i32 h = vm.CreateEmitter(1, 0, 20, 30, "rauch", 4, 1);
    std::vector<i32> a = {h, 64, 65, 66, 7}; // r=64,g=65,b=66,a=7
    vm.Invoke("SetEmitterColor", a);
    EffectEmitter& e = vm.emitters[0];
    CHECK_EQ((int)e.colR, 64);
    CHECK_EQ((int)e.colG, 65);
    CHECK_EQ((int)e.colB, 66);
    CHECK_EQ((int)e.colA, 7);
}

// --- 8) SetEmitterTimeAndAlpha: +188/192/196=arg1/2/3, +184(float)=arg4 ------
// (disasm 0x440246..0x440262: esi=arg1->+188, ebx=arg2->+192, ecx=arg3->+196,
//  stack=arg4->+184 as float. The float field takes the LAST arg, not the first.)
TEST(EffectScript, SetTimeAndAlpha) {
    EffectVm vm;
    i32 h = vm.CreateEmitter(1, 0, 20, 30, "rauch", 4, 1);
    std::vector<i32> a = {h, 10, 500, 550, 255};
    vm.Invoke("SetEmitterTimeAndAlpha", a);
    EffectEmitter& e = vm.emitters[0];
    CHECK_EQ((int)e.time0, 10);      // +188 = arg1
    CHECK_EQ((int)e.time1, 500);     // +192 = arg2
    CHECK_EQ((int)e.time2, 550);     // +196 = arg3
    CHECK(fclose(e.lifeBase, 255.0f)); // +184 = arg4 (float)
}

// --- 9) KillEmitter invalidates the handle; further writes are no-ops -------
TEST(EffectScript, KillEmitterInvalidates) {
    EffectVm vm;
    i32 h = vm.CreateEmitter(1, 0, 20, 30, "rauch", 4, 1);
    std::vector<i32> kill = {h};
    vm.Invoke("KillEmitter", kill);
    CHECK(!vm.emitters[0].alive);
    CHECK_EQ(vm.killCount, 1);
    CHECK(vm.Resolve(h) == nullptr);
    // a write to the dead handle is ignored (the IsValidPointer guard)
    std::vector<i32> a = {h, 1, 1, 1};
    vm.Invoke("SetEmitterSize", a);
    CHECK(fclose(vm.emitters[0].f[24], 0.0f));
}

// --- 10) invalid handle: writes are silently dropped (no crash) -------------
TEST(EffectScript, InvalidHandleDropped) {
    EffectVm vm;
    std::vector<i32> a = {999, 1, 2, 3};
    i32 r = vm.Invoke("SetEmitterSize", a);
    CHECK_EQ(r, 0);
    CHECK_EQ((int)vm.emitters.size(), 0);
}

// --- 11) end-to-end: a smoke-shaped effect script through the real VM -------
// Drives the compiler + executor over an .esc body that mirrors
// Schornstein_dunkel.esc's structure (CreateEmitter then a SetEmitter* chain).
TEST(EffectScript, SmokeShapedScriptRunsEndToEnd) {
    const char* src =
        "int emitter[2];\n"
        "void main(int x, int y, int z)\n"
        "{\n"
        "  emitter[0]=CreateEmitter(1,0,20,30,\"rauch\",4,1);\n"
        "  SetEmitterAmplitude(emitter[0],200,100,200,50,0);\n"
        "  SetEmitterSize(emitter[0],1,15,100);\n"
        "  SetEmitterColor(emitter[0],64,64,64,0);\n"
        "  emitter[1]=CreateEmitter(1,0,30,25,\"rauch\",100,0);\n"
        "  SetEmitterColor(emitter[1],64,64,64,0);\n"
        "}\n";
    bool ok = false;
    EffectVm vm = RunEffectScriptSource("smoke_test.esc", src, 100, 250, 400, &ok);
    CHECK(ok);
    // both CreateEmitter calls executed -> two emitters allocated.
    CHECK_EQ((int)vm.emitters.size(), 2);
    // the SetEmitterColor on emitter[0] applied (BGR store of 64,64,64).
    CHECK_EQ((int)vm.emitters[0].colR, 64);
    CHECK_EQ((int)vm.emitters[0].colG, 64);
    CHECK_EQ((int)vm.emitters[0].colB, 64);
    // emitter[0] size set (no scale): 1,15,100.
    CHECK(fclose(vm.emitters[0].f[24], 1.0f));
    CHECK(fclose(vm.emitters[0].f[26], 100.0f));
    // emitter[1] colour applied too.
    CHECK_EQ((int)vm.emitters[1].colR, 64);
}

// --- 12) the render bridge (rule 13): script -> live particle systems --------
// render::SmokeScriptToEmitters runs the effect VM and spawns one live particle
// system per emitter the script created. Two emitters -> two systems linked.
TEST(EffectScript, RenderBridgeSpawnsOneSystemPerEmitter) {
    const char* src =
        "int emitter[2];\n"
        "void main(int x, int y, int z)\n"
        "{\n"
        "  emitter[0]=CreateEmitter(1,0,20,30,\"rauch\",4,1);\n"
        "  SetParticlePos(emitter[0],x,y,z);\n"
        "  SetEmitterTimeAndAlpha(emitter[0],10,500,550,255);\n"
        "  emitter[1]=CreateEmitter(1,0,30,25,\"rauch\",100,0);\n"
        "  SetParticlePos(emitter[1],x,y,z);\n"
        "  SetEmitterTimeAndAlpha(emitter[1],100,100,500,75);\n"
        "}\n";
    render::DestroyAllSystems();
    int before = render::LiveSystems().Count();
    int owner = 0x1234;  // a non-null pseudo scene node (the chimney)
    int spawned = render::SmokeScriptToEmitters(src, &owner, 1200, 800, 640);
    CHECK_EQ(spawned, 2);
    CHECK_EQ(render::LiveSystems().Count(), before + 2);
    render::DestroyAllSystems();
}

// --- 13) RunSmokeScriptViaVm: host reader supplies the .esc text -------------
static const char* g_fakeEsc =
    "int emitter[1];\n"
    "void main(int x, int y, int z)\n"
    "{\n"
    "  emitter[0]=CreateEmitter(1,0,20,30,\"rauch\",4,1);\n"
    "  SetParticlePos(emitter[0],x,y,z);\n"
    "  SetEmitterTimeAndAlpha(emitter[0],10,500,550,255);\n"
    "}\n";
static bool FakeReader(const char* /*path*/, std::string& out, void* /*user*/) {
    out = g_fakeEsc;
    return true;
}
TEST(EffectScript, RunSmokeScriptViaVmUsesHostReader) {
    render::DestroyAllSystems();
    // No reader installed -> behaves like a missing script (returns -1).
    render::SetSmokeScriptReader(nullptr, nullptr);
    int dummy = 0x55;
    CHECK_EQ(render::RunSmokeScriptViaVm("effekte\\X.esc", &dummy, 1, 2, 3), -1);
    // Reader installed -> runs the script, spawns the system, returns a handle.
    render::SetSmokeScriptReader(FakeReader, nullptr);
    i32 h = render::RunSmokeScriptViaVm("effekte\\X.esc", &dummy, 1, 2, 3);
    CHECK(h != -1);
    CHECK_EQ(render::LiveSystems().Count(), 1);
    render::SetSmokeScriptReader(nullptr, nullptr);  // restore inert
    render::DestroyAllSystems();
}

// --- 14) the exit() teardown function: while / if / array-index / i++ --------
// The real Schornstein_dunkel.esc carries a SECOND function, exit(), that the
// engine runs on script FINISH (VIBE_Script_Finish @0x443f38 enters the "exit"
// function when present). Its body exercises the loop / branch / array-subscript
// control flow the main() smoke body never touches:
//     i=0;
//     while (i<2) { if (emitter[i]!=0) { KillEmitter(emitter[i]); } i++; }
// This drives that body through the reconstructed VM (ParseWhileLoop 0x44259c,
// DispatchTokenBranch 0x442ac8, EvaluateExpression 0x443ff0 with the `[idx]`
// read, AssignVariable 0x4445bc `++`) and pins the OBSERVABLE outcome: with two
// live (non-zero) emitter handles, KillEmitter fires exactly twice.
TEST(EffectScript, ExitTeardownLoopKillsBothEmitters) {
    // The real exit()'s body, compiled stand-alone with the global state it reads
    // (emitter[2] seeded with two live handles, i the loop counter). We compile a
    // tiny script whose main() IS the teardown loop so RunMain drives it directly.
    const char* src =
        "int emitter[2];\n"
        "int i;\n"
        "void main(int x, int y, int z)\n"
        "{\n"
        "  i=0;\n"
        "  while (i<2)\n"
        "  {\n"
        "    if (emitter[i]!=0)\n"
        "    {\n"
        "      KillEmitter(emitter[i]);\n"
        "    }\n"
        "    i++;\n"
        "  }\n"
        "}\n";
    LoadedScript ls = LoadScriptFromSource("teardown.esc", src, EffectCommandNames());
    CHECK(ls.ok);

    // Seed emitter[0]/emitter[1] with two live handles (as if main() had created
    // them) so the `emitter[i]!=0` branch is taken on both iterations.
    int vi = ls.compiled.symbols.LookupVariable("emitter");
    CHECK(vi >= 0);
    const ScriptVar& ev = ls.compiled.symbols.vars()[(size_t)vi];
    auto& st = ls.compiled.symbols.storage();
    st[(size_t)ev.storage + 0] = 1;   // emitter[0] = handle 1
    st[(size_t)ev.storage + 1] = 2;   // emitter[1] = handle 2

    // An EffectVm with two live emitters for KillEmitter to invalidate.
    EffectVm vm;
    vm.CreateEmitter(1, 0, 0, 0, "rauch", 0, 0);  // -> handle 1
    vm.CreateEmitter(1, 0, 0, 0, "rauch", 0, 0);  // -> handle 2
    ScriptHost host = MakeEffectHost(vm);

    ScriptExecutor exec(ls.compiled, host, EffectCommandNames());
    exec.Run(ls.mainCursor, /*budget*/ 100000);

    // The loop ran twice, the branch fired on each (both handles non-zero), so
    // KillEmitter was invoked for emitter[0] and emitter[1]: two kills, both dead.
    CHECK_EQ(vm.killCount, 2);
    CHECK(!vm.emitters[0].alive);
    CHECK(!vm.emitters[1].alive);
    // The loop counter advanced to its terminating value (i==2).
    int ii = ls.compiled.symbols.LookupVariable("i");
    CHECK(ii >= 0);
    const ScriptVar& iv = ls.compiled.symbols.vars()[(size_t)ii];
    CHECK_EQ(st[(size_t)iv.storage], 2);
}

// --- 15) a zero-handle emitter is NOT killed (the if-branch is skipped) -------
// Mirrors the exit() guard `if (emitter[i]!=0)`: a slot left at 0 must be skipped
// (the engine never calls KillEmitter(0)). Verifies DispatchTokenBranch skips the
// braced body on a false condition.
TEST(EffectScript, ExitTeardownSkipsZeroHandles) {
    const char* src =
        "int emitter[2];\n"
        "int i;\n"
        "void main(int x, int y, int z)\n"
        "{\n"
        "  i=0;\n"
        "  while (i<2)\n"
        "  {\n"
        "    if (emitter[i]!=0)\n"
        "    {\n"
        "      KillEmitter(emitter[i]);\n"
        "    }\n"
        "    i++;\n"
        "  }\n"
        "}\n";
    LoadedScript ls = LoadScriptFromSource("teardown2.esc", src, EffectCommandNames());
    CHECK(ls.ok);
    int vi = ls.compiled.symbols.LookupVariable("emitter");
    const ScriptVar& ev = ls.compiled.symbols.vars()[(size_t)vi];
    auto& st = ls.compiled.symbols.storage();
    st[(size_t)ev.storage + 0] = 0;   // emitter[0] = 0 -> skipped
    st[(size_t)ev.storage + 1] = 1;   // emitter[1] = handle 1 -> killed

    EffectVm vm;
    vm.CreateEmitter(1, 0, 0, 0, "rauch", 0, 0);  // handle 1
    ScriptHost host = MakeEffectHost(vm);
    ScriptExecutor exec(ls.compiled, host, EffectCommandNames());
    exec.Run(ls.mainCursor, 100000);

    // Only the non-zero handle (emitter[1]) was killed: exactly one kill.
    CHECK_EQ(vm.killCount, 1);
}
