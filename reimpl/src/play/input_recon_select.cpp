// ===========================================================================
// gilde.exe — Input poll orchestration + entity selection / selection-reset.
// 1:1 reconstruction; see input_recon_select.h for provenance and boundary
// notes.  Platform (DirectInput/Win32) layers are routed through hooks.
// ===========================================================================
#include "play/input_recon_select.h"

#include <cmath>
#include <cstring>

namespace guild::play {

// ---------------------------------------------------------------------------
// Module state (the reconstructed BSS globals + the installable hooks).
// ---------------------------------------------------------------------------
SelectionAnchors g_selectionAnchors;
i32 g_selectionOwnerA = 0;       // dword_631744
u8* g_selectionOwnerB = nullptr; // dword_631748

namespace {
InputPollHooks       g_pollHooks;
SelectEntityHooks    g_selHooks;
SelectionResetHooks  g_resetHooks;

// flt_610EBC = 0x40a00000 == 5.0f (Y bias added before projection).
constexpr float kSelectYBias = 5.0f;
} // namespace

void Input_SetPollHooks(const InputPollHooks& hooks) { g_pollHooks = hooks; }
const InputPollHooks& Input_GetPollHooks() { return g_pollHooks; }
void SelectEntity_SetHooks(const SelectEntityHooks& hooks) { g_selHooks = hooks; }
void Selection_SetResetHooks(const SelectionResetHooks& hooks) { g_resetHooks = hooks; }

// ===========================================================================
// 0x5c6b08 — VIBE_Coord_ConvertX.
// Verified from the disassembly (wave-15, live MCP):
//   fstcw  [cw]               ; save the current control word
//   mov    byte ptr [cw+1],1Fh; force the HIGH byte to 0x1F -> CW = 0x1F7F
//   fldcw  [cw] / frndint     ; round st(0) with RC = bits 11:10 = 0b11
//   fldcw  [saved]            ; restore
// RC = 0b11 is *round toward zero* (truncate), NOT round-to-nearest.  So
// ConvertX(x) == trunc(x); the callers' subsequent (int) cast is a no-op.
// (This corrects the earlier MCP-free assumption of nearbyint; see
//  world/money_format.cpp, whose trunc model was already correct.)
// ===========================================================================
double Coord_ConvertX(double v) {
    // frndint with RC = toward-zero == truncate toward zero.
    return std::trunc(v);
}

// ===========================================================================
// 0x40da88 — VIBE_Input_PollMouseAndKeyboard.
// ===========================================================================
u8 Input_PollMouseAndKeyboard() {
    if (g_pollHooks.pollMouseDevice)
        g_pollHooks.pollMouseDevice();           // VIBE_Input_PollMouseDevice()
    if (g_pollHooks.copyPacket)
        g_pollHooks.copyPacket(nullptr, nullptr, kInputCursorPacketBytes); // current->prev mirror
    if (g_pollHooks.pollKeyboardDevice)
        return g_pollHooks.pollKeyboardDevice(); // VIBE_Input_PollKeyboardDevice(0)
    return 0;
}

// ===========================================================================
// 0x4147cc — VIBE_SelectEntity_ComputeResult.
//
// Transcribed 1:1 from the Hex-Rays goto graph.  Variable names track the
// decompile (v4 = actor index, v6 = sub-slot 0/1, Ptr = animation record).
// ===========================================================================
i32 SelectEntity_ComputeResult(i32 screenX, i32 screenY, SelectEntityResult* out) {
    SelectEntityResult local;
    SelectEntityResult& R = out ? *out : local;

    auto recordOf = [&](int idx) -> u8* {
        return g_selHooks.actorRecord ? g_selHooks.actorRecord(idx) : nullptr;
    };

    int v4 = 0;     // actor index
    // dword_69FF98 = 0; dword_69FF9C = 0; byte_6769E0 = 0;
    R.pointX = 0;
    R.pointY = 0;
    R.name[0] = 0;
    R.pickedLabelId = 0;       // dword_62D22C starts zeroed; set to -1 only on the final miss
    R.pickedSecondary = -1;    // dword_62D290 (left untouched by the original; -1 sentinel here)

    u8* v5 = nullptr;
    int v6 = 0;     // sub-slot

    // The original is a goto graph (LABEL_6 / LABEL_27 / LABEL_29).  Translated
    // 1:1 with explicit gotos to preserve the exact branch structure.
    while (true) {                                   // top of the actor while(1)
        v5 = recordOf(v4);                           // &dword_676A60[171*v4]
        if (v5 && *reinterpret_cast<i32*>(v5 + 400)) {       // v5[100]
            if (*reinterpret_cast<i32*>(v5 + 408) &&         // v5[102]
                *reinterpret_cast<i32*>(v5 + 412)) {         // v5[103]
                break;                                       // -> the body below
            }
        }
    LABEL_27:
        if (++v4 >= 48)
            return -1;
    }

    // Locals declared up front so the goto back-edges below never cross an
    // initialisation (the decompile is a flat label graph; we mirror it 1:1).
    u8* v7;          // name buffer ptr
    u8* Ptr;         // animation record
    v6 = 0;

LABEL_6:
    if (*(v5 + 428)) {                                       // *((BYTE*)v5+428)
        v7 = v5 + 428;                                       // v5 + 107
    } else {
        // while ( v6 != 1 || !*((_BYTE*)v5+492) ) { LABEL_29: ... }  v7 = v5+123
LABEL_else_cond:                                             // the while-condition test
        if (v6 == 1 && *(v5 + 492))
            v7 = v5 + 492;                                   // v5 + 123 (loop exit)
        else
            goto LABEL_29;
    }

    // Ptr = VIBE_Animation_GetPtr(v7, v6);
    Ptr = g_selHooks.animationGetPtr ? g_selHooks.animationGetPtr(v7, v6) : nullptr;
    if (!Ptr)
        goto LABEL_29;

    {
        // v33 = a1; v27 = (double)a2 + 5.0; v26 = (float)a1;
        float v26 = static_cast<float>(screenX);
        float v27 = static_cast<float>(static_cast<double>(screenY) + kSelectYBias);
        float v29 = 0.0f, v28 = 0.0f;   // volume corners (BYREF)
        int v13 = 0;                    // ConvertX secondary flag (ecx; ==1 -> +256)

        int ok = g_selHooks.computeSelectionVolume
                     ? g_selHooks.computeSelectionVolume(v26, v27, 0, Ptr, &v29, &v28, &v13)
                     : 0;
        if (!ok)
            goto LABEL_29;

        // v9 = (double)*(unsigned int*)(Ptr+116);
        // The products v9*v29 / v9*v28 stay ON THE X87 STACK (st6/st7) until
        // ConvertX's frndint truncates them in-register — a 32-bit-int by
        // 24-bit-float product needs up to 56 mantissa bits, exact in the
        // 80-bit register but ROUNDED by a double.  long double models this.
        long double v9 = static_cast<long double>(*reinterpret_cast<u32*>(Ptr + 116));
        long double v10 = v9 * static_cast<long double>(v29); // v10 = v9*v29 (st6)
        v10 = std::trunc(v10);                               // ConvertX() frndint RC=11
        int v31 = static_cast<int>(v10);                     // v31 = (int)v10
        long double v11 = v9 * static_cast<long double>(v28); // v11 = v9*v28 (st7)
        v11 = std::trunc(v11);                               // ConvertX() frndint RC=11
        int v32 = static_cast<int>(v11);                     // v32 = (int)v11
        if (v13 == 1)                                        // if (v13==1) v32 += 256
            v32 += 256;

        R.pointX = v31;                                      // dword_69FF98
        R.pointY = v32;                                      // dword_69FF9C

        // Copy the animation name (Ptr[0..], byte-pairs) into byte_6769E0.
        {
            char* v14 = R.name;
            const u8* p = Ptr;
            int guard = 0;
            const int cap = static_cast<int>(sizeof(R.name)) - 2;
            while (guard <= cap) {
                char v15 = static_cast<char>(*p);
                *v14 = v15;
                if (!v15) break;
                char v16 = static_cast<char>(p[1]);
                p += 2;
                v14[1] = v16;
                v14 += 2;
                guard += 2;
                if (!v16) break;
            }
        }

        // v12 = actor record (v5).  Child-hotspot-list ids: count at +388, list
        // ids at +4*v17+4.  Walk each list's entries (count=*(rec+26)>>16, id
        // array at *(u8**)(rec+24)) and hit-test the entry's 740-byte rect record.
        int v17 = *reinterpret_cast<i32*>(v5 + 388);
        while (--v17 >= 0) {
            int listId = *reinterpret_cast<i32*>(v5 + 4 * v17 + 4);
            // v19 = &dword_67EB80[238*listId]; count = *(i32*)(v19+26)>>16;
            // idArray = v19[6].  Decoded at the hook boundary (see header).
            int count = 0;
            const i32* idArray = nullptr;
            bool haveList = g_selHooks.hotspotListRecord
                                ? g_selHooks.hotspotListRecord(listId, &count, &idArray)
                                : false;
            if (!haveList || !idArray)
                continue;
            int v18 = 0;
            while (v18 < count) {
                const i32* v30 = idArray + v18;       // &id[v18]
                int v20 = *v30;
                u8* v21 = g_selHooks.hotspotRect ? g_selHooks.hotspotRect(v20) : nullptr;
                if (v21) {
                    int v22 = *reinterpret_cast<i32*>(v21 + 14) >> 16;
                    if (v22 <= v31 &&
                        (*reinterpret_cast<i32*>(v21 + 18) >> 16) + v22 >= v31) {
                        int v23 = *reinterpret_cast<i32*>(v21 + 16) >> 16;
                        if (v23 <= v32 &&
                            (*reinterpret_cast<i32*>(v21 + 20) >> 16) + v23 >= v32) {
                            if (*(v21 + 24) != 64) {
                                R.pickedLabelId = *v30;        // dword_62D22C
                                i32* v24 = *reinterpret_cast<i32**>(v21 + 44);
                                if (v24) {
                                    R.pickedSecondary = *v24;  // dword_62D290
                                    R.secondaryWritten = true;
                                }
                                return *reinterpret_cast<i32*>(v21 + 8);
                            }
                            // type 64: store secondary, keep scanning.
                            R.pickedSecondary = *reinterpret_cast<i32*>(v21 + 116);
                            R.secondaryWritten = true;
                        }
                    }
                }
                ++v18;
            }
        }

        // dword_62D22C = -1; return -1;
        R.pickedLabelId = -1;
        return -1;
    }

LABEL_29:
    if (++v6 >= 2)
        goto LABEL_27;
    if (!v6)
        goto LABEL_6;
    // v6 == 1 (after ++): the original returns to the else-branch while-condition
    // test (NOT to LABEL_6 / the byte+428 dispatch) — it tries the v5+123 slot.
    goto LABEL_else_cond;
}

// ===========================================================================
// 0x4b9444 — VIBE_Selection_Reset.
// ===========================================================================
u8* Selection_Reset(int a1) {
    // result = 0;
    u8* result = nullptr;

    // dword_11BC274 = 0; dword_11BC278 = 0; dword_11BC260 = 0; dword_631740 = 0;
    g_selectionAnchors.g11BC274 = 0;
    g_selectionAnchors.g11BC278 = 0;
    g_selectionAnchors.g11BC260 = 0;
    g_selectionAnchors.g631740  = 0;

    // if ( !dword_631744 && !dword_631748 ) { result = QueryBegin(a1,1,0,68); goto L4; }
    if (g_selectionOwnerA == 0 && g_selectionOwnerB == nullptr) {
        result = g_resetHooks.personQueryBegin ? g_resetHooks.personQueryBegin(a1) : nullptr;
        // LABEL_4:
        if (!result)
            return result;
        // goto LABEL_8
    } else if (g_selectionOwnerB == nullptr) {
        // if ( !dword_631748 ) { LABEL_4: if(!result) return result; goto L8; }
        // result is still 0 here -> returns 0.
        if (!result)
            return result;
        // (unreachable continuation kept for fidelity)
    } else {
        // result = (__int16*)dword_631748;
        result = g_selectionOwnerB;
    }

    // LABEL_8:
    // v2 = *(_DWORD *)((char*)result + 93);
    int v2 = *reinterpret_cast<i32*>(result + 93);
    // byte_6317B4 = 0;
    g_selectionAnchors.g6317B4 = 0;

    // for ( result = QueryFind(v2,1,5); result; result = IterNext() )
    for (result = g_resetHooks.gameObjectQueryFind ? g_resetHooks.gameObjectQueryFind(v2)
                                                    : nullptr;
         result;
         result = g_resetHooks.gameObjectIterNext ? g_resetHooks.gameObjectIterNext()
                                                   : nullptr) {
        // if ( *(_BYTE*)(dword_13CE27C + 65 * *result) == 29 )
        i16 objId = *reinterpret_cast<i16*>(result);
        u8 typeByte = g_resetHooks.objectTypeByte ? g_resetHooks.objectTypeByte(objId) : 0;
        if (typeByte == 29) {
            // *((_BYTE*)result + 32) &= ~2u;
            result[32] &= static_cast<u8>(~2u);
        }
    }
    return result;
}

} // namespace guild::play
