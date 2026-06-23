// world_buildingflag_util_recon2 — see header. 1:1 from gilde.exe.
#include "sim/world_buildingflag_util_recon2.h"

namespace guild::sim {

// gilde.exe 0x582F20 (= dword_582908 + 0x618) — exact bytes (get_bytes):
//   01 03 04 0B 0D 0E 12 13 15 16 1A 1B 1C 1F 21 00
const guild::u8 kBuildingKindSet[16] = {
    0x01, 0x03, 0x04, 0x0B, 0x0D, 0x0E, 0x12, 0x13,
    0x15, 0x16, 0x1A, 0x1B, 0x1C, 0x1F, 0x21, 0x00,
};

// gilde.exe 0x583990 — VIBE_World_LookupBuildingTypeFlag
//
// Disassembly walked:
//   movsx ecx, word ptr [edx]      ; type = *(i16*)typePtr  (signed)
//   mov   edx, ecx ; shl edx,6 ; add ecx,edx   ; ecx = type*65
//   mov   edx, dword_13CE27C ; add edx, ecx     ; edx = base + 65*type
//   mov   bl, set[0] ; test bl,bl ; jz ret0
//   movsx ecx, byte ptr [edx]       ; kind = (signed char)*recBase
//   loop: edx = (signed char)set[i]; if (kind == edx) return set[i];
//         bh = set[i+1]; ++i; if (bh != 0) loop;  else return 0;
guild::u8 World_LookupBuildingTypeFlag(const guild::i16* typePtr,
                                       const guild::u8* kindRecordBase,
                                       const guild::u8* flagSet) {
    // type sign-extended from the i16 record field (movsx word).
    const int type = static_cast<int>(*typePtr);
    // base + 65*type  -> SceneTypeDef::kind byte for this type.
    const guild::u8* recBase = kindRecordBase + 65 * type;

    if (flagSet[0] == 0)  // bl = set[0]; jz loc_5839D7 -> return 0
        return 0;

    // kind read as signed char (movsx byte ptr [edx]).
    const signed char kind = static_cast<signed char>(*recBase);

    int i = 0;
    for (;;) {
        // edx = [esp+i-3]; sar edx,0x18  -> (signed char)set[i]
        const signed char cur = static_cast<signed char>(flagSet[i]);
        if (kind == cur)
            return flagSet[i];          // mov al, set[i]; return
        const guild::u8 next = flagSet[i + 1];  // bh = set[i+1]
        ++i;                                    // inc eax
        if (next == 0)                          // test bh,bh; jz -> fall to ret0
            return 0;
    }
}

guild::u8 World_LookupBuildingTypeFlag(const guild::i16* typePtr,
                                       const guild::u8* kindRecordBase) {
    return World_LookupBuildingTypeFlag(typePtr, kindRecordBase, kBuildingKindSet);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5b371c — VIBE_Universe_ApplyHiddenToggle
namespace {
SceneWalkInvokeFn    g_walk    = nullptr;
void*                g_root    = nullptr;
ObjectToggleHiddenFn g_toggle  = nullptr;
LightRefreshAllFn    g_refresh = nullptr;
}  // namespace

void SetUniverseHiddenToggleHooks(SceneWalkInvokeFn walk,
                                  void* rootNode,
                                  ObjectToggleHiddenFn toggle,
                                  LightRefreshAllFn refresh) {
    g_walk    = walk;
    g_root    = rootNode;
    g_toggle  = toggle;
    g_refresh = refresh;
}

guild::u8 Universe_ApplyHiddenToggle(guild::u8 newState) {
    // VIBE_SceneGraph_WalkAndInvoke(off_649D64, 0, VIBE_Object_ToggleHiddenState,
    //                               6, newState);
    if (g_walk)
        g_walk(g_root, 0, g_toggle, 6, newState);
    // return VIBE_Light_RefreshAllObjects(1u);
    if (g_refresh)
        return g_refresh(1u);
    return 0;
}

} // namespace guild::sim
