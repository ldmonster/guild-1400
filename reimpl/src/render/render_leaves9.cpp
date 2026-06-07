#include "render/render_leaves9.h"

#include <cstring>

namespace guild::render {

namespace {

inline const u8* BP(const void* p) { return static_cast<const u8*>(p); }
inline u8*       BP(void* p)       { return static_cast<u8*>(p); }

template <class T> inline T Rd(const void* p, int off) {
    T v; std::memcpy(&v, BP(p) + off, sizeof(T)); return v;
}
template <class T> inline void Wr(void* p, int off, T v) {
    std::memcpy(BP(p) + off, &v, sizeof(T));
}

// ---------------------------------------------------------------------------
// Inert default hooks.
// ---------------------------------------------------------------------------
int   DefaultFrameDataProcess(int, int, void*, int) { return 0; }
void* DefaultConvertRgbTo16(void*)                   { return nullptr; }
void* DefaultConvert8To16(void*)                     { return nullptr; }

} // namespace

RenderLeaves9Hooks& Leaves9Hooks() {
    static RenderLeaves9Hooks h{
        DefaultFrameDataProcess, DefaultConvertRgbTo16, DefaultConvert8To16};
    return h;
}
void SetLeaves9Hooks(const RenderLeaves9Hooks& h) { Leaves9Hooks() = h; }
void ResetLeaves9Hooks() {
    Leaves9Hooks() = RenderLeaves9Hooks{
        DefaultFrameDataProcess, DefaultConvertRgbTo16, DefaultConvert8To16};
}

// ===========================================================================
// 0x407414 — VIBE_Vector_Copy3
// ===========================================================================
u32* Vector_Copy3(u32* dst, const u32* src) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    return dst;
}

// ===========================================================================
// 0x5d4bf8 — VIBE_Shape_CopyPaletteHeader
//   result = 4 * *(u16*)(shape + 10);
//   memcpy(shape + *(u32*)(shape + 42), src, result);
// ===========================================================================
u32 Shape_CopyPaletteHeader(const void* src, void* shape) {
    u32 result = 4u * static_cast<u32>(Rd<u16>(shape, 10));
    u32 dstOff = Rd<u32>(shape, 42);
    std::memcpy(BP(shape) + dstOff, src, result);
    return result;
}

// ===========================================================================
// 0x5de760 — VIBE_Render_RetTrue
// ===========================================================================
i8 Render_RetTrue() { return 1; }

// ===========================================================================
// 0x5fa684 — VIBE_Math_ShiftAccumulate
// Registers in the original: result=eax(mid), a2=edx(hi), a3=ebp(lo), a4=edi(exp).
// The 96-bit magnitude being normalized is (hi:mid:lo) = (edx:eax:ebp).
//
// Step 1: if (mid|hi|lo)==0 return mid unchanged.
// Step 2: while hi==0, shift the whole thing up by one 32-bit word
//         (hi=mid, mid=lo, lo=0, exp-=32) — done at most twice.
// Step 3: while hi's top bit is clear, exp--, shift (hi:mid:lo) left by 1.
// Step 4: round — shift lo left by 1 (carry = old top bit of lo = guard bit),
//         propagate add-carry into mid then hi; if that overflows out of hi,
//         rotate-carry-right into hi's top bit and exp++.
// Returns the normalized low dword of the 64-bit significand (mid).
// ===========================================================================
u32 ShiftAccumulate(u32 mid, u32 hi, u32 lo, i32 exp,
                    u32* hiOut, u32* loOut, u16* expOut) {
    u32 d = hi;     // edx
    u32 a = mid;    // eax
    u32 b = lo;     // ebp
    i32 e = exp;    // edi

    if ((a | d | b) != 0) {
        // Step 2 (word shifts), at most twice.
        if (d == 0) {
            d = a;
            a = b;
            b = 0;
            e -= 32;
        }
        if (d == 0) {
            d = a;
            a = b;
            b = 0;
            e -= 32;
        }
        // Step 3: left-justify until the top bit of d (hi) is set.
        while (static_cast<i32>(d) >= 0) {   // js => top bit set ends loop
            --e;
            // shift (d:a:b) left by 1
            u32 carry0 = b >> 31;
            b <<= 1;
            u32 carry1 = a >> 31;
            a = (a << 1) | carry0;
            d = (d << 1) | carry1;
        }
        // Step 4: round (add the guard bit b's top bit upward).
        u32 guard = b >> 31;                 // carry out of (b + b)
        // b <<= 1; (value discarded after this point)
        u64 sum = static_cast<u64>(a) + guard;
        a = static_cast<u32>(sum);
        u32 carryA = static_cast<u32>(sum >> 32);
        u64 sumD = static_cast<u64>(d) + carryA;
        d = static_cast<u32>(sumD);
        u32 carryD = static_cast<u32>(sumD >> 32);
        if (carryD) {
            // rcr edx,1 — rotate the out-going carry into the top bit of d.
            d = (d >> 1) | 0x80000000u;
            ++e;
        }
    }

    if (hiOut)  *hiOut = d;
    if (loOut)  *loOut = a;
    if (expOut) *expOut = static_cast<u16>(e);
    return a;
}

// ===========================================================================
// 0x5fa622 — VIBE_Math_ParseDecimal
// 96-bit accumulator (hi:mid:lo) = value*10 + digit per ASCII digit, then
// normalize through ShiftAccumulate with base exponent 0x405E (16478).
// ===========================================================================
u32 ParseDecimal(const char* digits, Extended80* out) {
    u32 lo = 0, mid = 0, hi = 0;            // ebp, ecx, edx
    const u8* p = reinterpret_cast<const u8*>(digits);

    while (*p) {
        // save old value
        u32 oldLo = lo, oldMid = mid, oldHi = hi;
        auto shl1 = [&]() {                  // (hi:mid:lo) <<= 1
            u32 c0 = lo >> 31;
            lo <<= 1;
            u32 c1 = mid >> 31;
            mid = (mid << 1) | c0;
            hi  = (hi << 1) | c1;
        };
        auto add96 = [&](u32 al, u32 am, u32 ah) {   // value += (ah:am:al)
            u64 s = static_cast<u64>(lo) + al;
            lo = static_cast<u32>(s);
            u64 s2 = static_cast<u64>(mid) + am + static_cast<u32>(s >> 32);
            mid = static_cast<u32>(s2);
            hi = hi + ah + static_cast<u32>(s2 >> 32);
        };
        shl1();                              // 2v
        shl1();                              // 4v
        add96(oldLo, oldMid, oldHi);         // 5v
        shl1();                              // 10v
        u32 digit = static_cast<u32>(*p & 0x0F);
        add96(digit, 0, 0);                  // 10v + digit
        ++p;
    }

    // call VIBE_Math_ShiftAccumulate(eax=mid, edx=hi, ebp=lo, edi=0x405E)
    u32 sigHi = 0, sigLo = 0;
    u16 expOut = 0;
    u32 ret = ShiftAccumulate(mid, hi, lo, 0x405E, &sigHi, &sigLo, &expOut);

    if (out) {
        out->mantissaLo = sigLo;
        out->mantissaHi = sigHi;
        out->exponent   = expOut;
    }
    return ret;
}

// ===========================================================================
// 0x5d8968 — VIBE_Shape_DrawFromBankMode1
//   if (shapeIdx > *(u16*)(bank+42)) return 0;
//   v7 = bank + 4*shapeIdx;
//   v8 = bank + *(u32*)(v7+69);
//   saved = *(u8*)(v8+13);  *(u8*)(v8+13) = 1;
//   FrameDataProcess(a3, a2, v8, a4);
//   *(u8*)(v8+13) = saved;  return 1;
// ===========================================================================
i32 Shape_DrawFromBankMode1(void* bank, int a2, int a3, int a4, u8 shapeIdx) {
    if (static_cast<int>(shapeIdx) > static_cast<int>(Rd<u16>(bank, 42)))
        return 0;
    u8* b = BP(bank);
    int v7off = 4 * shapeIdx;
    u32 frameDelta = Rd<u32>(bank, v7off + 69);
    void* frame = b + frameDelta;
    u8 saved = Rd<u8>(frame, 13);
    Wr<u8>(frame, 13, 1);
    Leaves9Hooks().FrameDataProcess(a3, a2, frame, a4);
    Wr<u8>(frame, 13, saved);
    return 1;
}

// ===========================================================================
// 0x5d88e4 — VIBE_Shape_DrawFromBankMode5
//   if (!bank) return 0;
//   if (shapeIdx > *(u16*)(bank+42)) { /* original logs via sprintf */ return 0; }
//   ... (identical bracketing as Mode1, draw-mode = 5)
// ===========================================================================
i32 Shape_DrawFromBankMode5(void* bank, int a2, int a3, int a4, u8 shapeIdx) {
    if (!bank)
        return 0;
    if (static_cast<int>(shapeIdx) > static_cast<int>(Rd<u16>(bank, 42)))
        return 0;   // original emits "shp_ShowShapeFromBank:Shapenr is invalidate!"
    u8* b = BP(bank);
    int v7off = 4 * shapeIdx;
    u32 frameDelta = Rd<u32>(bank, v7off + 69);
    void* frame = b + frameDelta;
    u8 saved = Rd<u8>(frame, 13);
    Wr<u8>(frame, 13, 5);
    Leaves9Hooks().FrameDataProcess(a3, a2, frame, a4);
    Wr<u8>(frame, 13, saved);
    return 1;
}

// ===========================================================================
// 0x5d8080 — VIBE_Shape_ConvertToNew
//   if (*(u8*)(shape+12) == 2 && target == 1) return ConvertRgbTo16(shape);
//   if (*(u8*)(shape+12) == 0 && target == 1) return Convert8To16(shape);
//   return 0;
// ===========================================================================
void* Shape_ConvertToNew(void* shape, i8 target) {
    u8 depth = Rd<u8>(shape, 12);
    if (depth == 2 && target == 1)
        return Leaves9Hooks().ConvertRgbTo16(shape);
    if (depth == 0 && target == 1)
        return Leaves9Hooks().Convert8To16(shape);
    return nullptr;
}

} // namespace guild::render
