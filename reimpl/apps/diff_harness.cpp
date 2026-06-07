// apps/diff_harness.cpp — reconstruction side of the 1:1 differential tester.
//
// Runs a reconstructed function on inputs given as argv and prints its output as
// hex to stdout. The Python driver (tools/diff_test.py) emulates the ORIGINAL
// function's machine code (from the decrypted IDB) in Unicorn on the SAME inputs
// and asserts byte-identical output. This is the real 1:1 behaviour check:
// reconstructed C++  ==  original x86, not just golden vectors.
//
// Built outside src/ (apps/) so the src/**/*.cpp glob never pulls this main()
// into libguild. Linked against libguild.a:
//   g++ -std=c++17 -I. -Iinclude -Isrc apps/diff_harness.cpp build/libguild.a -o diff_harness
//
// Usage: diff_harness <fn> <args...>   ->  prints hex result line.
//
// Output encodings (must match the byte layout the Python side reads):
//   - integer / pointer results: "%08x" of the 32-bit eax.
//   - byte buffers:              concatenated "%02x".
//   - x87 float (ST0) results:   the IEEE-754 double bit pattern as 16 hex
//                                digits, little-endian-agnostic (we print the
//                                raw u64), so the Python side compares the exact
//                                double the original stored via `fstp qword`.
//   - float[] buffers:          each element as its raw u32 bit pattern, "%08x".

#include "util/color.h"
#include "util/math.h"
#include "compress/crc.h"
// ---- per-day simulation-path leaves (M4 compositional diff coverage) ----
#include "sim/gametime.h"             // GameTimeCompare/DiffMinutes/Advance
#include "sim/types.h"                // GameTime
#include "sim/building_production.h"  // GameTime_PackToRecord / PackedTime
#include "util/math_random.h"         // RandomModulo
#include "crt/rand.h"                 // RandNext / Srand
#include "world/office.h"             // OfficeGetCategoryByRank / GetDefinition
#include "world/amt_slot_table.h"     // AmtGetOfficeType

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;

namespace {
unsigned U(const char* s) { return (unsigned)strtoul(s, nullptr, 0); }
long long I(const char* s) { return (long long)strtoll(s, nullptr, 0); }
unsigned long long UL(const char* s) { return strtoull(s, nullptr, 0); }
float F(const char* s) { return std::strtof(s, nullptr); }

// Print a double as its raw 64-bit IEEE pattern (matches `fstp qword` bytes).
void emit_double(double d) {
    std::uint64_t bits;
    std::memcpy(&bits, &d, 8);
    std::printf("%016llx\n", (unsigned long long)bits);
}
// Print a float's raw 32-bit pattern.
void emit_float(float f) {
    std::uint32_t b;
    std::memcpy(&b, &f, 4);
    std::printf("%08x", (unsigned)b);
}
// Parse a hex byte-string ("aabbcc") into bytes.
std::vector<u8> parse_hex(const char* hx) {
    std::vector<u8> data;
    auto nib = [](char ch) { return (ch <= '9') ? ch - '0' : (ch | 0x20) - 'a' + 10; };
    for (std::size_t i = 0; hx[i] && hx[i + 1]; i += 2)
        data.push_back((u8)((nib(hx[i]) << 4) | nib(hx[i + 1])));
    return data;
}
// Parse N comma-separated floats from one argv token.
std::vector<float> parse_floats(const char* s, int n) {
    std::vector<float> v;
    const char* p = s;
    for (int i = 0; i < n; ++i) {
        v.push_back(std::strtof(p, const_cast<char**>(&p)));
        if (*p == ',') ++p;
    }
    return v;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { std::printf("ERR no fn\n"); return 2; }
    std::string fn = argv[1];

    // ---- util::color (pure register/buffer leaves) ----
    if (fn == "ColorSetRgb") {
        // ABI: dst@eax, a@dl, b@cl, c@bl. args: a b c
        u8 dst[3] = {0, 0, 0};
        util::ColorSetRgb(dst, (u8)U(argv[2]), (u8)U(argv[3]), (u8)U(argv[4]));
        std::printf("%02x%02x%02x\n", dst[0], dst[1], dst[2]);
        return 0;
    }
    if (fn == "ColorNotEqualRgb") {
        // ABI: a@eax, b@edx. args: a0 a1 a2 b0 b1 b2 -> returns 0/1 in eax
        u8 a[3] = {(u8)U(argv[2]), (u8)U(argv[3]), (u8)U(argv[4])};
        u8 b[3] = {(u8)U(argv[5]), (u8)U(argv[6]), (u8)U(argv[7])};
        std::printf("%08x\n", (unsigned)util::ColorNotEqualRgb(a, b));
        return 0;
    }

    // ---- compress::crc (global table + intra-image callee) ----
    if (fn == "CrcCompute") {
        // ABI: crc@eax, data@edx, len@ebx. args: crc(hex) datahex
        u32 crc = (u32)U(argv[2]);
        std::vector<u8> data = parse_hex(argv[3]);
        u32 r = compress::CrcCompute(crc, data.data(), (u32)data.size());
        std::printf("%08x\n", r);
        return 0;
    }

    // ---- util::math integer-register leaves ----
    if (fn == "ClampValueRange") {
        // ABI: lo@eax, value@edx, out@ecx, mul@ebx -> ret 0/1 eax, *out set.
        // args: lo value mul. Output: "<ret08x><out08x>" so we verify both.
        int out = 0;
        int ret = util::ClampValueRange((int)I(argv[2]), (int)I(argv[3]), &out,
                                        (int)I(argv[4]));
        std::printf("%08x%08x\n", (unsigned)ret, (unsigned)out);
        return 0;
    }
    if (fn == "Multiply64") {
        // ABI: a@edx:eax, b@ebx:ecx -> ret eax. args: a_lo a_hi b_lo b_hi
        i32 r = util::Multiply64((i32)I(argv[2]), (i32)I(argv[3]),
                                 (i32)I(argv[4]), (i32)I(argv[5]));
        std::printf("%08x\n", (unsigned)r);
        return 0;
    }
    if (fn == "LongLongDivide") {
        // 64-bit signed quotient (low 32 returned in eax). args: num den (dec/hex)
        i64 r = util::LongLongDivide((i64)I(argv[2]), (i64)I(argv[3]));
        std::printf("%08x\n", (unsigned)(u32)(u64)r);
        return 0;
    }
    if (fn == "UnsignedLongLongDivide") {
        // 64-bit unsigned quotient (low 32 returned in eax). args: num den
        u64 r = util::UnsignedLongLongDivide(UL(argv[2]), UL(argv[3]));
        std::printf("%08x\n", (unsigned)(u32)r);
        return 0;
    }

    // ---- util::math x87 ST0 float returns (the documented float gap) ----
    if (fn == "Distance2D") {
        // ABI: ax@eax, dy@edx, cy@ecx, bx@ebx -> ST0. Reads dbl_6264DC (DGROUP).
        // args: ax dy cy bx
        double r = util::Distance2D((int)I(argv[2]), (int)I(argv[3]),
                                    (int)I(argv[4]), (int)I(argv[5]));
        emit_double(r);
        return 0;
    }
    if (fn == "MaxVectorLength") {
        // ABI: vecs@eax, count@edx -> ST0. args: count v0x,v0y,v0z,pad,v1x,...
        int count = (int)I(argv[2]);
        std::vector<float> v = parse_floats(argv[3], count * 4);
        v.resize((std::size_t)count * 4, 0.0f);
        double r = util::MaxVectorLength(v.data(), count);
        emit_double(r);
        return 0;
    }
    if (fn == "CatmullRomInterp") {
        // ABI: __stdcall(p0,p1,p2,p3,t) -> ST0. args: p0 p1 p2 p3 t
        double r = util::CatmullRomInterp(F(argv[2]), F(argv[3]), F(argv[4]),
                                          F(argv[5]), F(argv[6]));
        emit_double(r);
        return 0;
    }

    // ---- util::math float-pointer + bool / int returns ----
    if (fn == "VectorWithinTolerance") {
        // ABI: a@eax, b@edx, tol@stack -> eax 0/1. args: ax,ay,az bx,by,bz tol
        std::vector<float> a = parse_floats(argv[2], 3);
        std::vector<float> b = parse_floats(argv[3], 3);
        float tol = F(argv[4]);
        std::printf("%08x\n", (unsigned)util::VectorWithinTolerance(a.data(),
                                                                    b.data(), tol));
        return 0;
    }
    if (fn == "LerpClampedCoord") {
        // ABI: __stdcall(a,b,span,d) -> int eax. args: a b span d
        int r = util::LerpClampedCoord(F(argv[2]), F(argv[3]), F(argv[4]),
                                       F(argv[5]));
        std::printf("%08x\n", (unsigned)r);
        return 0;
    }

    // ---- util::math float-buffer writers (output = the written floats) ----
    if (fn == "VectorLerp") {
        // ABI: a@eax, b@edx, t@stack, out@stack. args: ax,ay,az bx,by,bz t
        std::vector<float> a = parse_floats(argv[2], 3);
        std::vector<float> b = parse_floats(argv[3], 3);
        float t = F(argv[4]);
        float out[3] = {0, 0, 0};
        util::VectorLerp(a.data(), b.data(), t, out);
        emit_float(out[0]); emit_float(out[1]); emit_float(out[2]);
        std::printf("\n");
        return 0;
    }
    if (fn == "VectorNormalize") {
        // ABI: v@eax (in place) -> returns v. args: vx,vy,vz
        std::vector<float> v = parse_floats(argv[2], 3);
        util::VectorNormalize(v.data());
        emit_float(v[0]); emit_float(v[1]); emit_float(v[2]);
        std::printf("\n");
        return 0;
    }
    if (fn == "TriangleNormal") {
        // ABI: a@eax, b@edx, c@ecx, out@ebx. args: a b c (each x,y,z)
        std::vector<float> a = parse_floats(argv[2], 3);
        std::vector<float> b = parse_floats(argv[3], 3);
        std::vector<float> c = parse_floats(argv[4], 3);
        float out[3] = {0, 0, 0};
        util::TriangleNormal(a.data(), b.data(), c.data(), out);
        emit_float(out[0]); emit_float(out[1]); emit_float(out[2]);
        std::printf("\n");
        return 0;
    }
    if (fn == "CubicBezierPoint") {
        // ABI: p0@eax, p1@edx, p2@ecx, p3@ebx, t@stack, out@stack.
        // args: p0(x,y) p1 p2 p3 t  (2-float points). out[2] written.
        std::vector<float> p0 = parse_floats(argv[2], 2);
        std::vector<float> p1 = parse_floats(argv[3], 2);
        std::vector<float> p2 = parse_floats(argv[4], 2);
        std::vector<float> p3 = parse_floats(argv[5], 2);
        float t = F(argv[6]);
        float out[2] = {0, 0};
        util::CubicBezierPoint(p0.data(), p1.data(), p2.data(), p3.data(), t, out);
        emit_float(out[0]); emit_float(out[1]);
        std::printf("\n");
        return 0;
    }

    // ====================================================================
    // Per-day simulation-path leaves (M4 compositional 1:1 coverage).
    // Each is on the day-tick call tree of VIBE_GameTick_BeginPlayerRound
    // (0x533188) / VIBE_Command_ExAdvanceGameTick (0x498954) and is pure /
    // deterministic (reads only DGROUP tables / its own RNG state; no Win32).
    // ====================================================================

    // ---- sim::gametime — calendar arithmetic (no calls, pure ptr math) ----
    // GameTime record layout: day(i32@0) hour(u16@4) minute(i32@6) second(i32@10).
    if (fn == "GameTimeCompare") {
        // ABI: a@eax, b@edx -> eax (-1/0/+1). args: aDay aHour aMin aSec  bDay...
        sim::GameTime a{(i32)I(argv[2]), (u16)U(argv[3]), (i32)I(argv[4]), (i32)I(argv[5])};
        sim::GameTime b{(i32)I(argv[6]), (u16)U(argv[7]), (i32)I(argv[8]), (i32)I(argv[9])};
        std::printf("%08x\n", (unsigned)sim::GameTimeCompare(&a, &b));
        return 0;
    }
    if (fn == "GameTimeDiffMinutes") {
        // ABI: a@eax, b@edx -> eax (b-a in minutes). args: aDay aHour aMin aSec bDay...
        sim::GameTime a{(i32)I(argv[2]), (u16)U(argv[3]), (i32)I(argv[4]), (i32)I(argv[5])};
        sim::GameTime b{(i32)I(argv[6]), (u16)U(argv[7]), (i32)I(argv[8]), (i32)I(argv[9])};
        std::printf("%08x\n", (unsigned)sim::GameTimeDiffMinutes(&a, &b));
        return 0;
    }
    if (fn == "GameTimeAdvance") {
        // ABI: rec@eax, addDays@edx, addSeconds@ecx, addMinutes@ebx -> eax(hour)
        //   AND mutates rec. args: day hour min sec addDays addSeconds addMinutes
        // Output: ret(08x) + the resulting 14-byte record (the original's writes).
        sim::GameTime r{(i32)I(argv[2]), (u16)U(argv[3]), (i32)I(argv[4]), (i32)I(argv[5])};
        int ret = sim::GameTimeAdvance(&r, (int)I(argv[6]), (int)I(argv[7]), (int)I(argv[8]));
        u8 rb[14];
        std::memcpy(rb, &r, 14);
        std::printf("%08x", (unsigned)ret);
        for (int i = 0; i < 14; ++i) std::printf("%02x", rb[i]);
        std::printf("\n");
        return 0;
    }
    if (fn == "GameTimePackToRecord") {
        // ABI: src@eax, out@edx. Writes a 12-byte record at `out`:
        //   out+0=1; out+1=3*(day%4)+1; out+2(word)=(srcWord0)+1400;
        //   out+4=src+4 byte; out+5=src+6 byte; out+8(dword)=src+10.
        // The original NEVER writes out+3,+6,+7, so we compare only written bytes.
        // args: srcDay srcHour srcMin srcSec  (src is a GameTime).
        i32 day = (i32)I(argv[2]);
        u16 hourWord = (u16)U(argv[3]);
        i32 minute = (i32)I(argv[4]);
        i32 second = (i32)I(argv[5]);
        sim::PackedTime p = sim::GameTime_PackToRecord(day, hourWord, minute, second);
        // Rebuild the original's exact 12-byte output buffer from the fields.
        // *(_WORD*)a1 == day's low word; *(a1+4) low byte == hourWord low byte;
        // *(a1+6) low byte == minute low byte; *(a1+10) == second.
        u8 out[12];
        std::memset(out, 0, sizeof(out));
        out[0] = (u8)p.season;                       // = 1
        out[1] = (u8)p.dayInSeason;                  // 3*(day%4)+1
        u16 yt = p.yearTag;                          // (day low word)+1400
        std::memcpy(out + 2, &yt, 2);
        out[4] = p.hour;                             // src+4 low byte
        out[5] = p.minuteByte;                       // src+6 low byte
        std::memcpy(out + 8, &p.cursor, 4);          // src+10 dword
        // print only the bytes the original actually writes: 0,1,2,3,4,5,8,9,10,11
        static const int W[] = {0, 1, 2, 3, 4, 5, 8, 9, 10, 11};
        for (int i : W) std::printf("%02x", out[i]);
        std::printf("\n");
        return 0;
    }

    // ---- RNG: crt LCG + Math_RandomModulo (in-image rand chain) ----
    if (fn == "RandNext") {
        // ABI: __cdecl -> eax. Reads/advances LCG state. arg: seed (state before).
        crt::Srand((u32)U(argv[2]));
        std::printf("%08x\n", (unsigned)crt::RandNext());
        return 0;
    }
    if (fn == "RandomModulo") {
        // ABI: n@ax -> eax = RandNext()%n (0 if n==0). args: seed n
        crt::Srand((u32)U(argv[2]));
        std::printf("%08x\n", (unsigned)util::RandomModulo((u16)U(argv[3])));
        return 0;
    }

    // ---- office/law DGROUP-table readers (on the Amt/law per-day path) ----
    if (fn == "AmtGetOfficeType") {
        // ABI: type@al -> eax. arg: type. Reads dword_62EC8E office-def table.
        std::printf("%08x\n", (unsigned)world::AmtGetOfficeType((u8)U(argv[2])));
        return 0;
    }
    if (fn == "OfficeGetCategoryByRank") {
        // ABI: rank@al -> al. arg: rank. Reads HIBYTE(dword_62EC8E[3*rank]).
        std::printf("%08x\n", (unsigned)(u8)world::OfficeGetCategoryByRank((u8)U(argv[2])));
        return 0;
    }
    if (fn == "OfficeGetDefinition") {
        // ABI: rank@al, out@edx -> eax(0/1) + fills 3-dword OfficeDef at out.
        // arg: rank. Output: ret(08x) + word0,flag,textId each 08x (12 bytes).
        world::OfficeDef d{};
        int ret = world::OfficeGetDefinition((u8)U(argv[2]), &d);
        std::printf("%08x%08x%08x%08x\n", (unsigned)ret, (unsigned)d.word0,
                    (unsigned)d.flag, (unsigned)d.textId);
        return 0;
    }

    std::printf("ERR unknown fn %s\n", fn.c_str());
    return 2;
}
