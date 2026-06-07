#include "gui/text/richtext.h"

#include "crt/printf.h"

#include <cstring>

namespace guild::gui::text {

namespace {

// Literal-percent placeholder glyph the original splices for "%%": byte 0x16.
constexpr char kLiteralPercent = 0x16;  // 22

// Read the optional decimal field index that may precede a code letter.
// Returns the digit value (0..9) and advances `p`, or -1 when absent. Mirrors
//   v6 = (u8)str[++v2] - 48;  v209 = (v6 > 9) ? -1 : v6;  if (v6<=9) ++v2;
int ReadFieldDigit(const char*& p) {
    unsigned d = static_cast<unsigned char>(*p) - '0';
    if (d > 9)
        return -1;
    ++p;
    return static_cast<int>(d);
}

// Produce the bare-'i' grouped integer (thousands '.' separators) exactly as the
// inline RenderRichString loop does (delegated to FormatGroupedInt).
std::string ProduceGroupedInt(i32 v) {
    char buf[64];
    FormatGroupedInt(v, buf);
    return std::string(buf);
}

// %a count-icon: "%i%c" with icon 0x14 when a value is present, else just the
// icon. The original passes the int through plain "%i" (NOT grouped) here.
std::string ProduceCount(const Arg& a) {
    char buf[64];
    int n = guild::crt::Sprintf(buf, "%i%c", a.i, kIconCount);
    return std::string(buf, buf + n);
}

// %m money.
std::string ProduceMoney(const Arg& a) {
    char buf[128];
    int n = FormatMoney(a.i, a.divisor, buf);
    return std::string(buf, buf + n);
}

// %T date: "<season> <year>" (PackToRecord + season lookup), via "%s %i".
std::string ProduceDate(const Arg& a, const char* const* seasonNames) {
    DateRecord rec{};
    PackDateRecord(a.date, rec);
    int season = a.date.yearQuarter % 4;  // matches GetSeasonFromYear on packed year
    char buf[128];
    if (seasonNames && seasonNames[season]) {
        guild::crt::Sprintf(buf, "%s %i", seasonNames[season], static_cast<int>(rec.year));
    } else {
        guild::crt::Sprintf(buf, "%i %i", season, static_cast<int>(rec.year));
    }
    return std::string(buf);
}

} // namespace

// Faithful expansion of the self-contained `%`-codes and {rN} tokens. The
// original rewrites a working buffer in place; we build the output string left
// to right (behavior-identical for the codes this module owns, since each code's
// expansion only depends on text to its left being already emitted).
//
// gilde.exe 0x59d6e8 — VIBE_Text_RenderRichString (format-code core).
std::string RenderRichString(const char* fmt,
                             const std::vector<Arg>& args,
                             const TextDb* db,
                             const char* const* seasonNames) {
    std::string out;
    std::size_t argi = 0;
    const char* p = fmt;

    auto nextArg = [&]() -> const Arg* {
        if (argi < args.size())
            return &args[argi++];
        return nullptr;
    };

    while (*p) {
        char c = *p;

        // --- {rN} random text -------------------------------------------------
        if (c == '{') {
            if (p[1] == 'r') {
                // {rN}: N = p[2]-'0' variants. The renderer resolves the base
                // index then advances by RandomModulo(N) (db->PickRandomVariant).
                unsigned n = static_cast<unsigned char>(p[2]) - '0';
                const char* end = std::strchr(p, '}');
                if (n >= 1 && n <= 10 && end) {
                    const Arg* a = nextArg();  // base text-DB id
                    if (a && db) {
                        const char* v = db->PickRandomVariant(a->i, static_cast<int>(n));
                        if (v)
                            out += v;
                    }
                    p = end + 1;
                    continue;
                }
            }
            out += c;
            ++p;
            continue;
        }

        // --- '$' markup is left verbatim for the markup builder ---------------
        if (c == '$') {
            out += c;
            ++p;
            continue;
        }

        // --- '%' value codes --------------------------------------------------
        if (c == '%') {
            const char* q = p + 1;
            int field = ReadFieldDigit(q);  // optional leading digit (v209)
            char code = *q;
            // `field` is the delimited-field index for the $Ns substitution path
            // (VIBE_String_GetDelimitedField); only the field==-1 plain forms are
            // produced here (see deferred list), so it is recorded but not used.
            (void)field;

            switch (code) {
                case '%':  // "%%" -> literal-percent placeholder glyph (0x16)
                    out += kLiteralPercent;
                    p = q + 1;
                    continue;
                case 'i': {  // grouped thousands integer
                    const Arg* a = nextArg();
                    out += ProduceGroupedInt(a ? a->i : 0);
                    p = q + 1;
                    continue;
                }
                case 'a': {  // count icon: "%i%c" (icon 0x14)
                    const Arg* a = nextArg();
                    out += ProduceCount(a ? *a : Arg::MakeInt(0));
                    p = q + 1;
                    continue;
                }
                case 'm': {  // money (coin icon 0x11)
                    const Arg* a = nextArg();
                    out += ProduceMoney(a ? *a : Arg::MakeMoney(0));
                    p = q + 1;
                    continue;
                }
                case 'T': {  // date "<season> <year>"
                    const Arg* a = nextArg();
                    out += ProduceDate(a ? *a : Arg::MakeDate({}), seasonNames);
                    p = q + 1;
                    continue;
                }
                case 's': {  // text-DB / string substitution
                    const Arg* a = nextArg();
                    if (a) {
                        if (a->kind == Arg::Str) {
                            out += a->s;
                        } else if (db) {  // small int treated as DB id
                            const char* v = db->Text(a->i);
                            if (v)
                                out += v;
                        }
                    }
                    p = q + 1;
                    continue;
                }
                default:
                    // Unknown / deferred code: copy the '%' through verbatim so
                    // nothing is silently dropped (markup builder reports
                    // "Unknown textparameter: %%%c" for these).
                    out += c;
                    ++p;
                    continue;
            }
        }

        out += c;
        ++p;
    }
    return out;
}

// gilde.exe 0x59f99c — VIBE_Text_RenderFormattedMessage (id resolve + expand).
std::string RenderFormattedMessage(const TextDb& db,
                                   int msgId,
                                   const std::vector<Arg>& args,
                                   const char* const* seasonNames) {
    const char* fmt = db.Text(msgId);
    if (!fmt)
        return std::string();
    return RenderRichString(fmt, args, &db, seasonNames);
}

} // namespace guild::gui::text
