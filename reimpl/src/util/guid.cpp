#include "util/guid.h"

#include <cstdio>

namespace guild::util {

// The canonical format string, byte-identical to the literal at gilde.exe 0x614ee8.
static const char kGuidFormat[] =
    "%08x-%04x-%04x-%02x%02x%02x%02x%02x%02x%02x%02x";

// gilde.exe 0x432a0c — VIBE_Guid_Format
//   Original calls VIBE_Crt_Sprintf_0(out, fmt, data1, data2, data3,
//   data4[0..7]) with the trailing high byte taken from src[15]. We reproduce
//   the exact field order and lowercase %0Nx formatting via std::sprintf.
int GuidFormat(const Guid* src, char* out) {
    if (!src || !out)
        return 0;
    std::sprintf(out, kGuidFormat,
                 src->data1,
                 static_cast<unsigned>(src->data2),
                 static_cast<unsigned>(src->data3),
                 static_cast<unsigned>(src->data4[0]),
                 static_cast<unsigned>(src->data4[1]),
                 static_cast<unsigned>(src->data4[2]),
                 static_cast<unsigned>(src->data4[3]),
                 static_cast<unsigned>(src->data4[4]),
                 static_cast<unsigned>(src->data4[5]),
                 static_cast<unsigned>(src->data4[6]),
                 static_cast<unsigned>(src->data4[7]));
    return 1;
}

// gilde.exe 0x432a70 — VIBE_Guid_Parse
//   if (!str || !*str || !out) return 0;
//   sscanf(str, fmt, &d1,&d2,&d3, &b0..&b7); store; return 1;
// Each conversion targets an int-sized temporary in the original; the byte
// fields keep only the low byte. We mirror that exactly.
bool GuidParse(const char* str, Guid* out) {
    if (!str || !*str || !out)
        return false;

    unsigned d1 = 0, d2 = 0, d3 = 0;
    unsigned b[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::sscanf(str, kGuidFormat, &d1, &d2, &d3,
                &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7]);

    out->data1 = static_cast<u32>(d1);
    out->data2 = static_cast<u16>(d2);
    out->data3 = static_cast<u16>(d3);
    for (int i = 0; i < 8; ++i)
        out->data4[i] = static_cast<u8>(b[i]);
    return true;
}

} // namespace guild::util
