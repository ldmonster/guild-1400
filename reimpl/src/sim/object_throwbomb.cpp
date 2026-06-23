// gilde.exe 0x4869dc — VIBE_Object_SpawnThrownBomb. See object_throwbomb.h.
#include "sim/object_throwbomb.h"

#include <cstring>

namespace guild::sim {

namespace {
const ThrowBombHooks kInert{};
const ThrowBombHooks* g_hooks = &kInert;

// dword_48492C — the 3-dword color/param seed handed to AttachToUniverseNode. All
// zero in the binary (verified via get_bytes).
const i32 kBombColorParam[3] = {0, 0, 0};

// gilde.exe constants (get_bytes-verified):
constexpr f32 kSpawnYLift   = 60.0f;        // dbl_61B174 — spawn.y += 60
constexpr f32 kVelocityScale = 0.5f;        // dbl_61B17C — velocity = delta * 0.5
constexpr f32 kVyArcLift     = 50.0f;       // dbl_61B184 — vy += 50 (the throw arc)
constexpr f32 kDyAdjust      = -60.0f;      // dbl_61B18C — descriptor dy -= 60
constexpr f32 kBombHeight    = 18.0f;       // v28 — descriptor[0]/[22] = (int)18
// 1078530011 == 0x40490FDB == PI: stamped into the descriptor's rotation slots.
const i32 kPiBits = 1078530011;
inline i32 asBits(f32 v) { i32 b; std::memcpy(&b, &v, 4); return b; }
} // namespace

void SetThrowBombHooks(const ThrowBombHooks* h) { g_hooks = h ? h : &kInert; }
const ThrowBombHooks& GetThrowBombHooks() { return *g_hooks; }

BombSlot g_bombTable[kBombSlotCount];
void ResetBombTable() {
    for (auto& s : g_bombTable) s = BombSlot{};
}

// gilde.exe 0x4869dc — VIBE_Object_SpawnThrownBomb.
int SpawnThrownBomb(const BombSpawn& spawn, i32 targetTile, i32 arg,
                    i32* outDescriptor88) {
    const ThrowBombHooks& hk = GetThrowBombHooks();

    // --- find a free slot (0x486a1d..0x486a3f). slot0 first, else scan 1..31. ---
    int slot = 0;
    if (g_bombTable[0].node) {
        slot = -1;
        for (int k = 1; k < kBombSlotCount; ++k) {       // v4 = 4,8,..,124
            if (!g_bombTable[k].node) { slot = k; break; }
        }
        if (slot < 0) return -1;                          // v4 >= 128 -> return 0
    }

    // --- attach the bomb node at the lifted spawn position (0x486a41..0x486a91) ---
    f32 pos[3] = { spawn.x, spawn.y + kSpawnYLift, 0.0f };
    std::memcpy(&pos[2], &spawn.z, 4);                    // v24 = *(a1+8) (packed int z)
    const i32 node = hk.attachNode ? hk.attachNode(pos, kBombColorParam) : 0;
    g_bombTable[slot].node = node;                        // dword_B5F910[v4] = node
    if (hk.markNodeFlag) hk.markNodeFlag(node);           // *(node+529) |= 4

    // --- target world position + ballistic delta/velocity (0x486abd..0x486b4a) ---
    f32 tw[3] = {0, 0, 0};
    if (hk.tileToWorld) hk.tileToWorld(targetTile, arg, tw);
    f32 dx = tw[0] - spawn.x;                             // v19 -= *(a1)
    f32 dy = tw[1] - spawn.y;                             // v20 -= *(a1+4)
    f32 dz = tw[2] - *reinterpret_cast<const f32*>(&pos[2]); // v21 -= *(a1+8)
    f32 vx = dx * kVelocityScale;                         // v29
    f32 vy = dy * kVelocityScale + kVyArcLift;            // v27 = v20*0.5 + 50
    f32 vz = dz * kVelocityScale;                         // v30

    // --- build the 352-byte (88-dword) object anim/physics descriptor ----------
    // VIBE_Light_SetGrayColorThunk(0, 352, &desc) zero-fills it first.
    i32 desc[88];
    std::memset(desc, 0, sizeof(desc));

    desc[0]  = static_cast<i32>(kBombHeight);             // (int)18.0
    desc[22] = static_cast<i32>(kBombHeight);
    desc[23] = asBits(vx);                                // velocity x
    desc[24] = asBits(vy);                                // velocity y (arc-lifted)
    desc[25] = asBits(vz);                                // velocity z
    desc[27] = desc[28] = desc[29] = kPiBits;             // rotation slots = PI
    desc[44] = 1;
    desc[45] = asBits(dx);                                // target delta x
    dy += kDyAdjust;                                      // v20 += -60 (after delta calc)
    desc[46] = asBits(dy);                                // target delta y (adjusted)
    desc[47] = asBits(dz);                                // target delta z
    desc[49] = desc[50] = desc[51] = kPiBits;
    desc[67] = hk.coordConvertX ? hk.coordConvertX() : 0; // VIBE_Coord_ConvertX()
    desc[68] = asBits(dy);
    desc[69] = asBits(dz);
    desc[71] = desc[72] = desc[73] = kPiBits;

    if (outDescriptor88) std::memcpy(outDescriptor88, desc, sizeof(desc));

    // --- small anim-param struct (v26): zeroed, word[+1] |= 0x21C ---------------
    i32 animParams[6];
    std::memset(animParams, 0, sizeof(animParams));
    { // *(u16*)((char*)&v26 + 1) |= 0x21C
        auto* p = reinterpret_cast<unsigned char*>(animParams) + 1;
        u16 w; std::memcpy(&w, p, 2); w |= 0x021C; std::memcpy(p, &w, 2);
    }

    // --- start the object animation + record the slot (0x486ca4..0x486cc9) ------
    g_bombTable[slot].anim = hk.createObjectAnim
        ? hk.createObjectAnim(node, animParams, slot * 4)
        : 0;
    g_bombTable[slot].tile = targetTile;                  // dword_B5F918[v4] = a2
    g_bombTable[slot].arg  = arg;                          // dword_B5F91C[v4] = a3
    return slot;                                           // &dword_B5F910[v4]
}

} // namespace guild::sim
