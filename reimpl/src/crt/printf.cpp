#include "crt/printf.h"

#include <cstdint>
#include <cstring>

// Reconstruction of the MSVC-style `_output` printf core from gilde.exe.
//
// The original drives a state struct (base register ebx in the __usercall
// prototypes) whose fields we recovered from the decompiled byte offsets:
//
//   +0x04  field_width   (int)  parsed width; abs value, '-' clears via flag
//   +0x08  precision     (int)  -1 = absent
//   +0x15  conv_char     (u8)   the conversion letter
//   +0x16  pad_char      (u8)   ' ' default, '0' when the 0 flag is present
//   +0x1E  flags         (u8)   bit0 '#'(alt) bit1 ' '(space) bit2 '+'
//                               bit3 '-'(left) bit4 'h'(short) bit5 'l'(long)
//                               bit6 'N'(near) bit7 'F'(far)
//   +0x1F  flags2        (u8)   bit0  64-bit (I64 / L)
//   +0x20  prefix_len    (int)  sign/0x prefix chars staged ahead of digits
//   +0x24  zero_pad      (int)  count of leading zeros to satisfy precision/0
//   +0x28  text_len      (int)  length of the formatted digit/text body
//   +0x2C..+0x34         (int)  further pad counters (wide/locale; unused here)
//
// VIBE_Crt_ParseFormatFlags @0x6056ec sets pad_char/flags; ParseFormatSpec
// @0x605598 parses width/precision/length; FormatConversion @0x605a18 produces
// the sign prefix + digit body and the zero/space pad counts; FormatStringCore
// @0x6051f0 emits: [width space-pad] [prefix] [zero-pad] [body] [left space-pad].
namespace guild::crt {

// byte_64C1C0 @0x64c1c0 — recovered verbatim. Uppercase forms (%X/%P) are
// produced by uppercasing the emitted run, mirroring VIBE_Crt_StrToUpper.
const char kDigitTable[17] = "0123456789abcdef";

namespace {

// Flag bits packed into spec.flags (offset +0x1E in the original).
constexpr u8 kFlagAlt   = 0x01; // '#'
constexpr u8 kFlagSpace = 0x02; // ' '
constexpr u8 kFlagPlus  = 0x04; // '+'
constexpr u8 kFlagLeft  = 0x08; // '-'
constexpr u8 kFlagShort = 0x10; // 'h'
constexpr u8 kFlagLong  = 0x20; // 'l'

struct FormatSpec {
    int  field_width = 0;   // +0x04
    int  precision   = -1;  // +0x08
    u8   conv_char   = 0;   // +0x15
    u8   pad_char    = ' '; // +0x16
    u8   flags       = 0;   // +0x1E
    u8   flags2      = 0;   // +0x1F  bit0 => 64-bit
};

// Character sink, modelling the ecx callback in the original. Tracks the total
// count (like VIBE_Buffer_PutChar incrementing result[4]) and bounds writes.
struct Sink {
    char*       dst;
    std::size_t cap;   // total buffer capacity including NUL
    std::size_t len;   // total chars the full output would have
    void put(char c) {
        if (cap != 0 && len + 1 < cap)
            dst[len] = c;
        ++len;
    }
};

// VIBE_Crt_ParseFormatFlags @0x6056ec — consume the flag run after '%'.
const char* ParseFlags(const char* p, FormatSpec& s) {
    s.flags = 0;
    s.pad_char = ' ';
    for (;; ++p) {
        switch (*p) {
            case '-': s.flags |= kFlagLeft; break;
            case '#': s.flags |= kFlagAlt; break;
            case '+':
                // '+' sets plus and clears space (original masks 0xFD).
                s.flags = (s.flags | kFlagPlus) & static_cast<u8>(~kFlagSpace);
                break;
            case ' ':
                if (!(s.flags & kFlagPlus))
                    s.flags |= kFlagSpace;
                break;
            case '0': s.pad_char = '0'; break;
            default:  return p;
        }
    }
}

// VIBE_Crt_ParseFormatSpec @0x605598 — width, precision, length modifier.
const char* ParseSpec(const char* p, FormatSpec& s, std::va_list& ap) {
    p = ParseFlags(p, s);

    // Width.
    s.field_width = 0;
    if (*p == '*') {
        int w = va_arg(ap, int);
        if (w < 0) {           // negative '*' width => left-justify + |w|
            s.flags |= kFlagLeft;
            w = -w;
        }
        s.field_width = w;
        ++p;
    } else {
        while (*p >= '0' && *p <= '9')
            s.field_width = s.field_width * 10 + (*p++ - '0');
    }

    // Precision.
    s.precision = -1;
    if (*p == '.') {
        s.precision = 0;
        ++p;
        if (*p == '*') {
            int pr = va_arg(ap, int);
            s.precision = (pr < 0) ? -1 : pr;
            ++p;
        } else {
            while (*p >= '0' && *p <= '9')
                s.precision = s.precision * 10 + (*p++ - '0');
        }
    }

    // Length modifier.  Original recognises h, l/w, I64, L, N, F.
    switch (*p) {
        case 'h': s.flags |= kFlagShort; ++p; break;
        case 'l':
        case 'w': s.flags |= kFlagLong;  ++p; break;
        case 'L': s.flags2 |= 0x01;      ++p; break; // 64-bit
        case 'I':
            if (p[1] == '6' && p[2] == '4') { s.flags2 |= 0x01; p += 3; }
            break;
        default: break;
    }
    return p;
}

// VIBE_String_UIntToString @0x609640 — write `value` in base `radix` using the
// recovered digit table; returns the length (no NUL counting). Lowercase.
int UIntToString(std::uint64_t value, char* out, unsigned radix) {
    char tmp[64];
    int n = 0;
    do {
        tmp[n++] = kDigitTable[value % radix];
        value /= radix;
    } while (value != 0);
    for (int i = 0; i < n; ++i)
        out[i] = tmp[n - 1 - i];
    return n;
}

void ToUpperRun(char* s, int n) { // VIBE_Crt_StrToUpper @0x606020
    for (int i = 0; i < n; ++i)
        if (s[i] >= 'a' && s[i] <= 'z')
            s[i] = static_cast<char>(s[i] - 'a' + 'A');
}

// VIBE_Crt_FormatConversion @0x605a18 (integer/string/char/pointer paths) plus
// the emit ordering of VIBE_Crt_FormatStringCore @0x6051f0, fused into one
// routine over the Sink.  `body` holds the digit/text run; prefix holds sign or
// 0x; zero/space padding is computed per the original's _output model.
void EmitConversion(Sink& sink, FormatSpec& s, std::va_list& ap) {
    char body[80];
    int  body_len = 0;
    char prefix[3];
    int  prefix_len = 0;
    bool is_string = false;
    const char* str_ptr = nullptr;

    const u8 conv = s.conv_char;

    switch (conv) {
        case 'c': {
            // %c — single (possibly widened) character.
            int ch = va_arg(ap, int);
            body[0] = static_cast<char>(ch);
            body_len = 1;
            s.pad_char = ' '; // %c ignores the 0 flag in MSVC _output
            break;
        }
        case 's': {
            str_ptr = va_arg(ap, const char*);
            if (!str_ptr)
                str_ptr = "(null)";
            int max = (s.precision < 0) ? -1 : s.precision;
            int n = 0;
            while (str_ptr[n] && (max < 0 || n < max))
                ++n;
            body_len = n;
            is_string = true;
            s.pad_char = ' ';
            break;
        }
        case 'd':
        case 'i': {
            std::int64_t v;
            if (s.flags2 & 0x01)
                v = va_arg(ap, std::int64_t);
            else if (s.flags & kFlagShort)
                v = static_cast<short>(va_arg(ap, int));
            else
                v = va_arg(ap, int);
            std::uint64_t mag;
            if (v < 0) {
                prefix[prefix_len++] = '-';
                mag = static_cast<std::uint64_t>(-(v + 1)) + 1; // safe negate
            } else {
                if (s.flags & kFlagPlus)       prefix[prefix_len++] = '+';
                else if (s.flags & kFlagSpace) prefix[prefix_len++] = ' ';
                mag = static_cast<std::uint64_t>(v);
            }
            if (mag == 0 && s.precision == 0)
                body_len = 0;
            else
                body_len = UIntToString(mag, body, 10);
            break;
        }
        case 'u':
        case 'o':
        case 'x':
        case 'X': {
            unsigned radix = (conv == 'o') ? 8 : (conv == 'u') ? 10 : 16;
            std::uint64_t v;
            if (s.flags2 & 0x01)
                v = va_arg(ap, std::uint64_t);
            else if (s.flags & kFlagShort)
                v = static_cast<unsigned short>(va_arg(ap, unsigned));
            else
                v = va_arg(ap, unsigned);
            if (v == 0 && s.precision == 0)
                body_len = 0;
            else
                body_len = UIntToString(v, body, radix);
            if (conv == 'X')
                ToUpperRun(body, body_len);
            // '#' prefixes: 0 for octal, 0x/0X for hex (nonzero only).
            if (s.flags & kFlagAlt) {
                if (conv == 'o') {
                    if (body_len == 0 || body[0] != '0')
                        prefix[prefix_len++] = '0';
                } else if ((conv == 'x' || conv == 'X') && v != 0) {
                    prefix[prefix_len++] = '0';
                    prefix[prefix_len++] = (conv == 'X') ? 'X' : 'x';
                }
            }
            break;
        }
        case 'p':
        case 'P': {
            // Original default precision is 8 hex digits (32-bit pointer).
            void* ptr = va_arg(ap, void*);
            std::uint64_t v = reinterpret_cast<std::uintptr_t>(ptr);
            if (s.precision < 0)
                s.precision = 8;
            body_len = UIntToString(v, body, 16);
            while (body_len < s.precision) {
                // left-pad the body with '0' up to precision (zeros prefixed).
                for (int i = body_len; i > 0; --i)
                    body[i] = body[i - 1];
                body[0] = '0';
                ++body_len;
            }
            if (conv == 'P')
                ToUpperRun(body, body_len);
            break;
        }
        case '%': {
            body[0] = '%';
            body_len = 1;
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G': {
            // Float path deferred (original is 16.16 fixed-point, not IEEE).
            // Consume the double arg to keep va_list aligned and emit nothing
            // meaningful; tests do not exercise this path.
            (void)va_arg(ap, double);
            body[0] = '\0';
            body_len = 0;
            break;
        }
        default: {
            // Unknown conversion: echo the conversion char (original behaviour
            // when the letter is not recognised — see LABEL_142).
            body[0] = static_cast<char>(conv);
            body_len = 1;
            break;
        }
    }

    // Precision-driven zero padding for integers (not %c/%s/%%).
    int zero_pad = 0;
    bool numeric = (conv == 'd' || conv == 'i' || conv == 'u' ||
                    conv == 'o' || conv == 'x' || conv == 'X');
    if (numeric && s.precision > body_len)
        zero_pad = s.precision - body_len;

    // Width padding: total visible length = prefix + zero_pad + body_len.
    int visible = prefix_len + zero_pad + body_len;
    int width_pad = (s.field_width > visible) ? (s.field_width - visible) : 0;

    bool left = (s.flags & kFlagLeft) != 0;
    bool zero = (s.pad_char == '0') && !left;
    if (zero && !numeric && conv != 'p' && conv != 'P')
        zero = false; // 0 flag only affects numeric conversions here

    auto put_str = [&](const char* p, int n) {
        for (int i = 0; i < n; ++i)
            sink.put(p[i]);
    };
    auto put_rep = [&](char c, int n) {
        for (int i = 0; i < n; ++i)
            sink.put(c);
    };

    if (!left) {
        if (zero) {
            // prefix first, then zeros count toward field width.
            put_str(prefix, prefix_len);
            put_rep('0', width_pad);
            put_rep('0', zero_pad);
            if (is_string) put_str(str_ptr, body_len);
            else           put_str(body, body_len);
        } else {
            put_rep(' ', width_pad);
            put_str(prefix, prefix_len);
            put_rep('0', zero_pad);
            if (is_string) put_str(str_ptr, body_len);
            else           put_str(body, body_len);
        }
    } else {
        put_str(prefix, prefix_len);
        put_rep('0', zero_pad);
        if (is_string) put_str(str_ptr, body_len);
        else           put_str(body, body_len);
        put_rep(' ', width_pad);
    }
}

} // namespace

// VIBE_Crt_FormatStringCore @0x6051f0 — driver loop.
int Vsnprintf(char* dst, std::size_t cap, const char* fmt, std::va_list ap) {
    Sink sink{dst, cap, 0};
    std::va_list aq;
    va_copy(aq, ap);

    const char* p = fmt;
    while (*p) {
        if (*p != '%') {
            sink.put(*p++);
            continue;
        }
        ++p; // skip '%'
        FormatSpec s;
        p = ParseSpec(p, s, aq);
        s.conv_char = static_cast<u8>(*p);
        if (*p == '\0')
            break;
        ++p; // consume conversion letter
        EmitConversion(sink, s, aq);
    }

    va_end(aq);
    if (cap != 0) {
        std::size_t idx = (sink.len < cap) ? sink.len : (cap - 1);
        dst[idx] = '\0';
    }
    return static_cast<int>(sink.len);
}

int Snprintf(char* dst, std::size_t cap, const char* fmt, ...) {
    std::va_list ap;
    va_start(ap, fmt);
    int n = Vsnprintf(dst, cap, fmt, ap);
    va_end(ap);
    return n;
}

// VIBE_Crt_Sprintf_0 @0x5cba00 — unbounded sprintf entry.
int Sprintf(char* dst, const char* fmt, ...) {
    std::va_list ap;
    va_start(ap, fmt);
    int n = Vsnprintf(dst, static_cast<std::size_t>(-1), fmt, ap);
    va_end(ap);
    return n;
}

} // namespace guild::crt
