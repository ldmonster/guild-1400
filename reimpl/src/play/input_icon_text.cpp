#include "play/input_icon_text.h"

#include "util/mem_ops.h"   // util::MemMove == VIBE_Util_MemMove @0x5d9310

#include <cstring>

namespace guild::play {

// gilde.exe 0x40fb4c — VIBE_Input_SetIconTextById.
u16 Input_SetIconTextById(IconTextObject& obj, i32 editBufBase,
                          const IconTextUnderlying& under,
                          const char* newText, IconTextMeasureFn measure) {
    // Gate: under (wrec+0x2C) must be present AND obj[+0x150] (edit buffer) non-null.
    //   test esi,esi / jz ; cmp [ebp+150h],0 / jz
    if (!under.present || obj.editBuf == nullptr)
        return 0;

    unsigned char* base = obj.editBuf;          // [ebp+150h] (storage at addr editBufBase)

    // tail bytes from `base` to the end (incl. NUL) of the underlying text region:
    //   *(i32*)(under+0x2C) + strlen(*(char**)(under+0x2C)) + 1 - base
    auto tailLen = [&]() -> std::size_t {
        const i32 end = under.regionBase
                        + static_cast<i32>(under.text ? std::strlen(under.text) : 0)
                        + 1;
        return static_cast<std::size_t>(end - editBufBase);
    };

    // Remove the previously-inserted substring (if any):
    //   if ([ebp+154h] != 0) MemMove(base, base + len, tail);
    if (obj.insertedLen != 0) {
        util::MemMove(base, base + obj.insertedLen, tailLen());
    }

    // newLen = strlen(dword_8C36B0[a2]); obj[+0x154] = newLen.
    const std::size_t newLen = newText ? std::strlen(newText) : 0;
    obj.insertedLen = static_cast<i32>(newLen);

    // Open a gap of newLen at base, then splice in the new string:
    //   MemMove(base + newLen, base, tail2);  qmemcpy(base, newText, newLen);
    util::MemMove(base + newLen, base, tailLen());
    if (newLen) std::memcpy(base, newText, newLen);

    // Re-measure and store the width:
    //   w = Property_Get(newText, obj[+0x134]); obj[+0x10] = w;
    //   *(u16*)(wrec+20) = (u16)obj[+0x10];
    i16 w = measure ? measure(newText, obj.font) : 0;
    obj.measuredW = w;
    return static_cast<u16>(obj.measuredW);     // the low word pushed to wrec+20
}

} // namespace guild::play
