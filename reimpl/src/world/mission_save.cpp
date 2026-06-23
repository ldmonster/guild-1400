#include "world/mission_save.h"

#include <cstring>

#include "world/mission.h"   // g_missionSlots, g_missionSlotMode, kMissionSlotCount

// Faithful port of VIBE_Mission_SlotSetSingle (0x538638) and the save/load pair
// VIBE_Mission_SaveSlotTable (0x53b148) / VIBE_Mission_LoadSlotTable (0x53b25c).
// The originals stream six per-slot fields through VIBE_Vfs_WriteStream /
// VIBE_Vfs_ReadStreamBool; here the engine VFS is modelled by MissionStream so the
// byte layout (mode + 128 * {1,4,14,4,4,1}) is golden-vector testable.

namespace guild::world {

// gilde.exe 0x538638 — VIBE_Mission_SlotSetSingle  (eax=owner, dl=type).  VERIFIED-1:1
// Disasm-checked: byte_122FEC0[0]=dl(type); dword_122FEC4[0]=eax(owner);
// dword_122FEC8[0]=0 (deadline[0..3]); then VIBE_GameTime_Set(&deadline,0,0,0) at
// 0x5831f0 writes *(WORD)(p+4)=0, *(DWORD)(p+6)=0, *(DWORD)(p+10)=0 — covering
// deadline bytes [4,5],[6..9],[10..13]; combined with the +0 dword that zeroes all
// 14 deadline bytes, so memset(&deadline,0,14) is behavior-identical. byte_122FEE0[0]
// (state) = 0. Return eax is unused (sole caller 0x498d39 discards it) -> void.
void MissionSlotSetSingle(i32 owner, u8 type) {
    g_missionSlots[0].type  = type;            // byte_122FEC0[0]   = a2
    g_missionSlots[0].owner = owner;           // dword_122FEC4[0]  = a1
    // dword_122FEC8[0] = 0 then VIBE_GameTime_Set(dword_122FEC8, 0, 0, 0): the
    // whole 14-byte deadline/progress block ends up zeroed.
    std::memset(&g_missionSlots[0].deadline, 0, sizeof(g_missionSlots[0].deadline));
    g_missionSlots[0].state = 0;               // byte_122FEE0[0]   = 0
}

namespace {
// One field write/read; mirrors VIBE_Vfs_WriteStream(ptr,size,handle,1) returning
// !=-1 / VIBE_Vfs_ReadStreamBool(ptr,size,handle,1) returning nonzero on success.
bool StreamWrite(MissionStream& s, const void* src, std::size_t n) {
    if (!s.data || s.pos + n > s.size)
        return false;
    std::memcpy(s.data + s.pos, src, n);
    s.pos += n;
    return true;
}
bool StreamRead(MissionStream& s, void* dst, std::size_t n) {
    if (!s.data || s.pos + n > s.size)
        return false;
    std::memcpy(dst, s.data + s.pos, n);
    s.pos += n;
    return true;
}
} // namespace

// gilde.exe 0x53b148 — VIBE_Mission_SaveSlotTable.  VERIFIED-1:1
// Disasm-checked field order/widths/stride: mode WriteStream(byte_63C8F4,1) once;
// per slot edi=byte_122FEC0: +0 size 1 (loop cond), +4 size 4, +8 size 0Eh(14),
// +24 size 4, +28 size 4, +32 size 1; edi += 24h(36); loop while ebp<80h(128);
// returns eax=1 after slot 128, eax=0 on any WriteStream==-1. The IFileSystem byte
// stream here models VIBE_Vfs_WriteStream(ptr,size,handle,1) [rules 3-5 boundary].
bool MissionSaveSlotTable(MissionStream& s) {
    // VIBE_Vfs_WriteStream(&byte_63C8F4, 1, handle, 1)  (the single-slot mode byte).
    if (!StreamWrite(s, &g_missionSlotMode, 1))
        return false;
    for (int i = 0; i < kMissionSlotCount; ++i) {          // 128 slots, byte_122FEC0
        const MissionSlot& slot = g_missionSlots[i];
        if (!StreamWrite(s, &slot.type, 1))      return false;  // v2+0  (1)
        if (!StreamWrite(s, &slot.owner, 4))     return false;  // v2+4  (4)
        if (!StreamWrite(s, &slot.deadline, 14)) return false;  // v2+8  (0xE)
        if (!StreamWrite(s, &slot.fieldAt24, 4)) return false;  // v2+24 (4)
        if (!StreamWrite(s, &slot.fieldAt28, 4)) return false;  // v2+28 (4)
        if (!StreamWrite(s, &slot.state, 1))     return false;  // v2+32 (1)
    }
    return true;                                            // v3 >= 128 -> return 1
}

// gilde.exe 0x53b25c — VIBE_Mission_LoadSlotTable (byte-exact read counterpart).  VERIFIED-1:1
// Disasm-checked: mirrors save exactly via VIBE_Vfs_ReadStreamBool (nonzero=ok):
// mode(1) then per slot +0:1,+4:4,+8:14,+24:4,+28:4,+32:1; stride 36; 128 slots;
// returns 1 after slot 128, breaks (returns last !ok result, i.e. 0) on short read.
bool MissionLoadSlotTable(MissionStream& s) {
    if (!StreamRead(s, &g_missionSlotMode, 1))
        return false;
    for (int i = 0; i < kMissionSlotCount; ++i) {
        MissionSlot& slot = g_missionSlots[i];
        if (!StreamRead(s, &slot.type, 1))      return false;
        if (!StreamRead(s, &slot.owner, 4))     return false;
        if (!StreamRead(s, &slot.deadline, 14)) return false;
        if (!StreamRead(s, &slot.fieldAt24, 4)) return false;
        if (!StreamRead(s, &slot.fieldAt28, 4)) return false;
        if (!StreamRead(s, &slot.state, 1))     return false;
    }
    return true;
}

} // namespace guild::world
