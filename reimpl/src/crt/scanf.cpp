#include "crt/scanf.h"
#include "crt/ctype.h"   // kStrtolCtype == byte_64A208

#include <cstring>
#include <cstdlib>

namespace guild::crt {

namespace {

// byte_64AD14 @0x64ad14 — single-bit masks for the %[...] 256-bit char set.
//   {1,2,4,8,16,32,64,128}
constexpr u8 kBitMask[8] = {1, 2, 4, 8, 16, 32, 64, 128};

// Flag bits in the scan-state byte (offset +16 in the original stream struct).
constexpr u8 kAssign = 0x01; // assignment requested (cleared by '*')
constexpr u8 kEof     = 0x02; // input exhausted / error
constexpr u8 kI64Far  = 0x04; // F / I64 — 8-byte target
constexpr u8 kNear    = 0x08; // N
constexpr u8 kShort   = 0x10; // h
constexpr u8 kLong    = 0x20; // l / w (wide)
constexpr u8 kLongLong = 0x40; // L / I64 (64-bit integer)

inline bool IsWhite(int c) { // byte_64A208[(u8)(c+1)] & 2
    return (kStrtolCtype[static_cast<u8>(c + 1)] & 2) != 0;
}
inline bool IsDigit(int c) { // byte_64A208[(u8)(c+1)] & 0x20
    return (kStrtolCtype[static_cast<u8>(c + 1)] & 0x20) != 0;
}

// VIBE_Util_ToLower (ASCII tolower) for HexDigitValue.
inline int ToLower(int c) {
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

// gilde.exe 0x5fdf30 — VIBE_Crt_HexDigitValue: 0..15, else 16.
int HexDigitValue(int c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    int l = ToLower(c);
    if (l < 'a' || l > 'f')
        return 16;
    return l - 'a' + 10;
}

// The scanf stream: a string source with a field-width counter and flag byte.
// This models the original stream struct {getchar, unget, dataptr, ..., width@+12,
// flags@+16} for the string-backed sscanf entry (VIBE_Crt_StreamGetChar @0x5e5470).
struct Stream {
    const char* p;     // current read position (+8 dataptr in original)
    int   width;       // +12: field width remaining (-1 == unbounded)
    u8    flags;       // +16
    std::va_list* ap;  // assignment argument list

    // VIBE_Crt_StreamGetChar @0x5e5470 — read one char or set EOF and return -1.
    int Get() {
        unsigned char c = static_cast<unsigned char>(*p);
        if (c) { ++p; return c; }
        flags |= kEof;
        return -1;
    }
    // VIBE_Crt_StreamUngetDecrement @0x5e549c — push back one char.
    void Unget() { --p; }

    // VIBE_Crt_ScanNextChar @0x5fdf58 — width-bounded Get.
    int Next() {
        int w = width--;
        if (w == 0) return -1;
        int c = Get();
        if (flags & kEof) return -1;
        return c;
    }

    // Fetch the next assignment target pointer.
    void* Arg() { return va_arg(*ap, void*); }
};

// gilde.exe 0x5fd428 — VIBE_Crt_ScanSkipWhitespace. Consume whitespace, returning
// the count consumed; unget the first non-white char unless at EOF.
int SkipWhitespace(Stream& s) {
    int i = 0;
    int c;
    for (;; ++i) {
        c = s.Get();
        if (!IsWhite(c))
            break;
    }
    if (!(s.flags & kEof))
        s.Unget();
    return i;
}

// gilde.exe 0x5fd324 — VIBE_Crt_ScanParseSpec. Parse '*', width, length mods.
const unsigned char* ParseSpec(const unsigned char* p, Stream& s) {
    s.width = -1;                       // (a2+12) = -1
    s.flags = (s.flags | kAssign) & 3;  // keep EOF, set assign, clear the rest
    if (*p == '*') {                    // suppress assignment
        ++p;
        s.flags &= ~kAssign;
    }
    if (IsDigit(*p)) {                  // field width
        int w = 0;
        do {
            w = *p - '0' + 10 * w;
            ++p;
        } while (IsDigit(*p));
        s.width = w;
    }
    if (*p == 'N') { ++p; s.flags |= kNear; }
    else if (*p == 'F') { ++p; s.flags |= kI64Far; }

    unsigned char m = *p;
    if (m == 'h') { s.flags |= kShort; ++p; }
    else if (m == 'l' || m == 'w') { s.flags |= kLong; ++p; }
    else if (m == 'L') { s.flags |= kLongLong; ++p; }
    else if (m == 'I' && p[1] == '6' && p[2] == '4') { s.flags |= kLongLong; p += 3; }
    return p;
}

// Store an integer result into the next argument per the size flags.
void StoreInt(Stream& s, u64 value) {
    if (!(s.flags & kAssign))
        return;
    void* dst = s.Arg();
    if (s.flags & (kI64Far | kLongLong))
        *static_cast<u64*>(dst) = value;
    else if (s.flags & kShort)
        *static_cast<u16*>(dst) = static_cast<u16>(value);
    else
        *static_cast<u32*>(dst) = static_cast<u32>(value);
}

// gilde.exe 0x5fdb10 — VIBE_Crt_ScanReadInteger.  %d/%i/%u/%o/%x/%p.
int ReadInteger(Stream& s, int base) {
    u64 acc64 = 0;          // 64-bit accumulator (L/I64 path)
    int acc32 = 0;          // 32-bit accumulator
    int v4 = 0;             // digits consumed (significant)
    int leading = 0;        // chars consumed before digits (sign/prefix), 'i' counter
    bool is64 = (s.flags & (kLongLong | kI64Far)) != 0;
    char sign = '+';
    int c = 0;
    int i = 0;

    // Skip leading whitespace.
    for (;; ++i) {
        c = s.Get();
        if (!IsWhite(c))
            break;
    }
    if (s.flags & kEof)
        goto done;
    leading = i;

    {
        int w = s.width--;
        if (w == 0) { s.Unget(); goto done; }
    }

    if (c == '+' || c == '-') {
        sign = static_cast<char>(c);
        ++leading;
        c = s.Next();
        if (c == -1) goto done;
    }

    if (base == 0) {
        if (c == '0') {
            v4 = 1;                     // counts the leading 0 as consumed
            c = s.Next();
            if (c == -1) goto done;
            if (c == 'x' || c == 'X') {
                v4 = 0;
                c = s.Next();
                leading += 2;
                if (c == -1) goto done;
                base = 16;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && c == '0') {
        v4 = 1;
        c = s.Next();
        if (c == -1) goto done;
        if (c == 'x' || c == 'X') {
            v4 = 0;
            c = s.Next();
            leading += 2;
            if (c == -1) goto done;
        }
    }

    if (is64) {
        for (;;) {
            int dv = HexDigitValue(c);
            if (dv >= base) break;
            acc64 = acc64 * static_cast<u32>(base) + static_cast<u32>(dv);
            ++v4;
            c = s.Next();
            if (c == -1) goto done;
        }
    } else {
        for (;;) {
            int dv = HexDigitValue(c);
            if (dv >= base) break;
            acc32 = dv + base * acc32;
            ++v4;
            c = s.Next();
            if (c == -1) goto done;
        }
    }
    s.Unget();

done:
    if (is64) {
        if (sign == '-')
            acc64 = static_cast<u64>(-static_cast<i64>(acc64));
        if (v4 > 0) {
            StoreInt(s, acc64);
            return v4 + leading;
        }
        return v4 + leading;        // 0 if no digits (v4==0)
    } else {
        if (sign == '-')
            acc32 = -acc32;
        if (v4 > 0) {
            StoreInt(s, static_cast<u32>(acc32));
            return v4 + leading;
        }
        return v4 + leading;
    }
}

// gilde.exe 0x5fd838 — VIBE_Crt_ScanReadFloat.  %e/%f/%g.  Collect a numeric token
// then convert with strtod (the original calls off_64ADFC == _atoflt).
int ReadFloat(Stream& s) {
    char buf[128];
    char* w = buf;
    int v3 = 0;             // significant chars collected
    int leading = 0;

    int c;
    int i = 0;
    for (;; ++i) {
        c = s.Get();
        if (!IsWhite(c)) break;
    }
    if (s.flags & kEof) goto finish;
    leading = i;

    { int wd = s.width--; if (wd == 0) { s.Unget(); goto finish; } }

    if (c == '+' || c == '-') {
        *w++ = static_cast<char>(c);
        ++leading;
        c = s.Next();
        if (c == -1) goto finish;
    }
    if (!IsDigit(c) && c != '.') { s.Unget(); goto finish; }

    // Integer digits.
    if (IsDigit(c)) {
        while (true) {
            *w++ = static_cast<char>(c);
            ++v3;
            c = s.Next();
            if (c == -1) goto finish;
            if (!IsDigit(c)) break;
        }
    }
    // Fraction.
    if (c == '.') {
        *w++ = '.';
        c = s.Next();
        if (c == -1) goto finish;
        ++v3;
        while (IsDigit(c)) {
            *w++ = static_cast<char>(c);
            ++v3;
            c = s.Next();
            if (c == -1) goto finish;
        }
    }
    // Exponent.
    if (c == 'e' || c == 'E') {
        *w++ = static_cast<char>(c);
        ++v3;
        c = s.Next();
        if (c == -1) goto finish;
        if (c == '+' || c == '-') {
            *w++ = static_cast<char>(c);
            ++v3;
            c = s.Next();
            if (c == -1) goto finish;
        }
        if (IsDigit(c)) {
            while (true) {
                *w++ = static_cast<char>(c);
                ++v3;
                c = s.Next();
                if (c == -1) goto finish;
                if (!IsDigit(c)) break;
            }
        } else {
            v3 = 0;             // dangling exponent -> not a valid number
        }
    }
    s.Unget();

finish:
    if (v3 > 0) {
        v3 += leading;
        if (s.flags & kAssign) {
            *w = '\0';
            double d = std::strtod(buf, nullptr);
            void* dst = s.Arg();
            if (s.flags & (kLong | kLongLong))
                *static_cast<double*>(dst) = d;     // %lf / %Lf
            else
                *static_cast<float*>(dst) = static_cast<float>(d); // %f
        }
    }
    return v3;
}

// gilde.exe 0x5fd550 — VIBE_Crt_ScanReadString.  %s (whitespace-delimited).
int ReadString(Stream& s) {
    char* dst = nullptr;
    if (s.flags & kAssign)
        dst = static_cast<char*>(s.Arg());

    int i = 0;
    int c;
    for (;;) {                          // skip leading whitespace
        c = s.Get();
        if (!IsWhite(c)) break;
    }
    if (s.flags & kEof) {
        i = 0;
    } else {
        int w = s.width--;
        if (w != 0) {
            do {
                ++i;
                if ((s.flags & kAssign) && dst)
                    *dst++ = static_cast<char>(c);
                c = s.Next();
                if (c == -1)
                    goto term;
            } while (!IsWhite(c));
            s.Unget();
        }
    }
term:
    if ((s.flags & kAssign) && dst && i > 0)
        *dst = '\0';
    return i;
}

// gilde.exe 0x5fd468 — VIBE_Crt_ScanReadChars.  %c.
int ReadChars(Stream& s) {
    char* dst = nullptr;
    if (s.flags & kAssign)
        dst = static_cast<char*>(s.Arg());

    int n = s.width;
    if (n == -1) n = 1;                 // default field width is 1
    int got = 0;
    while (n > 0) {
        int c = s.Get();
        if (s.flags & kEof)
            break;
        ++got;
        --n;
        if ((s.flags & kAssign) && dst)
            *dst++ = static_cast<char>(c);
    }
    return got;
}

// gilde.exe 0x5fd708 — VIBE_Crt_ScanBuildCharSet.  Build a 256-bit membership map
// from the chars between '[' and ']'. Returns the pointer just past ']'.
const unsigned char* BuildCharSet(const unsigned char* spec, u8 set[32]) {
    std::memset(set, 0, 32);
    int c = *spec;
    const unsigned char* p = spec + 1;
    if (c) {
        do {
            set[c >> 3] |= kBitMask[c & 7];
            c = *p;
            if (!*p) break;
            ++p;
        } while (c != ']');
    }
    return p;
}

// gilde.exe 0x5fd74c — VIBE_Crt_ScanReadCharSet.  %[...] / %[^...].
int ReadCharSet(Stream& s, const unsigned char** specp) {
    const unsigned char* spec = *specp;
    bool negate = (*spec == '^');
    if (negate)
        ++spec;
    u8 set[32];
    *specp = BuildCharSet(spec, set);

    char* dst = nullptr;
    if (s.flags & kAssign)
        dst = static_cast<char*>(s.Arg());

    int got = 0;
    if (s.width != 0) {
        int n = s.width;
        do {
            int c = s.Get();
            if (s.flags & kEof)
                break;
            bool inset = (set[c >> 3] & kBitMask[c & 7]) != 0;
            if (inset == negate) {     // membership mismatch -> stop
                s.Unget();
                break;
            }
            ++got;
            if ((s.flags & kAssign) && dst)
                *dst++ = static_cast<char>(c);
            if (n != -1 && --n == 0)
                break;
        } while (true);
    }
    if ((s.flags & kAssign) && dst && got > 0)
        *dst = '\0';
    return got;
}

// gilde.exe 0x5fd6a8 — VIBE_Crt_ScanStoreCount.  %n.
void StoreCount(Stream& s, int count) {
    if (!(s.flags & kAssign))
        return;
    void* dst = s.Arg();
    if (s.flags & kI64Far)
        *static_cast<u64*>(dst) = static_cast<u32>(count);
    else if (s.flags & kShort)
        *static_cast<u16*>(dst) = static_cast<u16>(count);
    else
        *static_cast<u32*>(dst) = static_cast<u32>(count);
}

// gilde.exe 0x5fd070 — VIBE_Crt_ScanFormatCore.  Driver loop.
int FormatCore(Stream& s, const unsigned char* fmt) {
    int assigned = 0;       // v5 — successful assignments
    int consumed = 0;       // v6 — total input chars consumed (for %n)
    s.flags &= ~kEof;

    const unsigned char* p = fmt;
    while (true) {
        unsigned char ch = *p++;
        if (!ch)
            break;

        if (IsWhite(ch)) {
            consumed += SkipWhitespace(s);
            continue;
        }
        if (ch != '%') {
            int c = s.Get();
            if (c != ch) {
                if (s.flags & kEof)
                    break;
                s.Unget();
                break;
            }
            ++consumed;
            continue;
        }

        // Conversion.
        p = ParseSpec(p, s);
        unsigned char conv = *p;
        if (*p)
            ++p;

        int n = -2;             // sentinel: -2 = no-op (%% match / %n / unknown)
        switch (conv) {
            case 'd': case 'u': n = ReadInteger(s, 10); break;
            case 'i':           n = ReadInteger(s, 0);  break;
            case 'o':           n = ReadInteger(s, 8);  break;
            case 'x': case 'X': case 'p': n = ReadInteger(s, 16); break;
            case 'e': case 'E': case 'f': case 'g': case 'G':
                                n = ReadFloat(s); break;
            case 's':           n = ReadString(s); break;
            case 'S':           s.flags |= kLong; n = ReadString(s); break;
            case 'c':           n = ReadChars(s); break;
            case 'C':           s.flags |= kLong; n = ReadChars(s); break;
            case '[':           n = ReadCharSet(s, &p); break;
            case 'n':           StoreCount(s, consumed); n = -2; break;
            case '%': {
                int c = s.Get();
                if (c != '%') {
                    if (s.flags & kEof) goto end;
                    s.Unget();
                    goto end;
                }
                ++consumed;
                n = -2;
                break;
            }
            default: n = -2; break;   // unknown conversion: ignore
        }

        if (n == -2)
            continue;           // %%, %n, unknown — no count/assignment update
        if (n <= 0)
            break;              // conversion failed -> stop
        consumed += n;
        if (s.flags & kAssign)
            ++assigned;
        if (s.flags & kEof)
            break;
    }

end:
    if (assigned == 0 && (s.flags & kEof))
        return -1;
    return assigned;
}

} // namespace

int Vsscanf(const char* src, const char* fmt, std::va_list ap) {
    Stream s;
    s.p = src;
    s.width = -1;
    s.flags = 0;
    std::va_list aq;
    va_copy(aq, ap);
    s.ap = &aq;
    int r = FormatCore(s, reinterpret_cast<const unsigned char*>(fmt));
    va_end(aq);
    return r;
}

int Sscanf(const char* src, const char* fmt, ...) {
    std::va_list ap;
    va_start(ap, fmt);
    int r = Vsscanf(src, fmt, ap);
    va_end(ap);
    return r;
}

} // namespace guild::crt
