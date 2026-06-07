// Placeholder definitions for the leaves animation_playback.cpp calls that no
// translated module owns YET:
//
//   gilde.exe 0x5d3f10 — VIBE_Util_StrCmp                (util module; only
//                         VIBE_Util_StrCmpNoCase variants exist so far)
//   gilde.exe 0x5af38c — VIBE_Object_SetPosition         (object/scene module)
//   gilde.exe 0x5af50c — VIBE_Object_SetWorldTranslation (object/scene module)
//   gilde.exe 0x5af2c0 — VIBE_Object_PropagateDirtyFlag  (object/scene module)
//
// They are declared in render/animation_playback.h. Defining them ONCE here keeps
// the `guild` static library self-contained so every test executable links. The
// Object_* pushes are scene-graph state mutations; here they record the last
// pushed vector + a call count into test-observable globals so the playback math
// can be verified in isolation. When the owning agents translate the real bodies,
// DELETE the matching definition here (the duplicate-symbol error will flag any
// leftover).
//
// AnimStrCmp is a FAITHFUL reconstruction of VIBE_Util_StrCmp (a pure
// case-sensitive byte compare: 0 == equal, sign of the first differing byte
// otherwise) — the loop-flag / bone-name callers only branch on == 0, so the sign
// convention matches the original exactly.
#include "render/animation_playback.h"

namespace guild::render {

// gilde.exe 0x5d3f10 — VIBE_Util_StrCmp (faithful; 0 == equal).
int AnimStrCmp(const char* a, const char* b) {
    if (a == b)
        return 0;
    const unsigned char* pa = reinterpret_cast<const unsigned char*>(a);
    const unsigned char* pb = reinterpret_cast<const unsigned char*>(b);
    while (*pa == *pb) {
        if (*pa == 0)
            return 0;
        ++pa;
        ++pb;
    }
    return (*pa < *pb) ? -1 : 1;
}

// Test-observable scene-graph push capture (placeholders).
int   g_animObjSetPositionCalls = 0;
int   g_animObjSetWorldCalls    = 0;
int   g_animObjDirtyCalls       = 0;
float g_animLastPosition[3]     = {0.0f, 0.0f, 0.0f};
float g_animLastWorld[3]        = {0.0f, 0.0f, 0.0f};

// gilde.exe 0x5af38c — VIBE_Object_SetPosition (UNTRANSLATED placeholder).
int ObjectSetPosition(void* /*obj*/, const float* pos) {
    ++g_animObjSetPositionCalls;
    g_animLastPosition[0] = pos[0];
    g_animLastPosition[1] = pos[1];
    g_animLastPosition[2] = pos[2];
    return 0;
}

// gilde.exe 0x5af50c — VIBE_Object_SetWorldTranslation (UNTRANSLATED placeholder).
int ObjectSetWorldTranslation(void* /*obj*/, const float* pos) {
    ++g_animObjSetWorldCalls;
    g_animLastWorld[0] = pos[0];
    g_animLastWorld[1] = pos[1];
    g_animLastWorld[2] = pos[2];
    return 0;
}

// gilde.exe 0x5af2c0 — VIBE_Object_PropagateDirtyFlag (UNTRANSLATED placeholder).
int ObjectPropagateDirtyFlag(void* /*obj*/, u32 /*flag*/) {
    ++g_animObjDirtyCalls;
    return 1;
}

} // namespace guild::render
