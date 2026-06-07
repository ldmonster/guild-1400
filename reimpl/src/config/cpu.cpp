#include "config/cpu.h"

namespace guild::config {

// Decode CPUID.01h. EAX layout:
//   bits  3:0  stepping
//   bits  7:4  base model
//   bits 11:8  base family
//   bits 19:16 extended model
//   bits 27:20 extended family
// Folding rules (Intel SDM):
//   effective family = base family + (base family == 0x0F ? ext family : 0)
//   effective model  = base model  | (ext model << 4)   when family is 0x06 or 0x0F
CpuFeatures DecodeFeatures(u32 eax, u32 ecx, u32 edx) {
    CpuFeatures f;
    f.edxFlags = edx;
    f.ecxFlags = ecx;

    f.stepping = eax & 0xF;
    u32 baseModel = (eax >> 4) & 0xF;
    u32 baseFamily = (eax >> 8) & 0xF;
    u32 extModel = (eax >> 16) & 0xF;
    u32 extFamily = (eax >> 20) & 0xFF;

    f.family = baseFamily;
    if (baseFamily == 0x0F)
        f.family = baseFamily + extFamily;

    f.model = baseModel;
    if (baseFamily == 0x06 || baseFamily == 0x0F)
        f.model = baseModel | (extModel << 4);

    return f;
}

CpuFeatures DetectFeatures(CpuidShim& shim) {
    CpuidRegs r = shim.query(1, 0);
    return DecodeFeatures(r.eax, r.ecx, r.edx);
}

} // namespace guild::config
