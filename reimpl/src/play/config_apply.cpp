#include "play/config_apply.h"

#include "gui/input_state.h"

namespace guild::play {

// gilde.exe 0x56c0cc — VIBE_Config_ApplyCameraAndScrollSettings.
// The advanced-scroll branch fild's dword_631284 as UNSIGNED (the disasm pushes a
// zero high dword then fild [qword]); the linear branch fild's the signed int
// dword_1233558.  The scroll-speed result is narrowed to a 32-bit float (fstp
// dword) before storing — reproduced via the float cast.  SetWheelBase folds in
// the +256 the original adds inside VIBE_Input_SetWheelBase.
i32 ConfigApplyCameraAndScrollSettings(const CameraScrollOptions& opt,
                                       bool advancedScroll, u32 advancedRaw,
                                       CameraScrollGlobals& g) {
    using namespace cfgcam;

    g.focusReset = 0;                                  // dword_62D0E4 = 0

    double v0;
    if (advancedScroll) {                              // (word_63C740 & 4) != 0
        // (double)(unsigned)dword_631284 * 0.25 + 0.5) * 0.8
        v0 = (static_cast<double>(advancedRaw) * kLinearBase + kAdvancedBase) *
             kAdvancedScale;
    } else {
        // (double)(int)dword_1233558 * 0.00625 + 0.25
        v0 = static_cast<double>(opt.field0) * static_cast<double>(kLinearSlope) +
             kLinearBase;
    }
    g.scrollSpeed = static_cast<float>(v0);            // dword_62D07C (fstp dword)

    // VIBE_Input_SetWheelBase(dword_1233560 - 64) -> dword_62D0B8 = (wheel-64)+256
    g.wheelBase = guild::gui::Input_SetWheelBase(opt.wheel - 64);

    i32 result = opt.sensitivityByte;                  // movzx eax, byte [edx+4]
    g.edgeMargin = 100 - result;                       // dword_6316C8 = 100 - sensitivity
    return result;
}

}  // namespace guild::play
