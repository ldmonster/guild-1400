#include "sim/script_recon_purchase.h"
#include <cstdint>
#include <cstdio>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Hook tables (inert defaults). Null function pointers mean "engine edge not
// wired"; the logic below treats them as the original's failure/zero results.
// ---------------------------------------------------------------------------
static ScriptLoadRunHooks  g_loadRun;
static PurchaseScriptHooks g_purchase;
static int g_runTick = 0;          // dword_634494

const ScriptLoadRunHooks* SetScriptLoadRunHooks(const ScriptLoadRunHooks* h) {
    static ScriptLoadRunHooks prev;
    prev = g_loadRun;
    g_loadRun = h ? *h : ScriptLoadRunHooks{};
    return &prev;
}

const PurchaseScriptHooks* SetPurchaseScriptHooks(const PurchaseScriptHooks* h) {
    static PurchaseScriptHooks prev;
    prev = g_purchase;
    g_purchase = h ? *h : PurchaseScriptHooks{};
    return &prev;
}

int& PurchaseScriptRunTick() { return g_runTick; }

void ResetScriptReconPurchase() {
    g_loadRun  = ScriptLoadRunHooks{};
    g_purchase = PurchaseScriptHooks{};
    g_runTick  = 0;
}

// ===========================================================================
// 0x43c690 — VIBE_Script_LoadAndRunMain
// ===========================================================================
int Script_LoadAndRunMain(const char* name) {
    void* v1 = g_loadRun.loadFromScriptDir ? g_loadRun.loadFromScriptDir(name)
                                           : nullptr;        /*0x43c693*/
    if (!v1)                                                  /*0x43c69c*/
        return 0;                                             /*0x43c69e*/
    // 0x43c6a2..0x43c6a7: RunMain is called for its side effect, then the wrapper
    // returns edx == the LOADED SCRIPT pointer (`mov eax, edx`), NOT RunMain's
    // result.  (1:1 with the disasm; RunMain's return is discarded.)
    if (g_loadRun.runMain)
        g_loadRun.runMain(v1);
    return static_cast<int>(reinterpret_cast<intptr_t>(v1));  /*0x43c6a7 eax=edx*/
}

// ===========================================================================
// 0x43c6ac / 0x43c6d4 — VIBE_Script_LoadAndRunWithArg[Alt] (identical bodies)
// ===========================================================================
int Script_LoadAndRunWithArg(const char* name, int arg) {
    void* v1 = g_loadRun.loadFromScriptDir ? g_loadRun.loadFromScriptDir(name)
                                           : nullptr;        /*0x43c6b1*/
    if (!v1)                                                  /*0x43c6ba*/
        return 0;                                             /*0x43c6bc*/
    // 0x43c6c7..0x43c6d0: RunWithArgs(script, 1, *ecx) is called for its side
    // effect; the wrapper then returns edx == the LOADED SCRIPT pointer
    // (`mov eax, edx`), NOT RunWithArgs's result.  (`arg` is the already-
    // dereferenced *ecx operand the call site supplies.)
    if (g_loadRun.runWithArgs)
        g_loadRun.runWithArgs(v1, 1, arg);
    return static_cast<int>(reinterpret_cast<intptr_t>(v1));  /*0x43c6d0 eax=edx*/
}

// ===========================================================================
// 0x50588c — VIBE_Script_RunPurchaseLocationScript
// ===========================================================================
void* Script_RunPurchaseLocationScript(void* entity, void* locationRec) {
    void* activeByEntity = g_purchase.findActiveByEntity
                               ? g_purchase.findActiveByEntity(entity)
                               : nullptr;                     /*0x5058a0*/
    void* v6 = nullptr;                                       /*0x5058a2*/
    int   v21 = 0;                                            /*0x5058ad*/
    int   v22 = 0;                                            /*0x5058a6*/
    void* result = nullptr;                                   /*0x5058b4*/

    // Scan dword_11BB6A0[0..31] (byte offset 0..128 step 4) for the active record.
    for (int i = 0; i < kPurchasePersonSlots && !v6; ++i) {   /*0x5058d2*/
        void* v9 = g_purchase.personSlot ? g_purchase.personSlot(i) : nullptr; /*0x5058b6*/
        if (v9 && v9 == activeByEntity)                       /*0x5058c2*/
            v6 = activeByEntity;                              /*0x5058c4*/
    }

    if (!v6)
        return result;                                        // no valid partner

    // record+0x08 byte must be > 1.
    u8 stateByte = g_purchase.personStateByte ? g_purchase.personStateByte(v6) : 0;
    if (stateByte <= 1u)
        return result;

    // The partner building record = *((u32*)v6 + 97) (v6 + 0x184). The original
    // reads it both for the a2 key-match test (when a2 != 0) AND, unconditionally,
    // as the value returned on the run-success path (result = *(v6+0x184) @0x5059f9).
    // a2 null -> proceed; else compare the location key field (a2+2) with the
    // partner building's key field (building+0x30). On mismatch the original returns
    // the partner-building pointer (result was already set to it). /*0x5058ea*/
    void* partnerBuilding = g_purchase.personBuilding ? g_purchase.personBuilding(v6)
                                                      : nullptr; /*rec+0x184*/
    if (locationRec) {
        result = partnerBuilding;                              /*0x5058ea*/
        u32 locKey = g_purchase.locationKeyField
                         ? g_purchase.locationKeyField(locationRec) : 0;  /*a2+2*/
        u32 bldKey = g_purchase.buildingKeyField
                         ? g_purchase.buildingKeyField(partnerBuilding) : 0; /*bld+0x30*/
        if (locKey != bldKey)
            return result;                                    // mismatched partner
    }

    // Production vs. purchase key selection.
    if (g_purchase.isProductionType && g_purchase.isProductionType(entity)) { /*0x5058fe*/
        i8 typeByte = g_purchase.entityTypeByte ? g_purchase.entityTypeByte(entity) : 0;
        v21 = 589 * typeByte + g_purchase.productionKeyBase; /*0x50592b*/
    } else {
        i16 typeWord = g_purchase.locationTypeWord
                           ? g_purchase.locationTypeWord(locationRec) : 0;
        v22 = 65 * typeWord + g_purchase.purchaseKeyBase;    /*0x505995*/
    }

    int keyIndex = v22 ? v22 + 1 : v21 + 1;                  /*0x505... (v10)*/
    const char* key = g_purchase.keyString ? g_purchase.keyString(keyIndex) : "";

    char path[260];                                          /*0x505956 sprintf*/
    std::snprintf(path, sizeof(path), kEinkaufPathFmt, kScriptDirPrefix, key, key);

    void* resolved = g_purchase.resolvePath ? g_purchase.resolvePath(path) : nullptr; /*0x50596d*/
    result = resolved;
    if (!resolved)
        return result;                                       /*0x505974*/

    void* script = g_purchase.loadScript ? g_purchase.loadScript(path) : nullptr; /*0x5059be*/
    result = script;
    if (script) {                                            /*0x5059c7*/
        // The running-script handle, the RunWithArgs target and the returned
        // value all come from the PARTNER BUILDING record (v6+0x184 = ecx+0x184),
        // NOT the location record a2.  /*0x5059c9, 0x5059f1, 0x5059f9*/
        // Finish any script already running on the partner building (+0x28
        // handle != -1).
        int prevHandle = g_purchase.locationScriptHandle
                             ? g_purchase.locationScriptHandle(partnerBuilding) : -1; /*[bld+0x28]*/
        void* prevRunning = nullptr;
        if (prevHandle != -1)                                /*0x5059d5*/
            prevRunning = g_purchase.findByHandle
                              ? g_purchase.findByHandle(prevHandle) : nullptr; /*0x505a27*/
        if (prevRunning && g_purchase.finish)                /*0x5059d9*/
            g_purchase.finish(prevRunning);                  /*0x5059dd*/

        int tick = g_purchase.runWithArgs2
                       ? g_purchase.runWithArgs2(script, partnerBuilding) : 0; /*0x5059f1 (arg=bld)*/
        g_runTick = tick;                                    /*0x505a0b dword_634494=script[0x80]*/
        // 0x5059f9: result = *(v6+0x184) = the partner building record.
        result = partnerBuilding;
    } else {
        g_runTick = -1;                                      /*0x505a2b*/
    }
    return result;                                           /*0x505976*/
}

} // namespace guild::sim
