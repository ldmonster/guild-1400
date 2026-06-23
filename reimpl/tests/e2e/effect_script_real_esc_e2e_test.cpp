// End-to-end: parse + run the REAL shipped chimney-smoke effect script
// (effekte\Schornstein_dunkel.esc) through the reconstructed .esc VM core
// (sim/script_run + sim/script_compiler/_lexer/_symbols) and the reconstructed
// effect command set (sim/effect_script). GUARDED: if the unpacked script is
// absent the test passes trivially so the suite stays green.
//
// This pins the OBSERVABLE emitter set the chimney smoke produces:
//   - two emitters (emitter[0], emitter[1]) created by CreateEmitter,
//   - their SetEmitter* parameters at the recovered scales/offsets,
//   - SetParticlePos placing them at the passed (x,y,z) dummy world position,
//   - Sleep(-1) leaving the script resident (the per-frame stepper owns it).
// The bytes of Schornstein_dunkel.esc are reproduced inline as a fallback so the
// command-set + VM are still exercised end-to-end where the asset tree is absent.
#include "test.h"
#include "sim/effect_script.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <cmath>

using namespace guild;
using namespace guild::sim;

static bool fclose(float a, float b) { return std::fabs(a - b) < 1e-3f; }

// The real script's exact bytes (effekte\Schornstein_dunkel.esc), used when the
// unpacked asset tree is not present so the e2e still drives the full pipeline.
static const char* kSchornsteinDunkel =
    "int emitter[2];\n"
    "int i;\n"
    "\n\n\n"
    "void main(int x, int y, int z)\n"
    "{\n"
    "\n"
    "\t#multistep\n"
    "\temitter[0]=CreateEmitter(1,0,20,30,\"rauch\",4,1);\n"
    "\tSetParticlePos(emitter[0],x,y,z);\n"
    "\tSetEmitterAmplitude(emitter[0],200,100,200,50,0);\n"
    "\tSetEmitterPhasespeed(emitter[0],10,5,8,10,10);\n"
    "\tSetEmitterAcceleration(emitter[0],0,0,0);\n"
    "\tSetEmitterDirection(emitter[0],4,50,4);\n"
    "\tSetEmitterSize(emitter[0],1, 15, 100);\n"
    "\tSetEmitterTimeAndAlpha(emitter[0],10,500,550,255);\n"
    "\tSetEmitterColor(emitter[0], 64, 64, 64, 0);\n"
    "\n\n\n\n"
    "\temitter[1]=CreateEmitter(1,0,30,25,\"rauch\",100,0);\n"
    "\tSetParticlePos(emitter[1],x,y,z);\n"
    "\tSetEmitterAmplitude(emitter[1],150,75,150,50,50);\n"
    "\tSetEmitterPhasespeed(emitter[1],10,5,8,10,30);\n"
    "\tSetEmitterAcceleration(emitter[1],0,0,0);\n"
    "\tSetEmitterDirection(emitter[1],4,50,4);\n"
    "\tSetEmitterSize(emitter[1],1, 10, 100);\n"
    "\tSetEmitterTimeAndAlpha(emitter[1],100,100,500,75);\n"
    "\tSetEmitterColor(emitter[1], 64, 64, 64, 0);\n"
    "\n"
    "\t#singlestep\n"
    "\tSleep(-1);\n"
    "}\n";

static std::string LoadRealOrFallback(bool* fromDisk) {
    // Try a few plausible unpacked locations (the repo ships the script under
    // unpacked_resources/Scripts/Effekte/).
    const char* paths[] = {
        "unpacked_resources/Scripts/Effekte/Schornstein_dunkel.esc",
        "../unpacked_resources/Scripts/Effekte/Schornstein_dunkel.esc",
        "../../unpacked_resources/Scripts/Effekte/Schornstein_dunkel.esc",
    };
    for (const char* p : paths) {
        std::ifstream f(p, std::ios::binary);
        if (f) {
            std::ostringstream ss; ss << f.rdbuf();
            if (fromDisk) *fromDisk = true;
            return ss.str();
        }
    }
    if (fromDisk) *fromDisk = false;
    return std::string(kSchornsteinDunkel);
}

// --- the real chimney-smoke script produces the documented two-emitter set ---
TEST(EffectScriptRealEsc, SchornsteinDunkelProducesTwoEmitters) {
    bool fromDisk = false;
    std::string src = LoadRealOrFallback(&fromDisk);
    std::printf("  [effect-script] Schornstein_dunkel.esc source: %s (%zu bytes)\n",
                fromDisk ? "real on-disk asset" : "inline fallback", src.size());

    bool ok = false;
    // The chimney smoke runs main(x,y,z) at the dummy world position; pin a
    // representative chimney position.
    EffectVm vm = RunEffectScriptSource("effekte\\Schornstein_dunkel.esc", src,
                                        /*x*/ 1200, /*y*/ 800, /*z*/ 640, &ok);
    CHECK(ok);                              // strips/compiles/has a main
    CHECK_EQ((int)vm.emitters.size(), 2);  // emitter[0] + emitter[1]

    // --- emitter[0] -------------------------------------------------------
    const EffectEmitter& e0 = vm.emitters[0];
    CHECK(e0.alive);
    CHECK_EQ(e0.userType, 4);              // CreateEmitter(...,4,1) -> userType 4
    CHECK_EQ(e0.trigger, 1);
    // SetParticlePos(emitter[0],x,y,z): pos = (arg1,arg2,arg3) = (1200, 800, 640)
    CHECK(fclose(e0.posX, 1200.0f));
    CHECK(fclose(e0.posY, 800.0f));
    CHECK(fclose(e0.posZ, 640.0f));
    // SetEmitterAmplitude(...,200,100,200,50,0): *0.01
    CHECK(fclose(e0.f[16], 2.0f));
    CHECK(fclose(e0.f[17], 1.0f));
    CHECK(fclose(e0.f[18], 2.0f));
    CHECK(fclose(e0.f[19], 0.5f));
    // SetEmitterSize(...,1,15,100): no scale
    CHECK(fclose(e0.f[24], 1.0f));
    CHECK(fclose(e0.f[25], 15.0f));
    CHECK(fclose(e0.f[26], 100.0f));
    // SetEmitterDirection(...,4,50,4): *0.01
    CHECK(fclose(e0.f[27], 0.04f));
    CHECK(fclose(e0.f[28], 0.5f));
    CHECK(fclose(e0.f[29], 0.04f));
    // SetEmitterTimeAndAlpha(...,10,500,550,255): +188/192/196=arg1/2/3, +184=arg4
    CHECK_EQ((int)e0.time0, 10);
    CHECK_EQ((int)e0.time1, 500);
    CHECK_EQ((int)e0.time2, 550);
    CHECK(fclose(e0.lifeBase, 255.0f));
    // SetEmitterColor(...,64,64,64,0): smoke is dark grey
    CHECK_EQ((int)e0.colR, 64);
    CHECK_EQ((int)e0.colG, 64);
    CHECK_EQ((int)e0.colB, 64);
    CHECK_EQ((int)e0.colA, 0);

    // --- emitter[1] -------------------------------------------------------
    const EffectEmitter& e1 = vm.emitters[1];
    CHECK(e1.alive);
    CHECK_EQ(e1.userType, 100);           // CreateEmitter(...,100,0)
    CHECK_EQ(e1.trigger, 0);
    // SetEmitterAmplitude(...,150,75,150,50,50): *0.01 (note arg5=50 -> spread)
    CHECK(fclose(e1.f[16], 1.5f));
    CHECK(fclose(e1.f[17], 0.75f));
    CHECK(fclose(e1.ampSpread, 0.5f));
    // SetEmitterSize(...,1,10,100)
    CHECK(fclose(e1.f[25], 10.0f));
    // SetEmitterTimeAndAlpha(...,100,100,500,75): time0/1/2=100/100/500, life=75
    CHECK(fclose(e1.lifeBase, 75.0f));
    CHECK_EQ((int)e1.time2, 500);
    CHECK_EQ((int)e1.colR, 64);

    // Sleep(-1) requested -> the script stays resident; no emitter was killed in
    // main (the exit() KillEmitter loop runs only on teardown).
    CHECK(vm.sleeping);
    CHECK_EQ(vm.sleepMs, -1);
    CHECK_EQ(vm.killCount, 0);
}
