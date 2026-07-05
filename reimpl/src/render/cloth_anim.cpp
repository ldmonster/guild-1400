// =============================================================================
// guild::render — FLAG / BANNER / PENNANT ("Wimpel") animation driver impl.
// See cloth_anim.h for the full provenance + the cloth-wave finding.
//
//   0x4b5d98  VIBE_Character_AttachFlag
//   0x4b5e9c  VIBE_Character_ShowFlag
//   0x4b5ef8  VIBE_Character_RefreshFlagAnimation
//   0x4b62c0  VIBE_Character_CollectFlagNodes
//
// The 180° flag yaw uses render/scene_transform MatrixFromEuler 1:1 (the engine
// builds the same 3x3 from {0,π,0} at 0x4b5e0c and feeds it to ApplyParentTransform).
// =============================================================================
#include "cloth_anim.h"
#include "scene_transform.h"  // MatrixFromEuler @0x5cb1bc (Mat3)

#include <cctype>
#include <cstring>

namespace guild::render {

// --- inert / default leaves --------------------------------------------------
namespace {

// 0x5cb8f0 default — case-insensitive strcmp (0 == equal), matching the original.
int DefaultStrCmpNoCase(void* /*ctx*/, const char* a, const char* b) {
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    for (;; ++a, ++b) {
        int ca = std::tolower(static_cast<unsigned char>(*a));
        int cb = std::tolower(static_cast<unsigned char>(*b));
        if (ca != cb) return ca - cb;
        if (ca == 0) return 0;
    }
}

int StrCmpNoCase(FlagAnimHooks& H, const char* a, const char* b) {
    return H.strCmpNoCase ? H.strCmpNoCase(H.ctx, a, b)
                          : DefaultStrCmpNoCase(H.ctx, a, b);
}

} // namespace

// =============================================================================
// 0x4b5d98 — VIBE_Character_AttachFlag
// =============================================================================
bool AttachFlag(FlagAnimHooks& H, const char* nodeName, void* node,
                const FlagPerson& person, void* player, FlagObject& out) {
    // 0x4b5da6 — only the "dummy_FAHNE" placeholder node carries a flag.
    // StrCmpNoCase != 0 (mismatch) -> return 1 (keep walking), no-op.
    if (StrCmpNoCase(H, nodeName, kDummyFlagName) != 0)
        return true;  // 0x4b5daf

    // 0x4b5dd4 — pos = PointThroughBoneChain(node)  (world attach point).
    float pos[3] = {0.f, 0.f, 0.f};
    if (H.pointThroughBoneChain)
        H.pointThroughBoneChain(H.ctx, node, pos);

    // 0x4b5dee — obj = AttachToUniverseNode(person.universeNode, pos).
    void* obj = nullptr;
    if (H.attachToUniverseNode)
        obj = H.attachToUniverseNode(H.ctx, person.universeNode, pos);
    out = FlagObject{};
    out.handle = obj;

    // 0x4b5df2..0x4b5e0c — euler = {0, π, 0}; R = MatrixFromEuler(euler) (180° yaw).
    const float euler[3] = {0.f, kFlagYawPi, 0.f};
    Mat3 R = MatrixFromEuler(euler);
    // 0x4b5e17 — ApplyParentTransform(obj@eax, pos@edx = the bone-chain point,
    // R@ebx). The position rides along (harden fix: it was dropped before).
    if (H.applyParentTransform)
        H.applyParentTransform(H.ctx, obj, pos, R.m);

    // 0x4b5e1c..0x4b5e58 — heraldry texture set, only when heraldry != 0xFFFF.
    if (person.heraldry != kNoHeraldry) {
        int texIndex = FlagTextureSetIndex(person.heraldryByte);  // -62 bias
        if (H.selectTextureSet)
            H.selectTextureSet(H.ctx, obj, texIndex, player);
        out.texSetIndexApplied = texIndex;
        out.texSetState = 4;  // 0x4b5e58 — *(obj+535) = 4
    }

    // 0x4b5e6b — load + start the skeletal flag wave animation (the "wave").
    if (H.loadObjectAnimation)
        H.loadObjectAnimation(H.ctx, obj, kFlagAnimFile, kFlagAnimLoadMode);
    out.animLoaded = true;

    // 0x4b5e76..0x4b5e8a — render-flag bookkeeping on the flag object.
    out.renderFlags530 = static_cast<u8>((out.renderFlags530 & 0xB3) | 0x44);
    out.renderFlags529 = static_cast<u8>(out.renderFlags529 & ~0x02);

    return true;  // 0x4b5db1 — the engine always returns 1.
}

// =============================================================================
// 0x4b5e9c — VIBE_Character_ShowFlag
// =============================================================================
bool ShowFlag(FlagAnimHooks& H, const char* nodeName, FlagObject& obj,
              const FlagPerson& person, void* player) {
    // 0x4b5eb5 — skip non-"sp_WIMPEL" nodes or persons with no heraldry.
    if (StrCmpNoCase(H, nodeName, kFlagNodeName) != 0 ||
        person.heraldry == kNoHeraldry) {
        return true;  // 0x4b5eb7
    }
    // 0x4b5eed — re-apply the heraldry texture set (no re-attach / re-animate).
    int texIndex = FlagTextureSetIndex(person.heraldryByte);  // -62 bias
    if (H.selectTextureSet)
        H.selectTextureSet(H.ctx, obj.handle, texIndex, player);
    obj.texSetIndexApplied = texIndex;
    return true;  // 0x4b5eb9
}

// =============================================================================
// 0x4b5ef8 — VIBE_Character_RefreshFlagAnimation
// =============================================================================
bool RefreshFlagAnimation(FlagAnimHooks& H, const FlagPerson& person, u8 buildType,
                          bool /*nodeFlagBit528*/, void* player,
                          FlagSceneNode* nodes, int nodeCount,
                          FlagObject* produced) {
    // 0x4b5efe — person must have a universe node.
    if (!person.universeNode)
        return false;  // 0x4b5f87
    // 0x4b5f0d — heraldry must be valid.
    if (person.heraldry == kNoHeraldry)
        return false;
    // 0x4b5f3b — build/type byte must be 5, 6 or 7.
    if (!FlagBuildTypeEnabled(buildType))
        return false;

    // 0x4b5f42..0x4b5f99 — walk the universe node, invoking AttachFlag per node.
    // (When *(node+528)&1 is clear the engine transiently zeroes node+496 around
    //  the walk and restores it; that is a render-field suppression with no effect
    //  on the produced flag objects, so it is a no-op in this data-level model.)
    for (int i = 0; i < nodeCount; ++i) {
        FlagObject obj{};
        AttachFlag(H, nodes[i].name, nodes[i].node, person, player, obj);
        if (produced)
            produced[i] = obj;
    }
    return true;
}

// =============================================================================
// 0x4b62c0 — VIBE_Character_CollectFlagNodes
// =============================================================================
bool CollectFlagNodes(const char* nodeName, void* node,
                      void** out, int outCapacity, int& count) {
    // 0x4b62ce — node name contains "sp_WIMPEL"?
    if (NameContains(nodeName, kFlagNodeName)) {
        // 0x4b62e3 — v6 = (*acc)++; acc[v6 + 1] = node.  acc[0] is the count.
        int slot = count++;
        if (out && slot >= 0 && slot < outCapacity)
            out[slot] = node;
    }
    // 0x4b62dd — keep walking while count < 32.
    return count < kMaxFlagNodes;
}

} // namespace guild::render
