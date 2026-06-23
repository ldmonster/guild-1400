// ===========================================================================
// object_lifecycle10.cpp — see object_lifecycle10.h for the module overview and
// the per-function provenance index. namespace guild::sim.
// ===========================================================================
#include "sim/object_lifecycle10.h"

#include "render/mesh_attach_textures.h"  // render::frame:: shared draw-block LP64 layout

#include <cstring>

namespace guild::sim {

// ---------------------------------------------------------------------------
// Module state.
// ---------------------------------------------------------------------------
namespace {
ObjLife10Hooks g_hooks;

// Default faithful raw-byte clone of VIBE_Object_InitSubMeshEntry (0x5b0e18 — the
// REAL one lives in object_lifecycle4; this default mirrors its byte semantics so
// AllocDrawData stays self-contained when no hook is installed):
//   *(e+376)=0; *e=0; *(e+8)=0; *(e+4)=0; *(e+12)=0; *(e+16)=0; *(e+24)=node;
//   v2 = e+348; *(byte)(e+376)=-1;
//   do { e += 116; *(e-84)=0; *(e+20)=0; v3=*(e-84); *(e-80)=v3; *(e-88)=v3;
//        *(e+16)=0; *(byte)(e+22) &= 0xFD; } while ( e != v2 );   // 3 sub-slots
void DefaultInitSubMeshEntry(u8* entry, void* node) {
    auto wr = [](u8* p, i32 v) { std::memcpy(p, &v, 4); };
    wr(entry + 376, 0);
    wr(entry + 0, 0);
    wr(entry + 8, 0);
    wr(entry + 4, 0);
    wr(entry + 12, 0);
    wr(entry + 16, 0);
    i32 nodeSlot = static_cast<i32>(reinterpret_cast<std::intptr_t>(node));
    wr(entry + 24, nodeSlot);                 // original stores the 32-bit node slot
    u8* v2 = entry + 348;
    entry[376] = 0xFF;
    u8* p = entry;
    do {
        p += 116;
        wr(p - 84, 0);
        wr(p + 20, 0);
        wr(p - 80, 0);                         // = *(p-84) (just zeroed)
        wr(p - 88, 0);
        wr(p + 16, 0);
        p[22] = static_cast<u8>(p[22] & 0xFD); // clear bit1
    } while (p != v2);
}
}  // namespace

void ObjLife10SetHooks(const ObjLife10Hooks& hooks) { g_hooks = hooks; }
void ObjLife10ResetHooks() { g_hooks = ObjLife10Hooks(); }

// ===========================================================================
// 0x5b0e88 — VIBE_Object_InitStruct (eax = fn(node@eax, block@ecx)).
//   v3 = a1 + 60 (dwords) = node + 240;
//   do { a1 += 6 (dwords) = +24; a1[34]=0; a1[35]=0; a1[36]=0; a1[37]=0; a1[38]=0;
//        a1[33]=0; } while ( a1 != v3 );
//   v2[125]=0; v2[131]=0; *(byte)(v2+533)=1; *(byte)(v2+535)=0; v2[134]=0;
//   v2[16]=0; v2[116]=0; v2[117]=0; v4=v2[125](=0); v2[124]=v4; v2[127]=v4;
//   v2[126]=v4;
//   *(byte)(v2+529)=*(byte)(v2+529)&0xA0 then |2; *(byte)(v2+528)&=0x7D;
//   *(byte)(v2+528) = (*(byte)(v2+528)&0x9A); v2[17]=0; *(byte)(v2+532)=0;
//   *(byte)(v2+528) = that | 0x65;
//   *(byte)(v2+530) = *(byte)(v2+530)&0xC1; *(byte)(v2+531) = *(byte)(v2+531)&0xCF;
//   if ( v2[122] ) { *(v9+408)=0; *(v9+416)=15; *(v9+412)=0.1f; *(v9+420)=0;
//                    for ( i=0; i!=392; <inc:*(v9+i-8)=0.1f> ) { *(v9+i+52)=15;
//                                                                 i+=56; } }
//   v2[27..32]=0; memset(v14,0,12); SetWorldTranslation(v2, v14);
//   memset(v13,0,12); return SetPosition(v2, v13);
// v11 in the original SetPosition call aliases v2 (ecx still holds the node ptr).
// ===========================================================================
void* ObjectInitStruct(SceneNode10* node, void* lightBlock) {
    if (!node) return nullptr;
    auto WD = [&](int dwordIdx, i32 v) { node->d(4 * dwordIdx) = v; };

    int a1 = 0;                                               // running byte base
    do {
        a1 += 24;                                             // 0x5b0e96 (6 dwords)
        node->d(a1 + 4 * 34) = 0;                             // a1[34]
        node->d(a1 + 4 * 35) = 0;                             // a1[35]
        node->d(a1 + 4 * 36) = 0;                             // a1[36]
        node->d(a1 + 4 * 37) = 0;                             // a1[37]
        node->d(a1 + 4 * 38) = 0;                             // a1[38]
        node->d(a1 + 4 * 33) = 0;                             // a1[33]
    } while (a1 != 240);                                      // v3 = node+240

    WD(125, 0);                                               // 0x5b0ed9
    WD(131, 0);                                               // 0x5b0ee3
    node->b(533) = 1;                                         // 0x5b0eed (attachKind)
    node->b(535) = 0;                                         // 0x5b0ef4 (upgradeState)
    WD(134, 0);                                               // 0x5b0efb
    WD(16, 0);                                                // 0x5b0f05
    WD(116, 0);                                               // 0x5b0f0c
    WD(117, 0);                                               // 0x5b0f16
    i32 v4 = node->d(4 * 125);                                // 0x5b0f20 (== 0)
    WD(124, v4);                                              // 0x5b0f26
    WD(127, v4);                                              // 0x5b0f32
    WD(126, v4);                                              // 0x5b0f3b

    node->b(529) = node->b(529) & 0xA0;                       // 0x5b0f41
    u8 v6 = node->b(529) | 2;                                 // 0x5b0f56
    node->b(528) = node->b(528) & 0x7D;                       // 0x5b0f59
    node->b(529) = v6;                                        // 0x5b0f5f
    u8 v7 = node->b(528);                                     // 0x5b0f65
    WD(17, 0);                                                // 0x5b0f6b
    v7 &= 0x9A;                                               // 0x5b0f72
    node->b(532) = 0;                                         // 0x5b0f75
    node->b(528) = v7;                                        // 0x5b0f7e
    u8 v8 = node->b(530);                                     // 0x5b0f86
    node->b(528) = v7 | 0x65;                                 // 0x5b0f8c
    node->b(530) = v8 & 0xC1;                                 // 0x5b0f9b
    node->b(531) = node->b(531) & 0xCF;                       // 0x5b0faa

    if (lightBlock) {                                         // 0x5b0fb2 (v9 = v2[122])
        u8* lb = reinterpret_cast<u8*>(lightBlock);
        auto LB = [&](int off, i32 v) { std::memcpy(lb + off, &v, 4); };
        LB(408, 0);                                           // 0x5b0fb4
        LB(416, 15);                                          // 0x5b0fc4
        LB(412, kInitFloatBits);                              // 0x5b0fd4 (0.1f)
        LB(420, 0);                                           // 0x5b0fe4
        // for ( i=0; i!=392; <inc: *(lb+i-8)=0.1f> ) { *(lb+i+52)=15; i+=56; }
        // The "-8" store runs in the loop's increment clause (AFTER i+=56), so it
        // uses the post-increment index. Replicate that ordering exactly.
        for (int i = 0; i != 392;) {                          // 0x5b0fee
            LB(i + 52, 15);                                   // 0x5b0ff6 (body)
            i += 56;                                          // 0x5b0ffe
            LB(i - 8, kInitFloatBits);                        // 0x5b0fee (increment)
        }
    }

    WD(27, 0); WD(28, 0); WD(29, 0);                          // 0x5b1018..
    WD(30, 0); WD(31, 0); WD(32, 0);

    float v14[3] = {0.0f, 0.0f, 0.0f};                        // memset(v14,0,12)
    if (g_hooks.objSetWorldTranslation)                       // 0x5b1059
        g_hooks.objSetWorldTranslation(node, v14);
    float v13[3] = {0.0f, 0.0f, 0.0f};                        // memset(v13,0,12)
    if (g_hooks.objSetPosition)                               // 0x5b1077
        g_hooks.objSetPosition(node, v13);                    // v11 aliases node
    return node;
}

// ===========================================================================
// 0x5b107c — VIBE_Object_AllocDrawData (eax = fn(node@eax)).
//   if ( !*(node+492) ) {
//     v1 = AllocDebug(0x910, "d3:AddObjectDra...");
//     *(node+492) = v1; v3 = 0;
//     do { v4 = v3 + *(node+492) + 244; v3 += 384;
//          InitSubMeshEntry(v4, node); } while ( v3 != 1536 );  // 4 entries
//     for ( i=0; i!=512; <inc:*(+i+1652)=*(+i+1656)> ) {
//          *(+i+1804)=0; *(+i+1808)=0; *(+i+1812)=0; i+=128; *(+i+1656)=0; }
//     *(byte)(+2316)=0; *(+0)=0; *(+2304)=0; *(+2308)=0; *(+2312)=0; *(+2292)=0;
//     *(+2296)=1065353216(=1.0f); *(+2300)=0; *(+176)=-1; *(byte)(+2317) |= 0x40;
//   }
//   return *(node+492);
// v2 (the node base reloaded into ecx) and v6 (the +492 ptr reloaded) are benign
// register-aliasing artifacts; both equal the same node / draw block.
// ===========================================================================
void* ObjectAllocDrawData(SceneNode10* node) {
    if (!node) return nullptr;
    if (node->p(n10::kDrawData) != nullptr)                   // 0x5b107f
        return node->p(n10::kDrawData);

    void* blk = g_hooks.memAllocDebug
                    ? g_hooks.memAllocDebug(kDrawBlockSize, "d3:AddObjectDrawData")
                    : nullptr;                                // 0x5b1096
    node->p(n10::kDrawData) = blk;
    if (!blk) return blk;                                     // alloc failed (inert)

    u8* draw = reinterpret_cast<u8*>(blk);
    auto WB = [&](int off, i32 v) { std::memcpy(draw + off, &v, 4); };

    auto initEntry = g_hooks.initSubMeshEntry ? g_hooks.initSubMeshEntry
                                              : DefaultInitSubMeshEntry;
    for (int v3 = 0; v3 != 1536; v3 += kSubMeshStride)        // 0x5b10c3 (4 entries)
        initEntry(draw + v3 + kSubMeshFirst, node);           // 0x5b10b8

    // for ( i=0; i!=512; <inc: *(+i+1652)=*(+i+1656)> ) {
    //   *(+i+1804)=0; *(+i+1808)=0; *(+i+1812)=0; i+=128; *(+i+1656)=0; }
    // +1804/+1808/+1812 use the pre-increment i; +1656 and the inc-clause +1652 use
    // the post-increment i (and +1652 copies the 0 just written into +1656).
    for (int i = 0; i != 512;) {                              // 0x5b10c5
        WB(i + 1804, 0);                                      // 0x5b10cd (body, pre)
        WB(i + 1808, 0);                                      // 0x5b10de
        WB(i + 1812, 0);                                      // 0x5b10ef
        i += 128;                                             // 0x5b10fa
        WB(i + 1656, 0);                                      // 0x5b1105 (post)
        WB(i + 1652, 0);                                      // 0x5b10c5 (= +1656 == 0)
    }
    draw[2316] = 0;                                           // 0x5b1131
    WB(0, 0);                                                 // 0x5b113e
    WB(2304, 0);                                              // 0x5b114a
    WB(2308, 0);                                              // 0x5b115a
    WB(2312, 0);                                              // 0x5b116a
    WB(2292, 0);                                              // 0x5b117a
    WB(2296, kOneFloatBits);                                  // 0x5b118a (1.0f)
    WB(2300, 0);                                              // 0x5b119a
    WB(176, -1);                                              // 0x5b11aa
    draw[2317] = static_cast<u8>(draw[2317] | 0x40);          // 0x5b11ba
    return blk;
}

// ===========================================================================
// 0x5b3e30 — VIBE_Object_AttachToUniverseNode
//            (eax = fn(parent@eax, pos@edx, model@ecx, xlate@ebx, ctx@edi)).
//   result = Object_Spawn(4, model); v19 = result;
//   if ( result ) {
//     if ( parent ) SetParent(parent, result);
//     if ( !Mesh_FindStockObject() ) Mesh_LoadOrFindByName(model, model);
//     Mesh_AttachStockObjectLods(v19, 0, model, ctx);
//     v7 = v19[123];
//     if ( v7 && *(v7+260) ) {                       // has resident submeshes
//       v8 = 0;
//       for ( i=0;; ++i ) {
//         v10 = v19[123]; if ( i >= *(byte)(v10+2316) ) break;
//         v12 = *(v10+v8+260);
//         if ( v12 ) { v13 = *(v12+480); v14 = *(v10+v8+264);
//           for ( j=0; j<v13; ++v14 ) if ( *v14 ) Texture_UploadToSurface(*v14,0,v14,i);
//             ++j; }
//         v8 += 384; }
//       SetPosition(v19, pos); SetWorldTranslation(v19, xlate);
//       if ( !parent ) LinkIntoScene(v19); return v19;
//     } else { Dispose(v19); return 0; } }
//   return result;
// v6 (the model name reloaded into ecx) is a register artifact == model. The texture
// surface walk reads opaque draw-block sub-arrays; it is routed through the texture
// hook (default noop) so the deterministic node-link flow stays exact.
// ===========================================================================
void* ObjectAttachToUniverseNode(void* parent, const float* pos, const char* model,
                                 const float* xlate, void* ctx) {
    void* node = g_hooks.objSpawn ? g_hooks.objSpawn(4, model) : nullptr;  // 0x5b3e46
    if (!node) return node;                                   // 0x5b3e51
    SceneNode10* n = reinterpret_cast<SceneNode10*>(node);

    if (parent && g_hooks.objSetParent)                       // 0x5b3e5c
        g_hooks.objSetParent(parent, n);
    int stock = g_hooks.meshFindStockObject ? g_hooks.meshFindStockObject() : 0;
    if (!stock && g_hooks.meshLoadOrFind)                     // 0x5b3e64
        g_hooks.meshLoadOrFind(model);                        // v6 == model
    if (g_hooks.meshAttachLods)                               // 0x5b3e7e
        g_hooks.meshAttachLods(n, model);

    // RECONCILIATION (rule 1/3): the engine reads `*(v7 + 384*i + 260)` (the resident
    // gate / "LOD-mesh"==a2[4] STOCK ptr) and `*(v11 + 264)` (a2[5] texSET array).
    // Engine +260/+264 == LOD-frame_base(244)+16/+20 == a2[4]/a2[5]. AttachStockTextures
    // (the WRITER, mesh_attach_textures.cpp) stores those two pointer slots at the LP64-
    // relocated frame offsets render::frame::kStockSlot(24) / kTexSetSlot(32) so two
    // 8-byte native pointers don't overlap. We read at the SAME relocated slots so the
    // live writer/reader agree byte-for-byte (the shared render::frame contract). The
    // texture count `*(v12+480)` is the stock object's material count (stock+480 ==
    // render::stockrec::kMatCount). On a 32-bit build these collapse to the verbatim
    // engine +260/+264/+480.
    constexpr int kStockGate = render::frame::kLodFrameBase + render::frame::kStockSlot;
    constexpr int kTexSet    = render::frame::kLodFrameBase + render::frame::kTexSetSlot;
    void* drawBlk = n->p(n10::kDrawData);                     // v7 = v19[123]
    bool resident = false;
    if (drawBlk)                                              // 0x5b3e95
        resident = (BlockPtr(drawBlk, kStockGate) != nullptr);  // *(v7+260) == a2[4]
    if (!resident) {                                          // 0x5b3f46 path
        if (g_hooks.objDispose) g_hooks.objDispose(n);
        return nullptr;
    }

    int v8 = 0;
    for (int i = 0;; ++i) {                                   // 0x5b3ea6
        u8* d10 = static_cast<u8*>(n->p(n10::kDrawData));     // v10 reload
        if (static_cast<unsigned>(i) >= d10[2316]) break;     // 0x5b3ebc count
        void* v12 = BlockPtr(d10, v8 + kStockGate);           // 0x5b3ec1 ([ecx+104h]) stock
        if (v12) {
            i32 v13;
            std::memcpy(&v13, static_cast<u8*>(v12) + 480, 4);  // 0x5b3ee1 stock+480 matCount
            u8* v14 = static_cast<u8*>(BlockPtr(d10, v8 + kTexSet));  // 0x5b3ee7 ([ecx+108h]) texSET
            for (int j = 0; j < v13; ++j, v14 += sizeof(void*)) {  // 0x5b3ef1
                void* surf = BlockPtr(v14, 0);
                if (surf && g_hooks.textureUploadToSurface)   // 0x5b3efd
                    g_hooks.textureUploadToSurface(surf, v14, i);
            }
        }
        v8 += render::frame::kLodFrameStride;                 // 0x5b3ecc (384)
    }
    if (g_hooks.objSetPosition) g_hooks.objSetPosition(n, pos);          // 0x5b3f14
    if (g_hooks.objSetWorldTranslation)                                  // 0x5b3f25
        g_hooks.objSetWorldTranslation(n, xlate);
    if (!parent && g_hooks.objLinkIntoScene)                             // 0x5b3f2c
        g_hooks.objLinkIntoScene(n);
    (void)ctx;
    return n;                                                            // 0x5b3f37
}

// ---------------------------------------------------------------------------
// Shared core for FindByHandle / FindByName (the two functions are byte-identical
// apart from the Match* predicate they pass to WalkAndInvoke). The Match* predicates
// themselves are the REAL object_lifecycle3 siblings (0x5b7b7c / 0x5b7c48), reused
// here verbatim (NOT re-defined).
//   if ( !handle && !name ) return 0;
//   v7[0]=name; v7[1]=handle; v8=0;                    // == FindCtx{queryStr,queryNode,found}
//   if ( root ) { a5 = root[124]; root[124] = 0; }     // snapshot + detach link
//   HIBYTE(walkFlags) |= 2;
//   WalkAndInvoke(off_649D64, root, MatchCb, walkFlags, v7);
//   if ( root ) root[124] = a5;                        // restore link
//   return v8;   (== v7[2] == ctx.found, the matched node)
// SceneNode3's selfLink (+496, a1[124]) is followed as a native pointer (LP64).
// ---------------------------------------------------------------------------
static SceneNode3* FindCommon(SceneNode3* root, u16 walkFlags, const char* name,
                              SceneNode3* handle, void* matchCb) {
    if (!handle && !name) return nullptr;                     // 0x5b7bf0 / 0x5b7cbc
    FindCtx q;
    q.queryStr = name;                                        // v7[0]
    q.queryNode = handle;                                     // v7[1]
    q.found = nullptr;                                        // v8 = 0 (== v7[2])

    void** linkSlot = root ? reinterpret_cast<void**>(root->raw + 496) : nullptr;
    void* savedLink = nullptr;
    if (linkSlot) {                                           // 0x5b7c03
        savedLink = *linkSlot;                                // a5 = root[124]
        *linkSlot = nullptr;                                  // root[124] = 0
    }
    u16 flags = static_cast<u16>(walkFlags | kWalkFlagHiBit); // HIBYTE |= 2
    if (g_hooks.sceneWalkAndInvoke)                           // 0x5b7c28
        g_hooks.sceneWalkAndInvoke(/*root list*/ nullptr, root, matchCb, flags, &q);
    if (linkSlot)                                             // 0x5b7c2f
        *linkSlot = savedLink;                                // restore root[124]
    return q.found;                                           // 0x5b7c3c (v8)
}

// 0x5b7be4 — VIBE_Object_FindByHandle (reuses the REAL ObjectMatchHandleCallback).
SceneNode3* ObjectFindByHandle(SceneNode3* root, u16 walkFlags, const char* name,
                               SceneNode3* handle, void* /*ctx*/) {
    return FindCommon(root, walkFlags, name, handle,
                      reinterpret_cast<void*>(&ObjectMatchHandleCallback));
}

// 0x5b7cb0 — VIBE_Object_FindByName (reuses the REAL ObjectMatchNameCallback).
SceneNode3* ObjectFindByName(SceneNode3* root, u16 walkFlags, const char* name,
                             SceneNode3* handle, void* /*ctx*/) {
    return FindCommon(root, walkFlags, name, handle,
                      reinterpret_cast<void*>(&ObjectMatchNameCallback));
}

// ===========================================================================
// 0x4fff10 — VIBE_Object_DestroySpawnedEntities (eax = fn(a1@ecx, ctx@edi)).
//   Universe_SwitchActiveSlot(0, 0, a1, ctx);
//   if ( dword_634488 ) { SceneGraph_FreeNodeRecursive(dword_634488);
//                         dword_634488 = 0; }
//   result = Universe_ResetCurrentSlot(ctx);
//   for ( i=0; i<731; ++i )
//     if ( byte_122DDB0[i] > 0 ) { Universe_SwitchActiveSlot(*(dword_122DDAC+i+1)>>24,
//                                  0, i, ctx); result = ResetCurrentSlot(ctx);
//                                  byte_122DDB0[i] = -1; }
//   for ( j=0; j<72; ++j )
//     if ( *(byte)(&dword_122DD5D+j+3) > 0 ) { SwitchActiveSlot(*(&dword_122DD5D+j)>>24,
//                                  0, j, ctx); result = ResetCurrentSlot(ctx);
//                                  *(byte)(&dword_122DD5D+j+3) = -1; }
//   for ( k=0; k!=15744; k+=246 )
//     if ( dword_13ECF78[k] ) { result = Heightmap_Free(dword_13ECF78[k], ctx, 0);
//                               dword_13ECF78[k] = 0; }
//   return result;
// The four void-call result reuses (v4/v7/v8/v10) are register artifacts. The two
// slot tables are 1-byte-stride packed tables modeled as caller-owned byte buffers;
// each slot's active flag is the sign byte at +4+i (A) / +3+j (B) and its universe-hi
// is the dword's top byte (see header). The heightmap pool is a dword buffer.
// ===========================================================================
int ObjectDestroySpawnedEntities(int firstSlot, void* objRoot, void** objRootSlot,
                                 const DestroyTables10& t, void* ctx) {
    auto SW = [&](int hi, int mode, int slot) {
        if (g_hooks.universeSwitchActiveSlot)
            g_hooks.universeSwitchActiveSlot(hi, mode, slot, ctx);
    };
    auto RC = [&]() -> int {
        return g_hooks.universeResetCurrentSlot
                   ? g_hooks.universeResetCurrentSlot(ctx)
                   : 0;
    };

    SW(0, 0, firstSlot);                                      // 0x4fff18 (a1)
    if (objRoot) {                                            // 0x4fff25
        if (g_hooks.sceneGraphFreeNodeRecursive)
            g_hooks.sceneGraphFreeNodeRecursive(objRoot);     // 0x4fffc1
        if (objRootSlot) *objRootSlot = nullptr;              // dword_634488 = 0
    }
    int result = RC();                                        // 0x4fff2b

    // Table A: byte_122DDB0[i] == slotsA[4+i] flag; hi = (*(int*)(slotsA+1+i))>>24.
    if (t.slotsA) {
        for (int i = 0; i < t.slotsACount; ++i) {             // 0x4fff30
            signed char flag = static_cast<signed char>(t.slotsA[4 + i]);
            if (flag > 0) {                                   // 0x4fff39
                i32 dw;
                std::memcpy(&dw, t.slotsA + 1 + i, 4);
                SW(dw >> 24, 0, i);                           // 0x4fff46
                result = RC();                                // 0x4fff4d
                t.slotsA[4 + i] = 0xFF;                       // byte_122DDB0[i] = -1
            }
        }
    }

    // Table B: flag at slotsB[3+j]; hi = (*(int*)(slotsB+j))>>24.
    if (t.slotsB) {
        for (int j = 0; j < t.slotsBCount; ++j) {             // 0x4fff63
            signed char flag = static_cast<signed char>(t.slotsB[3 + j]);
            if (flag > 0) {                                   // 0x4fff6c
                i32 dw;
                std::memcpy(&dw, t.slotsB + j, 4);
                SW(dw >> 24, 0, j);                           // 0x4fff79
                result = RC();                                // 0x4fff7e
                t.slotsB[3 + j] = 0xFF;                       // (...+j+3) = -1
            }
        }
    }

    if (t.heightmaps) {
        for (int k = 0; k != t.heightmapSpan; k += t.heightmapStride) {  // 0x4fff8f
            i32 h = t.heightmaps[k];
            if (h) {                                          // 0x4fff91
                void* handle =
                    reinterpret_cast<void*>(static_cast<std::intptr_t>(h));
                result = g_hooks.heightmapFree
                             ? g_hooks.heightmapFree(handle, ctx)
                             : 0;                             // 0x4fff9f
                t.heightmaps[k] = 0;                          // 0x4fffa4
            }
        }
    }
    return result;                                            // 0x4fffbb
}

}  // namespace guild::sim
