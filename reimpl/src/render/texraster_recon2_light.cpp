// =============================================================================
// guild::render — texraster_recon2: scene-light orchestration cluster. 1:1 from
// the Hex-Rays decompile (see texraster_recon2_light.h). Cross-module scene-graph
// and object helpers are boundary callees routed through hooks; the control flow,
// traversal order, octree free/rebuild guard, and the sun-rays flag-byte edits
// are reconstructed verbatim.
// =============================================================================
#include "render/texraster_recon2_light.h"

namespace guild::render {

namespace {
LightSceneHooks   g_hooks;
LightSceneGlobals g_glob;
const char kPLicht[]        = "pLicht";        // aPlicht 0x611cc8
const char kSonnenstrahlen[] = "sonnenstrahlen"; // aSonnenstrahlen 0x611cd0
}

void SetLightSceneHooks(const LightSceneHooks& h) { g_hooks = h; }
const LightSceneHooks& GetLightSceneHooks() { return g_hooks; }
LightSceneGlobals& LightScene() { return g_glob; }

// gilde.exe 0x42dc7c — VIBE_Light_CreateSunRays(a1@ecx, a2@edi)
int Light_CreateSunRays(int a1, int a2)
{
    // v8[5] = a1; v8[0] = 0; v8[2] = 0;
    // v2 = *(FindByHandle(0,8,"pLicht",0,a2) + 136); v8[1] = v2;
    int desc[8] = {0};
    desc[5] = a1;
    desc[0] = 0;
    desc[2] = 0;
    if (g_hooks.objectFindByHandle) {
        // FindByHandle returns the parent light object's handle; read its +136
        // field (the light-node link) via objectByteBase. v2 = *(parent+136).
        int lichtHandle = g_hooks.objectFindByHandle(0, 8, kPLicht, 0, a2);
        u8* lb = g_hooks.objectByteBase ? g_hooks.objectByteBase(lichtHandle) : nullptr;
        g_glob.plichtObj76 = lb ? *reinterpret_cast<int*>(lb + 136) : 0;
    }
    desc[1] = g_glob.plichtObj76;

    // v7 = sun XYZ (dword_13FCD1C +76/+80/+84)
    int xyz[3];
    xyz[0] = g_glob.sunPosX;   // *(dword_13FCD1C+76)
    xyz[1] = g_glob.sunPosY;   // *(dword_13FCD1C+80)
    xyz[2] = g_glob.sunPosZ;   // *(dword_13FCD1C+84)

    int v3 = 0;
    if (g_hooks.objectAttachToUniverseNode)
        v3 = g_hooks.objectAttachToUniverseNode(0, xyz, kSonnenstrahlen, desc);

    // Edit the object's flag bytes (resolve handle -> byte base).
    u8* o = g_hooks.objectByteBase ? g_hooks.objectByteBase(v3) : nullptr;
    if (o) {
        o[535] = 5;                                  // *(v3+535) = 5
        *reinterpret_cast<int*>(o + 536) = 1;        // *(v3+536) = 1
        u8 v4 = (u8)(o[531] & 0xFB);                 // v4 = *(v3+531) & 0xFB
        o[530] |= 0x0C;                              // *(v3+530) |= 0xC
        o[531] = v4;                                 // *(v3+531) = v4
        u8 v5 = (u8)(o[529] & 0xFD);                 // v5 = *(v3+529) & 0xFD
        o[529] = v5;                                 // *(v3+529) = v5
    }
    g_glob.sunRaysObj   = v3;                        // dword_62D564 = v3
    g_glob.sunRaysFlag1 = 1;                         // dword_62D568 = 1
    g_glob.sunRaysStamp = g_glob.frameStamp;         // dword_62D56C = dword_62EB38
    g_glob.sunRaysFlag2 = 0;                         // dword_62D570 = 0

    if (g_hooks.lightUpdateDayCycle) g_hooks.lightUpdateDayCycle(v3);  // 0x42ddf8
    return g_glob.sunRaysObj;                         // return dword_62D564
}

// gilde.exe 0x5047c0 — VIBE_Light_ApplyTorchEffects(a1@eax)
char Light_ApplyTorchEffects(int a1)
{
    // Stack layout: v8[0..31] (collected handles) immediately followed by v9
    // (count). Model as one contiguous array of 33 ints: [0..31] objs, [32]=count.
    int  buf[33] = {0};
    int* collected = buf;       // v8[32]
    int& count = buf[32];       // v9 (= v8[32])
    char result = 0;

    // result = WalkAndInvoke(root, a1, CollectTorchObject, 704, &v8). The
    // callback fills collected[] and count (via the shared buffer). v9=0 first.
    count = 0;
    if (g_hooks.walkAndInvoke)
        result = g_hooks.walkAndInvoke(g_glob.universeRoot,
                                       reinterpret_cast<void*>(static_cast<intptr_t>(a1)),
                                       g_hooks.sceneCollectTorchObject,
                                       704, buf);

    int v3 = 0;                 // v3
    if (count > 0) {
        int v4 = 0;             // v4 (index into collected)
        do {
            int child = g_hooks.objChildHead ? g_hooks.objChildHead(collected[v4]) : 0; // *(obj+508)
            if (child) {
                do {
                    int next = g_hooks.objChildNext ? g_hooks.objChildNext(child) : 0;   // *(child+496)
                    i8  ty   = g_hooks.objChildType ? g_hooks.objChildType(child) : 0;    // *(char*)(child+533)
                    if (ty >= 5) {                                                       // cmp ah,5 / jl
                        static const char kRLicht[] = "rLicht"; // aRlicht 0x620ee8
                        if (g_hooks.gatePredicate && g_hooks.gatePredicate(child, kRLicht)) // loc_5CB930(child,"rLicht")
                            if (g_hooks.objectReparentWithTransform)
                                g_hooks.objectReparentWithTransform(child, a1);          // (child@eax, a1@edx)
                    }
                    child = next;            // v5 = v6
                } while (child);
            }
            if (g_hooks.objectDetachAndRelease)
                result = g_hooks.objectDetachAndRelease(collected[v4]);  // *0x50483f
            ++v3;
            ++v4;
        } while (v3 < count);
    }
    return result;
}

// gilde.exe 0x504860 — VIBE_Light_RefreshTorchLighting (octree rebuild guard)
u8 Light_RefreshTorchLighting()
{
    // TraverseTree(root, 0, ApplyTorchEffects, 192);
    if (g_hooks.traverseTree)
        g_hooks.traverseTree(g_glob.universeRoot, 0, g_hooks.lightApplyTorchEffectsCb, 192);
    if (g_glob.octreeRoot) {                          // if (dword_634488)
        if (g_hooks.freeNodeRecursive) g_hooks.freeNodeRecursive(g_glob.octreeRoot);
        if (g_hooks.buildOctreeForRegion)
            g_glob.octreeRoot = g_hooks.buildOctreeForRegion(0, 64, /*v1 (uninit)*/0u, 8u);
        else
            g_glob.octreeRoot = 0;
    }
    if (g_hooks.lightRefreshAllObjects)
        return g_hooks.lightRefreshAllObjects(1u);    // RefreshAllObjects(1)
    return 0;
}

// gilde.exe 0x504a00 — VIBE_Light_EnableDaylight
int Light_EnableDaylight()
{
    if (g_glob.octreeRoot) {                          // if (dword_634488)
        if (g_hooks.freeNodeRecursive) g_hooks.freeNodeRecursive(g_glob.octreeRoot);
        g_glob.octreeRoot = 0;                        // dword_634488 = v1 (=0)
    }
    // TraverseTree(root, 0, CharacterFlagRedrawByMode, 64);
    if (g_hooks.traverseTree)
        g_hooks.traverseTree(g_glob.universeRoot, 0, g_hooks.characterFlagRedrawByMode, 64);
    int newRoot = 0;
    if (g_hooks.buildOctreeForRegion)
        newRoot = g_hooks.buildOctreeForRegion(0, 64, 7u, 8u);
    g_glob.octreeRoot = newRoot;                      // dword_634488 = result
    return newRoot;
}

// gilde.exe 0x5c7d70 — VIBE_Light_RegisterUpdateCallbacks
char Light_RegisterUpdateCallbacks()
{
    if (g_hooks.traverseTree)
        g_hooks.traverseTree(g_glob.universeRoot, 0, g_hooks.lightRefreshChildBrightness, 4);
    if (g_hooks.traverseTree) {
        g_hooks.traverseTree(g_glob.universeRoot, 0, g_hooks.lightUpdateFlickerIntensity, 4);
        return 1; // mirrors returning the 2nd TraverseTree's (char) result
    }
    return 0;
}

} // namespace guild::render
