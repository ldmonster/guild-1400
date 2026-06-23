// camera_update_recon.cpp — 1:1 translation of VIBE_Camera_Update @0x4b4c68
// (the per-frame camera dispatcher of gilde.exe) and its cursor-write leaf
// VIBE_Coord_ConvertY @0x40da48. See camera_update_recon.h for the recovered
// calling convention, the dispatch flow, and the callee-routing table.
#include "camera_update_recon.h"

#include <cstring>

namespace guild::render {

// ===========================================================================
// Default inert hooks.
// ===========================================================================
static i32 inert_updatePan(void*) { return 0; }
static void inert_setCursorPos(i32, i32) {}

CameraUpdateHooks CameraUpdate_DefaultHooks() {
    CameraUpdateHooks uh;
    uh.updatePan     = &inert_updatePan;
    uh.updatePanUser = nullptr;
    uh.setCursorPos  = &inert_setCursorPos;
    return uh;
}

// ===========================================================================
// gilde.exe 0x40da48 — VIBE_Coord_ConvertY (cursor coordinate write)
//
//   LOWORD(dword_672174) = ax;  HIWORD(dword_672174) = dx;   /*0x40da4f/55*/
//   LOWORD(dword_6721C4) = ax;  HIWORD(dword_6721C4) = dx;   /*0x40da5c/62*/
//   LOWORD(dword_672210) = ax;  HIWORD(dword_672210) = dx;   /*0x40da69/6f*/
//   if ( !dword_62D0D4 )                                     /*0x40da78*/
//       return SetCursorPos(result, a2);                     /*0x40da7e*/
//   return result;                                           /*0x40da7b*/
//
// Our model carries the packed dwords pre-shifted: d672174 == dword_672174>>16,
// d672210 == dword_672210>>16, d67220E == (int)unk_67220E>>16 (the overlapping
// dword whose high word is LOWORD(dword_672210)). The packed write therefore
// lands as: d672174 = d672210 = (i16)y and d67220E = (i16)x. dword_6721C4 has
// no consumer in the reconstructed tree (not modeled).
// ===========================================================================
i32 Camera_CursorCoordWrite(i32 x, i32 y, const Camera2State& st2,
                            Camera2Input& in2, CameraInput& in1,
                            const CameraUpdateHooks& uh) {
    const i32 xi = (i32)(i16)(x & 0xFFFF);   // ax
    const i32 yi = (i32)(i16)(y & 0xFFFF);   // dx (the original takes a2@<dx>)
    in2.d672174 = yi;        // HIWORD(dword_672174) = dx
    in2.d672210 = yi;        // HIWORD(dword_672210) = dx
    in2.d67220E = xi;        // (int)unk_67220E>>16 == LOWORD(dword_672210) = ax
    in1.d672174 = yi;        // CameraInput mirrors of the same globals
    in1.d672210 = yi;
    in1.d67220E = xi;
    if (!st2.boxFlag) {      // if ( !dword_62D0D4 )
        if (uh.setCursorPos)
            uh.setCursorPos(x, yi);          // SetCursorPos(result, a2)
        return x;
    }
    return x;                // return result (eax unchanged)
}

// Family-record write of the dispatcher (0x4b4d84..0x4b4dd6): raw dword copies
// into the record returned by VIBE_Person_GetFamilyRecord:
//   rec[33..35] = obj+76..84 (pos bits), rec[37..39] = obj+132..140 (world
//   bits), rec[32] = dword_6316E0 (zoomT bits).
static void family_write_dispatcher(void* rec, const CameraObject& obj,
                                    i32 zoomTBits) {
    if (!rec) return;
    i32* dr = reinterpret_cast<i32*>(rec);
    std::memcpy(&dr[33], &obj.posX,   sizeof(i32));  // *(rec+0x84) = *(obj+0x4C)
    std::memcpy(&dr[34], &obj.posY,   sizeof(i32));  // *(rec+0x88) = *(obj+0x50)
    std::memcpy(&dr[35], &obj.posZ,   sizeof(i32));  // *(rec+0x8C) = *(obj+0x54)
    std::memcpy(&dr[37], &obj.worldX, sizeof(i32));  // *(rec+0x94) = *(obj+0x84)
    std::memcpy(&dr[38], &obj.worldY, sizeof(i32));  // *(rec+0x98) = *(obj+0x88)
    std::memcpy(&dr[39], &obj.worldZ, sizeof(i32));  // *(rec+0x9C) = *(obj+0x8C)
    dr[32] = zoomTBits;                              // *(rec+0x80) = dword_6316E0
}

// ===========================================================================
// gilde.exe 0x4b4c68 — VIBE_Camera_Update (per-frame camera dispatcher)
// ===========================================================================
i32 Camera_Update(CameraObject& obj, CameraState& cs, Camera2State& st2,
                  Camera2Input& in2, CameraInput& in1, CameraUpdateState& us,
                  const CameraUpdateInput& uin, const CameraHooks& h1,
                  const Camera2Hooks& h2, const CameraUpdateHooks& uh,
                  i32 thisXLeftover) {
    // 0x4b4c6b/0x4b4c74: if ( !dword_13FCD1C || dword_62EB4C ) -> early path.
    if (!obj.present || uin.d62EB4C) {
        us.panResult = 0;                            // 0x4b4c85: dword_631628 = 0
        if (!st2.boxFlag) {                          // 0x4b4c8d: !dword_62D0D4
            // 0x4b4c9a: dword_62D0C4 = (dword_69FFBC >> 16) - 8   (right edge)
            st2.box0 = (uin.d69FFBC >> 16) - 8;
            // 0x4b4cac: dword_62D0C8 = *(int*)(0x69FFBA) >> 16    (bottom edge:
            // overlapping dword read at 69FFB8+2 == sign-extended LOWORD(69FFBC))
            st2.box1 = (i32)(i16)(uin.d69FFBC & 0xFFFF);
            st2.box2 = 0;                            // 0x4b4cb6: dword_62D0CC = 0
            st2.boxFlag = 1;                         // 0x4b4cbf: dword_62D0D4 = 1
            st2.box3 = 0;                            // 0x4b4cc7: dword_62D0D0 = 0
            // 0x4b4ccd: VIBE_Coord_ConvertY(eax = (int)unk_67220E>>16,
            //                               dx  = low16 of the same value).
            const i32 mx = in2.d67220E;
            Camera_CursorCoordWrite(mx, mx, st2, in2, in1, uh);
        }
        return 0;                                    // 0x4b4cd2: xor eax,eax
    }

    // 0x4b4cd8/0x4b4ce5: gated main path. When gated, dword_631628 is returned
    // UNCHANGED (no zeroing on this path — verified in the disasm: the jnz's
    // jump straight to the `mov eax, dword_631628; retn` tail at 0x4b4e1b).
    if (!uin.d11BC24C && uin.d633908 == -1) {
        // 0x4b4cf9: if ( !dword_672238 ) dword_631628 = VIBE_Camera_UpdatePan()
        if (!in2.d672238)
            us.panResult = uh.updatePan ? uh.updatePan(uh.updatePanUser) : 0;

        // 0x4b4d0c: if ( !(dword_11BC2D0 & 0x8000) ) VIBE_Camera_UpdateMovement()
        // (h1 forwarded so the wheel branch's AnchorToTerrain @0x4b46b1 sees the
        // bound terrainHeight, exactly as the original call chain does.)
        if ((uin.d11BC2D0 & 0x8000u) == 0)
            Camera_UpdateMovement(obj, cs, st2, in2, h2, &h1);

        // 0x4b4d23: if ( byte_6316D8 && !byte_671D6F )
        //               VIBE_Camera_ClampToTerrainHeight(ecx)   (leftover ecx)
        if (uin.byte6316D8 && !in2.byte671D6F)
            Camera_ClampToTerrainHeight(obj, cs, in1, h1, thisXLeftover);

        // 0x4b4d31..0x4b4d5c: family-record save gate. NOTE this gate requires
        // BOTH dword_631744 and dword_631748 to be zero — stricter than the
        // RotateView/UpdateMovement history gate (which allows 748 != 0).
        if (us.panResult) {
            if (in2.g_631610 == in2.g_631618 && !in2.g_631744 && !in2.g_631748) {
                // 0x4b4d7b: VIBE_Person_GetFamilyRecord(
                //               &word_12CE910[268 * (u16)word_63CC5C])
                // (row = active player; the hook owns the row resolution.)
                void* rec = h2.personGetFamilyRecord ? h2.personGetFamilyRecord()
                                                     : nullptr;
                family_write_dispatcher(rec, obj, cs.zoomTBits);
            }
        }

        // 0x4b4dd7..0x4b4e15: unconditional pos/world history mirrors.
        std::memcpy(&st2.hist_2E4, &obj.worldX, sizeof(i32)); // dword_11BC2E4
        std::memcpy(&st2.hist_2E8, &obj.worldY, sizeof(i32)); // dword_11BC2E8
        std::memcpy(&st2.hist_2EC, &obj.worldZ, sizeof(i32)); // dword_11BC2EC
        std::memcpy(&st2.hist_2D4, &obj.posX,   sizeof(i32)); // dword_11BC2D4
        std::memcpy(&st2.hist_2D8, &obj.posY,   sizeof(i32)); // dword_11BC2D8
        std::memcpy(&st2.hist_2DC, &obj.posZ,   sizeof(i32)); // dword_11BC2DC
    }

    return us.panResult;                             // 0x4b4e1b: dword_631628
}

} // namespace guild::render
