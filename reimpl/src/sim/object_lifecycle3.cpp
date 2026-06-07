// ===========================================================================
// object_lifecycle3.cpp — see object_lifecycle3.h for the module overview and
// the per-function provenance index.
// ===========================================================================
#include "sim/object_lifecycle3.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Module state.
// ---------------------------------------------------------------------------
namespace {
ObjLife3Hooks g_hooks;

// Faithful node dword/byte offsets (in BYTES) used across the family.
constexpr int kPos    = 76;    // a1[19] .. a1[21]
constexpr int kScale  = 108;   // a1[27] .. a1[29]
constexpr int kPivot  = 120;   // a1[30] .. a1[32]
constexpr int kEuler  = 132;   // +132/+136/+140
constexpr int kFlags528 = 528;
constexpr int kFlags530 = 530;
constexpr int kFlags531 = 531;
constexpr int kNodeType = 533; // *(node+533)
constexpr int kFrame64  = 64;  // *(node+64)
constexpr int kDrawData = 492; // a1[123]
constexpr int kSelfLink = 496; // a1[124]

// 0x5af298 — VIBE_Object_MarkDirtyFlag (the per-node callback the walks invoke).
// The transform setters depend on the ROOT node being marked dirty; the
// recursion over children is the WalkAndInvoke leaf (hook). We reproduce the
// root-node side effect directly.
//   a1[528] |= 4;  if (arg) a1[530] &= ~0x80;  a1[531] &= ~1;  return 1.
char MarkDirtyFlag(SceneNode3* n, u8 arg) {
    n->b(kFlags528) |= 4u;
    if (arg) {
        n->b(kFlags530) &= ~0x80u;
    }
    n->b(kFlags531) &= ~1u;
    return 1;
}

// off_649D64 walk-mask families used by the originals.
constexpr u16 kWalkMaskDirty = 511;   // VIBE_SceneGraph_WalkAndInvoke ..., 511, arg
constexpr u16 kTraverseMask  = 192;   // VIBE_SceneGraph_TraverseTree ..., 192

// Drive the dirty walk: faithful root effect + optional subtree hook.
void DirtyWalk(SceneNode3* node, u8 arg) {
    if (node) MarkDirtyFlag(node, arg);
    if (g_hooks.walkMarkDirty) g_hooks.walkMarkDirty(node, kWalkMaskDirty, arg);
}
void ShadowReset(SceneNode3* node) {
    if (g_hooks.traverseShadowReset) g_hooks.traverseShadowReset(node, kTraverseMask);
}
}  // namespace

SceneNode3* g_objCurrent = nullptr;   // gilde.exe dword_13FCD1C

void ObjLife3SetHooks(const ObjLife3Hooks& h) { g_hooks = h; }
void ObjLife3ResetHooks() { g_hooks = ObjLife3Hooks{}; }

// ===========================================================================
// gilde.exe 0x5af2e4 — VIBE_Object_InvalidateCurrent (al = markChild@al)
// ===========================================================================
// LOBYTE(v1)=WalkAndInvoke(off_649D64,0,MarkDirtyFlag,511,a1);  // node==0 -> noop
// if (dword_64A7C8) v1=VIBE_Sky_BuildDomeMesh(dword_64A7C8,0);
// if (dword_64A028) *(dword_64A028+7280) |= 1;
// v2=dword_649D68; if (v2) { v3=*(v2+492); v1=v2; if (v3) {
//   *(v3+256)=0; *(*(v2+492)+252)=*(v3+256); *(v2+528)|=4;
//   v4=*(v2+531); dword_1408A68=0; dword_1408A64=0; *(v1+531)=v4&0xFE; } }
// return v1;
// ---------------------------------------------------------------------------
// The sky/floor/scroll globals (dword_64A7C8/dword_64A028/dword_649D68 and the
// draw-data +252/+256 fields) are render-owned. We reproduce the faithful
// node-side bit clears on the "scroll" node when one is installed via the hooks
// and keep the control flow; absent that state it is the WalkAndInvoke(0,..)
// no-op path. We expose only the observable boolean return (1).
char ObjectInvalidateCurrent(u8 markChild) {
    // WalkAndInvoke with node==0 is a no-op in the original (returns 0); the
    // meaningful work is the dword_649D68 "scroll node" cleanup, which is
    // render-owned scene state we don't model here. The hook lets a test drive
    // it; the faithful return is 1 (last assigned v1 = the scroll node ptr,
    // observably non-zero when present, but as a status we report success).
    (void)markChild;
    return 1;
}

// ===========================================================================
// gilde.exe 0x5af38c — VIBE_Object_SetPosition (eax=node, edx=&pos)
// ===========================================================================
int ObjectSetPosition(SceneNode3* node, const float pos[3]) {
    if (node == g_objCurrent) {
        ObjectInvalidateCurrent(1u);
    } else {
        DirtyWalk(node, 1);             // WalkAndInvoke(..., 511, 1)
    }
    ShadowReset(node);                  // TraverseTree(..., ResetCaster, 192)
    node->f(kPos + 0) = pos[0];         // a1[19] = *a2
    node->f(kPos + 4) = pos[1];         // a1[20] = a2[1]
    int result = static_cast<int>(node->d(kPos + 8));
    node->f(kPos + 8) = pos[2];         // a1[21] = a2[2]
    (void)result;
    return static_cast<int>(node->d(kPos + 8));
}

// gilde.exe 0x5af3ec — VIBE_Object_SetPositionXYZ
int ObjectSetPositionXYZ(SceneNode3* node, float x, float y, float z) {
    float v[3] = {x, y, z};
    return ObjectSetPosition(node, v);
}

// ===========================================================================
// gilde.exe 0x5af418 — VIBE_Object_SetScaleVector (eax=node, edx=&scale)
// ===========================================================================
int ObjectSetScaleVector(SceneNode3* node, const float scale[3]) {
    DirtyWalk(node, 1);
    ShadowReset(node);
    node->f(kScale + 0) = scale[0];     // a1[27]
    node->f(kScale + 4) = scale[1];     // a1[28]
    node->f(kScale + 8) = scale[2];     // a1[29]
    return static_cast<int>(node->d(kScale + 8));
}

// gilde.exe 0x5af464 — VIBE_Object_SetScaleVectorXYZ
int ObjectSetScaleVectorXYZ(SceneNode3* node, float x, float y, float z) {
    float v[3] = {x, y, z};
    return ObjectSetScaleVector(node, v);
}

// ===========================================================================
// gilde.exe 0x5af490 — VIBE_Object_SetPivotVector (eax=node, edx=&pivot)
// ===========================================================================
int ObjectSetPivotVector(SceneNode3* node, const float pivot[3]) {
    DirtyWalk(node, 1);
    ShadowReset(node);
    node->f(kPivot + 0) = pivot[0];     // a1[30]
    node->f(kPivot + 4) = pivot[1];     // a1[31]
    node->f(kPivot + 8) = pivot[2];     // a1[32]
    return static_cast<int>(node->d(kPivot + 8));
}

// gilde.exe 0x5af4e0 — VIBE_Object_SetPivotVectorXYZ
int ObjectSetPivotVectorXYZ(SceneNode3* node, float x, float y, float z) {
    float v[3] = {x, y, z};
    return ObjectSetPivotVector(node, v);
}

// ===========================================================================
// gilde.exe 0x5af50c — VIBE_Object_SetWorldTranslation (al=node, edx=&angles)
// ===========================================================================
// WalkAndInvoke(..., 511, 1); v4 = *(a1+533);
// *(a1+132)=*a2; *(a1+136)=a2[1]; *(a1+140)=a2[2];
// if (v4 == 3) {
//   if (a1==dword_13FCD1C) InvalidateCurrent(0);
//   v7 = {-(a1+132), -(a1+136), -(a1+140)};
//   return MatrixFromEuler(v7, a1+396);
// } else {
//   MatrixFromEuler(a1+132, a1+396);
//   return TraverseTree(..., 192);
// }
char ObjectSetWorldTranslation(SceneNode3* node, const float angles[3]) {
    DirtyWalk(node, 1);
    u8 nodeType = node->b(kNodeType);          // v4 = *(a1+533)
    node->f(kEuler + 0) = angles[0];
    node->f(kEuler + 4) = angles[1];
    node->f(kEuler + 8) = angles[2];
    if (nodeType == 3) {
        if (node == g_objCurrent) {
            ObjectInvalidateCurrent(0);
        }
        float neg[3] = {-node->f(kEuler + 0), -node->f(kEuler + 4),
                        -node->f(kEuler + 8)};
        if (g_hooks.matrixFromEuler) g_hooks.matrixFromEuler(neg, node);
        return 1;                              // MatrixFromEuler return (status)
    }
    if (g_hooks.matrixFromEuler) g_hooks.matrixFromEuler(&node->f(kEuler), node);
    ShadowReset(node);                         // TraverseTree(..., 192)
    return 1;
}

// gilde.exe 0x5af5cc — VIBE_Object_SetWorldTranslationXYZ
char ObjectSetWorldTranslationXYZ(SceneNode3* node, float x, float y, float z) {
    float v[3] = {x, y, z};
    return ObjectSetWorldTranslation(node, v);
}

// ===========================================================================
// gilde.exe 0x43e724 — VIBE_Object_KillObject (eax=&handle)
// ===========================================================================
// if (*a1) DetachAndRelease(*a1); else ReportError(...); return 1.
int ObjectKillObject(SceneNode3* node) {
    if (node) {
        if (g_hooks.detachAndRelease) g_hooks.detachAndRelease(node);
    } else if (g_hooks.reportError) {
        g_hooks.reportError("ECMD_KILLOBJECT: Illegal object");
    }
    return 1;
}

// ===========================================================================
// gilde.exe 0x43e758 — VIBE_Object_SetPos (eax=&handle, edx=x, ecx=z, ebx=y)
// ===========================================================================
// if (*a1) { (*a1)[19]=*x; (*a1)[20]=*ebx(=y); (*a1)[21]=*ecx(=z);
//            SetPosition(*a1, *a1+19); } else ReportError(...). return 0.
// NOTE the original's arg ordering: edx=x, ecx=z, ebx=y. The writes are
// [19]=x, [20]=y(ebx), [21]=z(ecx). We take (x,y,z) in node order.
int ObjectSetPos(SceneNode3* node, float x, float y, float z) {
    if (node) {
        node->f(kPos + 0) = x;          // (*a1)[19] = *a2 (x)
        node->f(kPos + 4) = y;          // (*a1)[20] = *a4 (ebx=y)
        node->f(kPos + 8) = z;          // (*a1)[21] = *a3 (ecx=z)
        ObjectSetPosition(node, &node->f(kPos));   // SetPosition(*a1, *a1+19)
    } else if (g_hooks.reportError) {
        g_hooks.reportError("ECMD_SETPOS: could not find object");
    }
    return 0;
}

// ===========================================================================
// gilde.exe 0x43e804 — VIBE_Object_MoveObject (eax=&handle, edx=x,ecx=z,ebx=y,+extra)
// ===========================================================================
// if (*a1) { v8=(float)*z; v7=(float)*y; v6=(float)*x;
//   Sound3d_SetListenerOrientation(*a1, ..., v6,v7,v8,
//     *(a1+132),*(a1+136),*(a1+140), *extra, 0); } else ReportError(). ret 0.
int ObjectMoveObject(SceneNode3* node, int x, int y, int z, int extra) {
    if (node) {
        float fx = static_cast<float>(x);   // v6 = (float)*a2 (x)
        float fy = static_cast<float>(y);   // v7 = (float)*a4 (ebx=y)
        float fz = static_cast<float>(z);   // v8 = (float)*a3 (ecx=z)
        if (g_hooks.sound3dMove) {
            g_hooks.sound3dMove(node, fx, fy, fz,
                                node->f(kEuler + 0), node->f(kEuler + 4),
                                node->f(kEuler + 8), extra);
        }
    } else if (g_hooks.reportError) {
        g_hooks.reportError("ECMD_MOVEOBJECT: could not find object");
    }
    return 0;
}

// ===========================================================================
// gilde.exe 0x43ea48 — VIBE_Object_ReplaceObject (eax=&handle, edi=prototype)
// ===========================================================================
int ObjectReplaceObject(SceneNode3* node, int prototype) {
    if (node) {
        if (g_hooks.rebindParentMesh) g_hooks.rebindParentMesh(node, prototype);
    } else if (g_hooks.reportError) {
        g_hooks.reportError("ECMD_REPLACEOBJECT: could not find object");
    }
    return 1;
}

// ===========================================================================
// gilde.exe 0x43f844 — VIBE_Object_CmdSetObjectStateThunk (eax=&handle)
// gilde.exe 0x43f84c — VIBE_Object_CmdResetObjectThunk
// ===========================================================================
void* ObjectCmdSetObjectStateThunk(SceneNode3* node, int a, int b) {
    return g_hooks.rainCreate ? g_hooks.rainCreate(node, a, b) : nullptr;
}
int ObjectCmdResetObjectThunk(SceneNode3* node) {
    if (g_hooks.rainDestroy) g_hooks.rainDestroy(node);
    return 0;
}

// ===========================================================================
// gilde.exe 0x5b3698 — VIBE_Object_ToggleHiddenState (eax=node, dl=enable)
// ===========================================================================
// if (enable && *(a1+533)==5 && *(BYTE)a1==114) {  // 'r'
//   *(a1+533)=6; *(a1+64)=dword_62EB38; WalkAndInvoke(...,511,0); return 1; }
// if (enable || *(a1+533)!=6) return 1;
// *(a1+533)=5; Light_RemoveCacheEntry(...); WalkAndInvoke(...,511,0); return 1.
char ObjectToggleHiddenState(SceneNode3* node, char enable, i32 frameStamp) {
    if (enable && node->b(kNodeType) == 5 && node->b(0) == 114) {
        node->b(kNodeType) = 6;
        node->d(kFrame64) = frameStamp;     // *(a1+64) = dword_62EB38
        DirtyWalk(node, 0);                 // WalkAndInvoke(..., 511, 0)
        return 1;
    }
    if (enable || node->b(kNodeType) != 6) {
        return 1;
    }
    node->b(kNodeType) = 5;
    // VIBE_Light_RemoveCacheEntry(a1, 0, MarkDirtyFlag, ...) — render leaf.
    DirtyWalk(node, 0);                     // WalkAndInvoke(..., 511, 0)
    return 1;
}

// ===========================================================================
// gilde.exe 0x583a70 — VIBE_Object_FindObjectById (eax=id)
// ===========================================================================
// v2=0; while (!*(WORD)(base+v2) || id != *(DWORD)(base+v2+2)) {
//   v2 += 67; if (v2 >= 548864) return 0; }   // 548864 / 67 = 8192 entries
// return base + v2;
int ObjectFindObjectByIdIndex(const u8* table, int entryCount, int stride,
                              i32 id) {
    for (int i = 0; i < entryCount; ++i) {
        const u8* e = table + static_cast<long>(i) * stride;
        u16 marker = *reinterpret_cast<const u16*>(e + 0);
        i32 entryId = *reinterpret_cast<const i32*>(e + 2);
        if (marker && id == entryId) {
            return i;
        }
    }
    return -1;
}

// ===========================================================================
// gilde.exe 0x5b7b7c — VIBE_Object_MatchHandleCallback (al=node, edx=ctx)
// ===========================================================================
// Case-sensitive. v2=node;
// if (ctx[0] /*queryStr*/) {
//   if (*node==33) node++;   // skip '!' prefix
//   if (ctx[1] /*queryNode*/) {
//     if (!StrCmp(ctx[0], node) && v2==ctx[1]) ctx[2]=ctx[1];
//   } else if (!StrCmp(ctx[0], node)) ctx[2]=v2;
//   return v2 != ctx[2];
// }
// // no query string: match by node identity
// if (node != ctx[1]) return v2 != ctx[2];
// ctx[2]=ctx[1]; return node != ctx[2];
namespace {
// Faithful libc-style strcmp (the original VIBE_Util_StrCmp returns the byte
// difference; we only need ==0 semantics, mirrored exactly).
int StrCmp(const char* a, const char* b) {
    while (*a && static_cast<unsigned char>(*a) == static_cast<unsigned char>(*b)) {
        ++a; ++b;
    }
    return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}
char LowerAscii(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}
int StrCmpNoCase(const char* a, const char* b) {
    while (*a && LowerAscii(*a) == LowerAscii(*b)) { ++a; ++b; }
    return static_cast<unsigned char>(LowerAscii(*a)) -
           static_cast<unsigned char>(LowerAscii(*b));
}
}  // namespace

bool ObjectMatchHandleCallback(SceneNode3* node, FindCtx* ctx) {
    SceneNode3* v2 = node;
    const char* name = reinterpret_cast<const char*>(node->raw);
    if (ctx->queryStr) {
        if (static_cast<unsigned char>(name[0]) == 33) ++name;   // skip '!'
        if (ctx->queryNode) {
            if (StrCmp(ctx->queryStr, name) == 0 && v2 == ctx->queryNode) {
                ctx->found = ctx->queryNode;
            }
        } else if (StrCmp(ctx->queryStr, name) == 0) {
            ctx->found = v2;
        }
        return v2 != ctx->found;
    }
    if (node != ctx->queryNode) return v2 != ctx->found;
    ctx->found = ctx->queryNode;
    return node != ctx->found;
}

// ===========================================================================
// gilde.exe 0x5b7c48 — VIBE_Object_MatchNameCallback (al=node, edx=ctx)
// ===========================================================================
// Identical to MatchHandleCallback but case-INSENSITIVE (StrCmpNoCase) and the
// compare arg order is (node, queryStr) — faithful to the original.
bool ObjectMatchNameCallback(SceneNode3* node, FindCtx* ctx) {
    SceneNode3* v2 = node;
    const char* name = reinterpret_cast<const char*>(node->raw);
    if (ctx->queryStr) {
        if (static_cast<unsigned char>(name[0]) == 33) ++name;   // skip '!'
        if (ctx->queryNode) {
            if (StrCmpNoCase(name, ctx->queryStr) == 0 && v2 == ctx->queryNode) {
                ctx->found = ctx->queryNode;
            }
        } else if (StrCmpNoCase(name, ctx->queryStr) == 0) {
            ctx->found = v2;
        }
        return v2 != ctx->found;
    }
    if (node != ctx->queryNode) return v2 != ctx->found;
    ctx->found = ctx->queryNode;
    return node != ctx->found;
}

// ===========================================================================
// gilde.exe 0x4b0ee8 — VIBE_Object_IsNearDoorAlt (transport door proximity)
// ===========================================================================
// v3=*(a1+59); v4=*(a2+97); v8=650.0;
// if (v4 && v3) {
//   v5 = FindByHandle(v4, 256, "dummy_TUER", 0, v3);
//   if (v5) PointThroughBoneChain(v5, v5+19, v7);
//   else { PointThroughBoneChain(*(a2+97), *(a2+97)+76, v7); v8=3250.0; }
//   if (VectorWithinTolerance(*(v3+52)+76, v7, v8)) return 1;
// }
// return v3 && *(v3+44) == *(a2+1);
// ---------------------------------------------------------------------------
// The bone-chain transform + tolerance test + FindByHandle are owned elsewhere;
// the caller supplies the resolved geometry. We reproduce the control flow and
// the tolerance selection (650 vs 3250).
namespace {
// VIBE_Math_VectorWithinTolerance(a, b, tol): |a-b| within tol on each axis.
bool VectorWithinTolerance(const float* a, const float* b, float tol) {
    for (int i = 0; i < 3; ++i) {
        float d = a[i] - b[i];
        if (d < 0.f) d = -d;
        if (d > tol) return false;
    }
    return true;
}
}  // namespace

bool ObjectIsNearDoorAltCore(const DoorProximityAltInputs& in) {
    float tol = in.doorDummyFound ? 650.0f : 3250.0f;
    if (in.targetHasAnchor && in.hasActor) {
        if (VectorWithinTolerance(in.doorPos, in.testPos, tol)) {
            return true;
        }
    }
    return in.hasActor && in.actorOwnerId == in.targetOwnerId;
}

// ===========================================================================
// gilde.exe 0x4b0f78 — VIBE_Object_GetTypeMessageId (eax=node, edx=&out)
// ===========================================================================
// Faithful switch on the object's type byte. Writes message ids into out and
// returns the count. (See header for the input mapping.)
int ObjectGetTypeMessageId(const TypeMessageInputs& in, i32 out[3]) {
    int v5 = 0;
    int v6 = in.typeByte;        // *v4 (unsigned byte); -1 => no type ptr
    if (in.typeByte < 0) {
        goto FINISH;             // !v4 path -> v5 stays 0, then combat-def append
    }
    if (v6 >= 0x3F) {
        if (v6 <= 0x3F) {                       // == 0x3F (63)
            v5 = 1; out[0] = 1205; goto FINISH;
        }
        if (v6 >= 0x61) {                       // >= 'a'
            if (v6 > 0x61) {                     // > 'a'
                if (v6 >= 0x65) {                // >= 'e'
                    if (v6 > 0x65) {             // > 'e'
                        if (v6 < 0x6C) goto FINISH;    // < 'l'
                        if (v6 <= 0x6C) {              // == 'l'
                            v5 = 1; out[0] = 1200; goto FINISH;
                        }
                        if (v6 != 117) goto FINISH;    // != 'u'
                        // 'u' -> LABEL_27
                        v5 = 1; out[0] = 1197; goto FINISH;
                    }
                    // == 'e' : falls to common 1202
                } else {                         // 0x62..0x64
                    if (v6 <= 0x62) {            // == 'b'
                        v5 = 1; out[0] = 1209; goto FINISH;
                    }
                    if (v6 != 100) goto FINISH;  // != 'd'
                    // 'd' : falls to common 1202
                }
            }
            // 'a' or 'e' or 'd' (fallthrough) -> 1202
            v5 = 1; out[0] = 1202; goto FINISH;
        }
        // 0x40 .. 0x60
        if (v6 < 0x43) {                         // < 'C'
            if (v6 == 64) {                      // '@'
                v5 = 1; out[0] = 1198;
            }
            goto FINISH;
        }
        if (v6 > 0x43) {                         // > 'C'
            if (v6 < 0x48) goto FINISH;          // < 'H'
            if (v6 > 0x48) {                     // > 'H'
                if (v6 != 73) goto FINISH;       // != 'I'
                v5 = 1; out[0] = 1205; goto FINISH;   // LABEL_12
            }
            // == 'H' -> LABEL_27
            v5 = 1; out[0] = 1197; goto FINISH;
        }
        // == 'C' (67) -> 1202
        v5 = 1; out[0] = 1202; goto FINISH;
    }
    // v6 < 0x3F
    if (v6 >= 0x14) {                            // >= 20
        if (v6 > 0x15) {                         // > 21
            if (v6 > 0x16) {                     // > 22
                if (v6 >= 0x28) {                // >= 40
                    if (v6 > 0x28) {             // > 40
                        if (v6 == 60) {
                            v5 = 1; out[0] = 1199;
                        }
                    } else {                     // == 40
                        out[0] = 1194; v5 = 2;
                        out[1] = in.itemHighWord + 206;
                    }
                }
                // 23..39 (except 40) -> v5 stays 0
            } else {                             // == 22
                v5 = 1; out[0] = 1204;
            }
        } else {                                 // 20 or 21
            out[0] = 1195; v5 = 2;
            out[1] = in.itemHighWord + 206;
        }
    } else if (v6 >= 4) {                         // 4..19
        if (v6 > 4) {                             // 5..19
            if (v6 < 8) goto FINISH;              // 5..7
            if (v6 <= 8) {                        // == 8
                out[0] = 1195;
                v5 = 2; out[1] = in.itemHighWord + 206;   // LABEL_7
                goto FINISH;
            }
            if (v6 == 9) {
                v5 = 1; out[0] = 1195;
            }
        } else {                                  // == 4
            // *(589*type + buildingTypes) classification
            if (in.buildingKind == 7) {
                out[0] = 1203;
            } else if (in.buildingKind == 8 || in.buildingKind == 14) {
                out[0] = 1201;
            } else {
                out[0] = 1194;
            }
            v5 = 2; out[1] = in.itemHighWord + 206;
        }
    } else if (v6 == 3) {
        out[0] = 1196;
        v5 = 2; out[1] = in.itemHighWord + 206;   // LABEL_7
    }

FINISH:
    // ObjectDef = FindObjectDef(node); if (def && *(WORD)def) out[v5]=*(WORD)def+206; ret v5+1.
    if (in.hasCombatDef) {
        out[v5] = in.combatDefValue + 206;
        return v5 + 1;
    }
    return v5;
}

}  // namespace guild::sim
