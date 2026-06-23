#include "sim/effect_script.h"

#include <cstddef>

// =============================================================================
// guild::sim — the effect-script command set + VM driver (gilde.exe 1:1).
//
// Reconstructs the EFFECT/EMITTER command handlers the .esc scripts invoke
// (VIBE_Script_RegisterObjectCommands @0x440618 + RegisterCommands @0x43c850)
// and drives the already-reconstructed VM core (script_run / script_compiler)
// through them so a real effect script (e.g. Schornstein_dunkel.esc) produces
// its emitter set. See effect_script.h for the byte-exact field/scale recovery.
// =============================================================================
namespace guild::sim {

// ---------------------------------------------------------------------------
// 0x43fd24 — VIBE_Particle_CreateEmitter (script command). The binary fills a
// default template (scale 10/50/30, life 180, white, alpha 255, init bit) and
// tail-calls SpawnSystemByType which allocates a system and returns its handle.
// Here the observable result is a new EffectEmitter recorded in the table; its
// handle is the 1-based index. The CreateEmitter args are stored verbatim (the
// kind/amplitude/texSlot/owner/"tex"/userType/trigger the smoke script passes).
// ---------------------------------------------------------------------------
i32 EffectVm::CreateEmitter(int kind, int amplitude, int texSlot, int owner,
                            const std::string& texName, int userType, int trigger) {
    EffectEmitter e;
    e.alive        = true;
    e.kind         = kind;
    e.amplitudeArg = amplitude;
    e.texSlot      = texSlot;
    e.owner        = owner;
    e.texName      = texName;
    e.userType     = userType;
    e.trigger      = trigger;
    emitters.push_back(std::move(e));
    return static_cast<i32>(emitters.size());  // 1-based handle
}

EffectEmitter* EffectVm::Resolve(i32 handle) {
    // The VIBE_Memory_IsValidPointer guard: a live, in-range emitter.
    if (handle < 1 || static_cast<size_t>(handle) > emitters.size()) return nullptr;
    EffectEmitter* e = &emitters[static_cast<size_t>(handle) - 1];
    return e->alive ? e : nullptr;
}

// Small helpers mirroring the per-command arg reads. The InvokeCommand ABI puts
// args[0] = handle and args[1..] = the parameters in source order.
static int arg(const std::vector<i32>& a, size_t i) {
    return i < a.size() ? a[i] : 0;
}

i32 EffectVm::Invoke(const std::string& name, std::vector<i32>& args) {
    // --- CreateEmitter(kind, amplitude, texSlot, owner, "tex", userType, trig)
    // The texture-name string operand is class kTokStrLit; the VM evaluates it to
    // 0 in the integer arg list, so the literal text is not in `args`. We still
    // record the recovered default ("rauch" for smoke) is supplied by callers via
    // the source; for the integer-only ABI we pass an empty texName here. (The
    // observable emitter parameters the smoke renders from are the numeric ones.)
    if (name == "CreateEmitter") {
        // Args: kind, amplitude, texSlot, owner, "tex"(string), userType, trigger.
        // The string operand occupies arg slot 4 (evaluated to 0 in the integer
        // arg list); userType/trigger are therefore at slots 5 and 6.
        return CreateEmitter(arg(args, 0), arg(args, 1), arg(args, 2),
                             arg(args, 3), /*texName*/ std::string(),
                             arg(args, 5), arg(args, 6));
    }

    // Every remaining effect command takes the emitter handle as args[0].
    EffectEmitter* e = Resolve(arg(args, 0));

    // --- 0x4405c4 SetParticlePos(h, x, y, z): pos = (x, y, z) ----------------
    // DISASM (0x4405c8..0x440606): esi=arg1, ebx=arg2, edx<-ecx=arg3; v8[0]=*esi,
    // v8[1]=*ebx, v8[2]=*edx  =>  (arg1, arg2, arg3) — NO reorder. SetPosition
    // (0x5af38c) stores a2[0..2] -> object[19..21] in order, so the emitter xyz is
    // exactly (arg1, arg2, arg3). (Hex-Rays mislabeled the ecx/ebx args; the
    // register trace above is authoritative — earlier x,z,y read was wrong.)
    if (name == "SetParticlePos") {
        if (e) {
            e->posX = static_cast<float>(arg(args, 1)); // v8[0] = arg1 (x)
            e->posY = static_cast<float>(arg(args, 2)); // v8[1] = arg2 (y)
            e->posZ = static_cast<float>(arg(args, 3)); // v8[2] = arg3 (z)
        }
        return 0;
    }

    // --- 0x43fe9c SetEmitterAmplitude(h,a1,a2,a3,a4,a5): f[16..19]=*0.01, f44 -
    if (name == "SetEmitterAmplitude") {
        if (e) {
            e->f[16] = static_cast<float>(static_cast<double>(arg(args, 1)) * kEmitterAmpScale);
            e->f[17] = static_cast<float>(static_cast<double>(arg(args, 2)) * kEmitterAmpScale);
            e->f[18] = static_cast<float>(static_cast<double>(arg(args, 3)) * kEmitterAmpScale);
            e->f[19] = static_cast<float>(static_cast<double>(arg(args, 4)) * kEmitterAmpScale);
            e->ampSpread = static_cast<float>(static_cast<double>(arg(args, 5)) * kEmitterAmpScale);
        }
        return 0;
    }

    // --- 0x43ff30 SetEmitterPhasespeed(h,a1..a5): f[20..23]=*0.01, f45 --------
    if (name == "SetEmitterPhasespeed") {
        if (e) {
            e->f[20] = static_cast<float>(static_cast<double>(arg(args, 1)) * kEmitterPhaseScale);
            e->f[21] = static_cast<float>(static_cast<double>(arg(args, 2)) * kEmitterPhaseScale);
            e->f[22] = static_cast<float>(static_cast<double>(arg(args, 3)) * kEmitterPhaseScale);
            e->f[23] = static_cast<float>(static_cast<double>(arg(args, 4)) * kEmitterPhaseScale);
            e->phaseSpread = static_cast<float>(static_cast<double>(arg(args, 5)) * kEmitterPhaseScale);
        }
        return 0;
    }

    // --- 0x43ffc4 SetEmitterDirection(h,a1,a2,a3): f[27..29]=*0.01 -----------
    if (name == "SetEmitterDirection") {
        if (e) {
            e->f[27] = static_cast<float>(static_cast<double>(arg(args, 1)) * kEmitterDirScale);
            e->f[28] = static_cast<float>(static_cast<double>(arg(args, 2)) * kEmitterDirScale);
            e->f[29] = static_cast<float>(static_cast<double>(arg(args, 3)) * kEmitterDirScale);
        }
        return 0;
    }

    // --- 0x44002c SetEmitterSize(h,a1,a2,a3): f[24..26] (no scale) -----------
    if (name == "SetEmitterSize") {
        if (e) {
            e->f[24] = static_cast<float>(arg(args, 1));
            e->f[25] = static_cast<float>(arg(args, 2));
            e->f[26] = static_cast<float>(arg(args, 3));
        }
        return 0;
    }

    // --- 0x4400d4 SetEmitterAcceleration(h,a1,a2,a3): f[36..38]=*0.001 -------
    if (name == "SetEmitterAcceleration") {
        if (e) {
            e->f[36] = static_cast<float>(static_cast<double>(arg(args, 1)) * kEmitterAccelScale);
            e->f[37] = static_cast<float>(static_cast<double>(arg(args, 2)) * kEmitterAccelScale);
            e->f[38] = static_cast<float>(static_cast<double>(arg(args, 3)) * kEmitterAccelScale);
        }
        return 0;
    }

    // --- 0x440234 SetEmitterTimeAndAlpha(h,a1,a2,a3,a4) ----------------------
    //   The handler (a2@edx,a3@ebx,ecx,a4 stack) writes:
    //     +188(u32)=a1, +192(u32)=a2, +196(u32)=a3, +184(float)=a4.
    //   (Verified 0x440246..0x440262 — the FLOAT field +184 takes arg4, NOT arg1;
    //    the three u32 time fields take args1..3 in order.)
    if (name == "SetEmitterTimeAndAlpha") {
        if (e) {
            e->time0 = static_cast<uint32_t>(arg(args, 1)); // +188 = a1
            e->time1 = static_cast<uint32_t>(arg(args, 2)); // +192 = a2
            e->time2 = static_cast<uint32_t>(arg(args, 3)); // +196 = a3
            e->lifeBase = static_cast<float>(arg(args, 4));  // +184 = a4 (float)
        }
        return 0;
    }

    // --- 0x44028c SetEmitterColor(h,r,g,b,a): +200=b,+201=g,+202=r,+203=a ----
    if (name == "SetEmitterColor") {
        if (e) {
            e->colR = static_cast<uint8_t>(arg(args, 1));
            e->colG = static_cast<uint8_t>(arg(args, 2));
            e->colB = static_cast<uint8_t>(arg(args, 3));
            e->colA = static_cast<uint8_t>(arg(args, 4));
        }
        return 0;
    }

    // --- 0x43fe68 KillEmitter(h): free the emitter ---------------------------
    if (name == "KillEmitter") {
        if (e) { e->alive = false; ++killCount; }
        return 0;
    }

    // --- 0x43c708 Sleep(ms): yield this script context -----------------------
    // Sleep(-1) keeps the script resident forever (the smoke path stores the
    // handle and lets the per-frame stepper own its lifetime). We record the
    // request; the integration owns the actual frame-yield.
    if (name == "Sleep") {
        sleeping = true;
        sleepMs  = arg(args, 0);
        return 0;
    }

    // Unknown / not-an-effect command -> the VM host-hook default.
    return 0;
}

// ---------------------------------------------------------------------------
// The effect command name set (registration order: RegisterCommands @0x43c850
// generic VM commands, then RegisterObjectCommands @0x440618 object/effect
// commands). Only the names matter to the lexer's classification; the full set
// is listed so any shipped .esc lexes its commands correctly.
// ---------------------------------------------------------------------------
const std::vector<std::string>& EffectCommandNames() {
    static const std::vector<std::string> kNames = {
        // --- 0x43c850 VIBE_Script_RegisterCommands -------------------------
        "ecmd_Dummy", "Print", "PrintInt", "PrintFloat", "Random", "GetTime",
        "RunScript", "RunScriptInt", "RunScriptString", "StopScript", "Sleep",
        "FindScript", "KillLocalScripts", "CallUserFunction",
        "CallUserFunctionExtended",
        // --- 0x440618 VIBE_Script_RegisterObjectCommands -------------------
        "CreateObject", "CreateObjectAtDummy", "CreateObjectGroupAtDummy",
        "KillObject", "SetPos", "SetAngle", "MoveObject", "RotateObject",
        "MoveObjectRelative", "RotateObjectRelative", "SetAmbiente", "LightCalc",
        "ReplaceObject", "LoadScene", "AttachAnim", "PreloadAnim",
        "AttachAnimLooped", "AttachAnimLoopedToAll", "DetachAnimFromAll",
        "GetObjectHandle", "GetRndObjectHandle", "GetRndSubObjectHandle",
        "GetSubObjectHandle", "SetNoTextures", "SetLightSet", "SetFogSet",
        "CameraFlight", "ZoomOnObject", "CameraFlightEnhanced", "NewCameraFlight",
        "ObjectFlight", "CreateRain", "DeleteRain", "CreateParticle",
        "KillParticle", "SetCameraToDummy", "StopCameraFlight", "RenameObject",
        "SetBlocked", "SetTransient", "CreateEmitter", "KillEmitter",
        "SetEmitterAmplitude", "SetEmitterPhasespeed", "SetEmitterDirection",
        "SetEmitterSize", "SetEmitterVelocity", "SetEmitterAcceleration",
        "SetEmitterRndVelocity", "SetEmitterPlane", "SetEmitterTimeAndAlpha",
        "SetEmitterColor", "SetEmitterFlags", "SetEmitterInitFill",
        "SetEmitterRebirthFill", "SetEmitterTextureMode", "SetEmitterIsTrigger",
        "SetEmitterTriggerOnce", "SetEmitterMaxTrigger", "TriggerEmitter",
        "SetParticlePos", "SelectAllTextureSets",
    };
    return kNames;
}

ScriptHost MakeEffectHost(EffectVm& vm) {
    ScriptHost host;
    host.invokeCommand = [&vm](const std::string& name, std::vector<i32>& args) -> i32 {
        return vm.Invoke(name, args);
    };
    host.callUserFunction = [](i32) -> i32 { return 0; };
    return host;
}

// gilde.exe 0x4431dc — the param-binding portion of VIBE_Script_EnterFunction:
// when `main(x,y,z)` is entered, the entry arguments are bound as the function's
// named parameters before the body runs (DeclareLocal per param). RunMain enters
// at main's body cursor without that binding, so we reproduce it here: for each
// of main's declared parameters, ensure a variable of that name exists in the
// symbol table and seed its storage cell with the corresponding entry arg. The
// smoke script's SetParticlePos(emitter[0], x, y, z) then reads the real chimney
// position instead of zero. Up to three entry args are bound (the smoke main).
static void BindMainParams(LoadedScript& ls, int x, int y, int z) {
    int mi = ls.compiled.symbols.LookupFunction("main");
    if (mi < 0 || mi >= (int)ls.compiled.symbols.funcs().size()) return;
    const ScriptFunc& mfn = ls.compiled.symbols.funcs()[(size_t)mi];
    const int entryArgs[3] = {x, y, z};
    for (size_t p = 0; p < mfn.paramNames.size() && p < 3; ++p) {
        const std::string& pname = mfn.paramNames[p];
        if (pname.empty()) continue;
        int vi = ls.compiled.symbols.LookupVariable(pname);
        if (vi < 0) {
            // Param not yet a variable: declare it (int scalar) so the body's
            // reads resolve. DefineVariable allocates a storage cell.
            u8 ptype = (p < mfn.paramTypes.size()) ? mfn.paramTypes[p] : 1; // int
            if (ptype == 0) ptype = 1;
            vi = ls.compiled.symbols.DefineVariable(pname, ptype, 1);
        }
        if (vi >= 0 && vi < (int)ls.compiled.symbols.vars().size()) {
            const ScriptVar& v = ls.compiled.symbols.vars()[(size_t)vi];
            auto& st = ls.compiled.symbols.storage();
            if (v.storage >= 0 && v.storage < (int)st.size())
                st[(size_t)v.storage] = entryArgs[p];
        }
    }
}

// Find the offset just past the matching '}' of the braced block whose opening
// '{' is at or after `from` in `s`. Returns s.size() if unbalanced. Used to bound
// a single function-entry run to that function's body (the original's function
// return: a context steps within ONE function until it blocks/returns; the next
// function — e.g. the script's exit() teardown — runs only on a separate event,
// not as a fall-through). RunMain enters at main's body without a return barrier,
// so we cap the run at main's closing brace here, in the effect driver.
static size_t MatchingBraceEnd(const std::string& s, int from) {
    int pos = from;
    while (pos < (int)s.size() && s[pos] != '{') ++pos;
    if (pos >= (int)s.size()) return s.size();
    int depth = 0;
    for (; pos < (int)s.size(); ++pos) {
        if (s[pos] == '{') ++depth;
        else if (s[pos] == '}') { if (--depth == 0) return (size_t)pos + 1; }
    }
    return s.size();
}

EffectVm RunEffectScript(LoadedScript& ls, int x, int y, int z, int stepBudget) {
    EffectVm vm;
    vm.argX = x;
    vm.argY = y;
    vm.argZ = z;
    if (!ls.ok) return vm;
    // Bind main(x,y,z)'s parameters (EnterFunction's param marshalling) so the
    // body reads the entry args (the chimney world position) — not zero.
    BindMainParams(ls, x, y, z);
    // Cap the run at main's closing brace (the function-return barrier) so the
    // executor does not fall through into the script's exit() teardown function.
    // The symbol cursors for main all precede this point, so truncating the live
    // source after main's '}' is behaviour-preserving for the main run. (exit()'s
    // KillEmitter teardown is the FINISH path, modeled by EffectVm::killCount when
    // a host tears the resident script down — see header.)
    if (ls.mainCursor >= 0 && (size_t)ls.mainCursor < ls.compiled.source.size()) {
        size_t end = MatchingBraceEnd(ls.compiled.source, ls.mainCursor);
        if (end < ls.compiled.source.size())
            ls.compiled.source.resize(end);
    }
    ScriptHost host = MakeEffectHost(vm);
    // main(x,y,z): run the body, whose command statements drive vm.Invoke.
    RunMain(ls, host, stepBudget);
    return vm;
}

EffectVm RunEffectScriptSource(const std::string& name, const std::string& source,
                               int x, int y, int z, bool* ok) {
    LoadedScript ls = LoadScriptFromSource(name, source, EffectCommandNames());
    if (ok) *ok = ls.ok;
    return RunEffectScript(ls, x, y, z);
}

EffectVm RunEffectScriptFromVfs(const char* relName, int x, int y, int z,
                                bool addPrefix, bool* ok) {
    LoadedScript ls = LoadScriptFromVfs(relName, EffectCommandNames(), addPrefix);
    if (ok) *ok = ls.ok;
    return RunEffectScript(ls, x, y, z);
}

} // namespace guild::sim
