#include "sim/cutscene_misc2.h"

#include <cstdint>   // intptr_t

namespace guild::sim {

// ===========================================================================
// Leaf-hook plumbing + bundled mutable state.
// ===========================================================================
namespace {
const Cutscene2Hooks* g_hooks = nullptr;
Cutscene2Hooks        g_inert{};   // all-null -> deterministic no-ops
Cutscene2State        g_state{};
CutsceneSky           g_sky{};
}  // namespace

void SetCutscene2Hooks(const Cutscene2Hooks* hooks) { g_hooks = hooks; }
const Cutscene2Hooks& GetCutscene2Hooks() { return g_hooks ? *g_hooks : g_inert; }

Cutscene2State& Cutscene2() { return g_state; }
CutsceneSky& CutsceneSkyState() { return g_sky; }

namespace {
// dword_631598 with byte1 OR'd by 0x80, exactly as the runners do:
//   v = dword_631598; BYTE1(v) = BYTE1(dword_631598) | 0x80;
i32 FrameFlagsWithHiBit() {
    i32 v = g_state.frameFlags;
    return v | (static_cast<i32>(kFrameLoopHiBit) << 8);   // set byte1 bit7
}

int Pump(i32 flags, i32 a, void* payload) {
    const Cutscene2Hooks& h = GetCutscene2Hooks();
    if (h.pumpFrame) return h.pumpFrame(flags, a, payload);
    return 0;   // inert: report "stop" so loops terminate
}

void* FindByHandle(i32 handle) {
    const Cutscene2Hooks& h = GetCutscene2Hooks();
    return h.scriptFindByHandle ? h.scriptFindByHandle(handle) : nullptr;
}
}  // namespace

// ===========================================================================
// gilde.exe 0x4aa01c — VIBE_Cutscene_LoadAndRunScript
// ===========================================================================
void* CutsceneLoadAndRunScript(const char* name, i32* scriptHandleOut) {
    if (scriptHandleOut) *scriptHandleOut = 0;
    if (g_state.replayGate)                       // if (dword_6315BC)
        return nullptr;                           //   return 0;
    const Cutscene2Hooks& h = GetCutscene2Hooks();
    void* s = h.scriptLoadFromDir ? h.scriptLoadFromDir(name) : nullptr;
    if (!s)                                       // if (!result) return result;
        return s;
    if (h.scriptRunMain) h.scriptRunMain(s);      // Script_RunMain(s)
    // return *(char**)(s + 128); the started-script handle. We model the +128
    // read as the loaded record's handle reported through scriptHandleFlags's
    // sibling: the load hook already returns the record; the +128 handle is what
    // callers track. Surface it via the out-param if the host provides one.
    if (scriptHandleOut) *scriptHandleOut = 1;    // nonzero "handle exists"
    return s;
}

// ===========================================================================
// gilde.exe 0x4aa06c — VIBE_Cutscene_RunScriptLoop
// ===========================================================================
i32 CutsceneRunScriptLoop(void* payload, i32 handle) {
    i32 v = 0;
    if (!g_state.replayGate) {                    // if (!dword_6315BC)
        do {
            v = Pump(FrameFlagsWithHiBit(), 0, payload);
            if (!v) break;                        // if (!v3) break;
            v = (FindByHandle(handle) != nullptr); // v3 = Script_FindByHandle(...)
        } while (v);                              // while (v3)
    }
    return v;
}

// ===========================================================================
// gilde.exe 0x4aa0a4 — VIBE_Cutscene_RunScriptUntilSkip
// ===========================================================================
i32 CutsceneRunScriptUntilSkip(int (*skipCb)(), i32 a2, i32 handle) {
    if (g_state.replayGate)                       // if (dword_6315BC) return 0;
        return 0;
    i32 result;
    while (true) {
        result = Pump(FrameFlagsWithHiBit(), a2, nullptr);
        if (!result) break;                       // if (!result) break;
        result = (FindByHandle(handle) != nullptr);
        if (!result) break;                       // if (!result) break;
        if (g_state.skipGate) {                   // if (dword_672230)
            if (skipCb) return skipCb();           //   return a1();
            return 1;                              //   else return 1;
        }
    }
    return result;
}

// ===========================================================================
// gilde.exe 0x4aa0fc — VIBE_Cutscene_RunScriptWait
//   literal frame-loop flags 497414 (0x79706).
// ===========================================================================
i32 CutsceneRunScriptWait(void* payload, i32 handle) {
    i32 v = 0;
    if (!g_state.replayGate) {
        do {
            v = Pump(497414, 0, payload);          // RunFrameLoop(497414, 0, payload)
            if (!v) break;
            v = (FindByHandle(handle) != nullptr);
        } while (v);
    }
    return v;
}

// ===========================================================================
// gilde.exe 0x4aa808 — VIBE_Cutscene_RunTimedScript
// ===========================================================================
i32 CutsceneRunTimedScript(i32 frames, i32 a2, i32 (*skipCb)()) {
    u32 start = g_state.gameTick;                 // v4 = dword_62EB38
    i32 v5 = 0;
    if (g_state.replayGate)                        // if (dword_6315BC) return 0;
        return 0;
    const Cutscene2Hooks& h = GetCutscene2Hooks();
    while (true) {
        if (!Pump(FrameFlagsWithHiBit(), a2, reinterpret_cast<void*>(static_cast<intptr_t>(frames))))
            return v5;                             // pump stopped -> return v5
        if (g_state.skipGate) {                    // if (dword_672230)
            v5 = skipCb ? skipCb() : 1;
            g_state.forceQuit = 1;                 // dword_631614 = 1
        } else {
            // if ((double)(unsigned)dword_62EB38 >= (double)(int)frames*scale + (double)(int)start)
            // gilde.exe 0x4aa8ab: LHS is (double)(unsigned int)dword_62EB38, but the
            // start snapshot v4 is an `int` -> (double)v4 is a SIGNED conversion.
            if (static_cast<double>(g_state.gameTick) >=
                static_cast<double>(frames) * kFrameMsScale
                    + static_cast<double>(static_cast<i32>(start)))
                g_state.forceQuit = 1;             // dword_631614 = 1
        }
        if (h.combatUpdateDamageNumbers) h.combatUpdateDamageNumbers();
        if (h.showParticipantDialog) h.showParticipantDialog(0);
    }
}

// ===========================================================================
// gilde.exe 0x4aa8bc — VIBE_Cutscene_RunDelayedScript
// ===========================================================================
i32 CutsceneRunDelayedScript(i32 frames, i32 dialogSlot) {
    u32 start = g_state.gameTick;                 // v1 = dword_62EB38
    i32 v3 = 0;
    if (!g_state.replayGate) {                     // if (!dword_6315BC)
        const Cutscene2Hooks& h = GetCutscene2Hooks();
        while (true) {
            v3 = Pump(FrameFlagsWithHiBit(), static_cast<i32>(start),
                      reinterpret_cast<void*>(static_cast<intptr_t>(1)));
            if (!v3) break;                        // if (!v3) break;
            // gilde.exe 0x4aa91f: (double)(unsigned)dword_62EB38 >= (double)v4*scale
            // + (double)v1, where v1 (start) is an `int` -> SIGNED to-double.
            if (static_cast<double>(g_state.gameTick) >=
                static_cast<double>(frames) * kFrameMsScale
                    + static_cast<double>(static_cast<i32>(start)))
                g_state.forceQuit = 1;             // dword_631614 = 1
            if (h.combatUpdateDamageNumbers) h.combatUpdateDamageNumbers();
            if (h.showParticipantDialog) h.showParticipantDialog(dialogSlot);
        }
    }
    return v3;
}

// ===========================================================================
// gilde.exe 0x4aa7b0 — VIBE_Cutscene_RunCombatScript
// ===========================================================================
void CutsceneRunCombatScript(i32 deadline, void* payload, i32 dialogSlot) {
    if (!g_state.replayGate) {                     // if (!dword_6315BC)
        const Cutscene2Hooks& h = GetCutscene2Hooks();
        while (true) {
            if (!Pump(FrameFlagsWithHiBit(), deadline, payload))
                break;                             // pump stopped
            if (deadline <= g_state.tickCounter)   // if (v7 <= dword_6315A8)
                g_state.forceQuit = 1;             // dword_631614 = 1
            if (h.combatUpdateDamageNumbers) h.combatUpdateDamageNumbers();
            if (h.showParticipantDialog) h.showParticipantDialog(dialogSlot);
        }
    }
    // original tail JUMPOUTs into the duel main; here a plain return.
}

// ===========================================================================
// gilde.exe 0x4aa450 — VIBE_Cutscene_FadeIn
// ===========================================================================
i32 CutsceneFadeIn(i32 a1, void* payload) {
    i32 result = 0;
    if (!g_state.replayGate) {                     // if (!dword_6315BC)
        const Cutscene2Hooks& h = GetCutscene2Hooks();
        void* fade = h.fadeRegister ? h.fadeRegister() : nullptr;
        // while (!(*(byte*)dword_6315D4 & 4)) pump frames
        while (true) {
            u8 doneByte = h.fadeDoneByte ? h.fadeDoneByte(fade) : 0x04; // inert: done
            if (doneByte & 0x04) break;            // bit2 set -> fade finished
            result = Pump(g_state.frameFlags | static_cast<i32>(kFrameLoopHiBit),
                          a1, payload);
        }
    }
    return result;
}

// ===========================================================================
// gilde.exe 0x4aa6a8 — VIBE_Cutscene_SetupSky
// ===========================================================================
void CutsceneSetupSky(unsigned int* /*a1*/) {
    if (g_state.replayGate) return;                // if (dword_6315BC) return;
    const Cutscene2Hooks& h = GetCutscene2Hooks();
    g_sky.sky = h.skyCreate ? h.skyCreate() : nullptr;       // dword_6315F0
    g_sky.mirror = g_sky.sky;                                // dword_64A7C8 = dword_6315F0
    g_sky.layer = h.skyCreateLayer ? h.skyCreateLayer(g_sky.sky) : nullptr; // dword_6315EC
    if (h.skyConfigLayer) h.skyConfigLayer(g_sky.sky, g_sky.layer);  // scroll + fade
}

// ===========================================================================
// gilde.exe 0x4aa740 — VIBE_Cutscene_DestroySky
// ===========================================================================
i32 CutsceneDestroySky() {
    i32 result = 0;
    if (!g_state.replayGate) {                     // if (!dword_6315BC)
        const Cutscene2Hooks& h = GetCutscene2Hooks();
        if (g_sky.layer && h.skyRemoveLayer)       // if (dword_6315EC)
            h.skyRemoveLayer(g_sky.sky, g_sky.layer);
        if (h.skyDestroy) h.skyDestroy(g_sky.sky); // Sky_Destroy(dword_6315F0)
        g_sky.mirror = nullptr;                    // dword_64A7C8 = 0  (ONLY the mirror)
        // NB: the binary does NOT clear dword_6315F0 / dword_6315EC here — the sky
        // and layer handles are left intact (0x4aa740). A second DestroySky would
        // re-RemoveLayer/Destroy the same handles, exactly as the original.
    }
    return result;
}

// ===========================================================================
// gilde.exe 0x4a6964 — VIBE_Cutscene_ShowDuelWindow
// ===========================================================================
i32 CutsceneShowDuelWindow(i32 a1, i32 a2, i32 a3) {
    const Cutscene2Hooks& h = GetCutscene2Hooks();
    if (g_state.duelMode)                          // if (dword_6315A4)
        return h.duelOutcomeWindow ? h.duelOutcomeWindow(a1, a2, a3) : 0;
    return h.duelChoiceWindow ? h.duelChoiceWindow(a1, a3) : 0;
}

// ===========================================================================
// gilde.exe 0x4a7a64 — VIBE_Cutscene_CheckBirthParticipants
// ===========================================================================
i32 CutsceneCheckBirthParticipants(const CutsceneSlot* slot) {
    const Cutscene2Hooks& h = GetCutscene2Hooks();
    if (!h.resolvePerson) return 0;                // no record table -> abort
    // a1[13] / a1[14] == slot +52 / +56 == partIds[0] / partIds[1].
    void* a = h.resolvePerson(slot->partIds[0]);   // RecordById
    void* b = h.resolvePerson(slot->partIds[1]);   // v4
    if (!a || !b)                                  // if (!v5 || !v4) return 0;
        return 0;
    // v8 = Person_FindRecordById(a[+92]);  // a's parent
    i32 parentId = h.personParentId ? h.personParentId(a) : -1;
    void* parent = h.resolvePerson(parentId);
    u8 parentKind = parent && h.personKind ? h.personKind(parent) : 0;
    if (!parent || parentKind == kPersonKindDeceased) {  // !v8 || kind == 15
        // QueueRequestPair33(b.id, 1); return 0;
        if (h.queueBirthFailure) h.queueBirthFailure(slot->partIds[1]);
        return 0;
    }
    u8 kind = h.personKind ? h.personKind(a) : 0;  // v10 = a[+2]
    if (kind == kPersonKindBride || kind == kPersonKindGroom) {  // 6 || 7
        if (h.buildSpeechPacket) h.buildSpeechPacket(a, slot);
        return 1;
    }
    return 1;
}

// ===========================================================================
// gilde.exe 0x4aa4a8 — VIBE_Cutscene_Teardown
// ===========================================================================
void CutsceneTeardown() {
    if (g_state.replayGate || g_state.nestDepth <= 0)   // !dword_6315BC && >0
        return;
    const Cutscene2Hooks& h = GetCutscene2Hooks();

    // if (dword_6315D0 != -1) { s = FindByHandle(dword_6315D0); if (s) Finish(s); }
    if (g_state.scriptHandleA != -1) {
        void* s = FindByHandle(g_state.scriptHandleA);
        if (s && h.scriptFinish) h.scriptFinish(s);
    }

    // The off_649D64 destructor call (+660) is a host leaf — sceneTeardown covers it.

    // busy-wait: while FindByHandle(dword_62E8DC) running (+164 & 1) pump frames
    if (g_state.scriptHandleB != -1) {
        while (true) {
            u8 flags = h.scriptHandleFlags ? h.scriptHandleFlags(g_state.scriptHandleB) : 0;
            if (!(flags & 1)) break;               // !v2 || !(v2[+164]&1)
            Pump(g_state.frameFlags | static_cast<i32>(kFrameLoopHiBit), 0, nullptr);
        }
    }

    // FinishPendingScripts(); WaitForPendingScripts();  (sibling-module leaves)
    if (h.sceneTeardown) {
        // sceneTeardown subsumes FinishPending/WaitPending + heightmap/anim free
        // + Universe_ResetCurrentSlot — all host-owned and tested there.
    }

    // busy-wait: while FindByHandle(dword_6315D0) running pump frames
    if (g_state.scriptHandleA != -1) {
        while (true) {
            u8 flags = h.scriptHandleFlags ? h.scriptHandleFlags(g_state.scriptHandleA) : 0;
            if (!(flags & 1)) break;
            Pump(g_state.frameFlags | static_cast<i32>(kFrameLoopHiBit), 0, nullptr);
        }
    }

    if (h.sceneTeardown) h.sceneTeardown();        // scene/anim/heightmap free

    --g_state.nestDepth;                           // --dword_6315CC
}

// ===========================================================================
// gilde.exe 0x4aab0c — VIBE_Cutscene_RestoreParticipantState
// ===========================================================================
i32 CutsceneRestoreParticipantState(const CutsceneSlot* slot, i32 person,
                                    const i32* ids, int count) {
    const Cutscene2Hooks& h = GetCutscene2Hooks();
    int n = slot->partCount;                       // *(u8*)(v7 + 48)
    for (int i = 0; i < n; ++i) {
        if (i < count && person == ids[i]) {       // a2 == dword_11AB010[v3]
            // stage cmd28 (flags 1/1/1, byte5=0, [2]=0) + RequestSendCutInfo +
            // busy-wait packet status -> one restore broadcast for this part.
            if (h.restoreBroadcast) h.restoreBroadcast(person, slot->id);
        }
    }
    return n;                                       // return slot[+48]
}

}  // namespace guild::sim
