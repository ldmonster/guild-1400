// =============================================================================
// shape_showbank_misc_recon.cpp — VIBE_Velocity_Apply (0x5d883c). 1:1.
// =============================================================================
#include "render/shape_showbank_misc_recon.h"

namespace guild::render {

// gilde.exe 0x5d883c — VIBE_Velocity_Apply ("shp_ShowShapeFromBank").
//   if ( !bank ) return 0;
//   if ( shapeNr > *(u16 *)(bank + 0x2a) ) { sprintf error(bank+0x0a); return 0; }
//   v7    = bank + 4*shapeNr;
//   v8    = bank + *(u32 *)(v7 + 0x45);
//   saved = *(u8 *)(v8 + 0x0d);
//   *(u8 *)(v8 + 0x0d) = 2;
//   VIBE_FrameData_Process(a1, a2, *(u32 *)(v7 + 0x45) + bank, a4);
//   *(u8 *)(v8 + 0x0d) = saved;
//   return 1;
int Shape_ShowFromBank(int a1, int a2, u8* bank, int a4, u8 shapeNr,
                       FrameDataProcessFn process, ShapeErrorFn onError) {
    if (!bank)                                                       /*0x5d8849*/
        return 0;
    // movzx esi, shapeNr; movzx ebp, word [bank+0x2a]; cmp; jle ok.
    const int count = *reinterpret_cast<const u16*>(bank + 0x2a);    /*0x5d8853*/
    if (static_cast<int>(shapeNr) > count) {                         /*0x5d8859*/
        if (onError)
            onError(reinterpret_cast<const char*>(bank + 0x0a));     /*0x5d8869 bank+0x0a*/
        return 0;                                                    /*0x5d8871*/
    }
    u8* v7 = bank + 4 * static_cast<int>(shapeNr);                   /*0x5d887f shl 2; lea*/
    const u32 shapeOff = *reinterpret_cast<const u32*>(v7 + 0x45);   /*0x5d8885 [ebp+45h]*/
    u8* shape = bank + shapeOff;                                     /*0x5d8888 add esi,ecx*/
    const u8 saved = shape[0x0d];                                    /*0x5d888a mov dl,[esi+0Dh]*/
    shape[0x0d] = 2;                                                 /*0x5d888d mov [esi+0Dh],2*/
    process(a1, a2, bank + shapeOff, a4);                            /*0x5d889d FrameData_Process*/
    shape[0x0d] = saved;                                             /*0x5d88a9 restore*/
    return 1;                                                        /*0x5d88ac*/
}

} // namespace guild::render
