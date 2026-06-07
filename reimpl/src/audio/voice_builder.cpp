#include "audio/voice_builder.h"

#include <cstdio>

namespace guild::audio {

namespace {
// Format a 0-padded 2-digit decimal exactly like sprintf("%02i", n) for the
// values the original passes (small non-negative variation indices, and the
// signed wrap the original would itself produce for negatives).
std::string Fmt02i(int n) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02i", n);
    return std::string(buf);
}
} // namespace

// gilde.exe 0x581c68 — VIBE_Voice_BuildSampleNameAndPlay (suffix portion)
std::string BuildVoiceSuffix(u32 person, const VoicePerson* personRec, bool& onlyBase) {
    onlyBase = false;

    if (person == static_cast<u32>(-7)) {
        // Special: emit only the base name; no suffix, no variation append.
        onlyBase = true;
        return std::string();
    }

    if (person >= kVoiceSentinelThreshold) {
        // Sentinel voice types (value read as signed int).
        switch (static_cast<int>(person)) {
            case -2: return "_W1"; // aW1 @0x62608c
            case -1: return "_W2"; // aW2 @0x626090
            case -5: return "_M1"; // aM1 @0x626094
            case -4: return "_M2"; // aM2 @0x626098
            case -3: return "_M3"; // aM3 @0x62609c
            case -6: return "_HS"; // aHs @0x6260a0
            case -8: return "";    // unk_6260A4 (empty)
            default: return "";    // unmatched sentinel -> suffix stays empty
        }
    }

    // Person index -> gender byte at +9 decides _W%i (female) vs _M%i (male).
    if (!personRec)
        return "";
    if (personRec->gender == 1) {
        // sprintf("_W%i", (field1 & 1) + 1)  => "_W1" or "_W2"
        return std::string("_W") + std::to_string((personRec->variantSeed & 1) + 1);
    }
    if (personRec->gender == 0) {
        // sprintf("_M%i", field1 % 3 + 1)    => "_M1".."_M3"
        // field1 is read as unsigned (% 3u in the original).
        u32 m = static_cast<u32>(personRec->variantSeed) % 3u + 1;
        return std::string("_M") + std::to_string(m);
    }
    return "";
}

// gilde.exe 0x581c68 — VIBE_Voice_BuildSampleNameAndPlay (name composition)
std::string BuildSampleName(u32 person, const VoicePerson* personRec,
                            int variation, const std::string& base,
                            const SbBank& bank, bool audioReady,
                            bool& useVariationOut) {
    useVariationOut = false;

    bool onlyBase = false;
    std::string suffix = BuildVoiceSuffix(person, personRec, onlyBase);

    if (onlyBase) {
        // -7: sprintf(name, base) — just the base, no suffix or variation.
        return base;
    }

    // First composition: name = base + suffix.
    std::string name = base + suffix;

    // IsMp3 test runs against this first name (VIBE_Audio_SampleIsMp3).
    bool isMp3 = SampleIsMp3(bank, name, audioReady);

    // if (variation >= 0 && !isMp3) suffix += sprintf("%02i", variation).
    if (variation >= 0 && !isMp3) {
        suffix += Fmt02i(variation);
        name = base + suffix; // rebuild "%s%s"
    } else if (variation >= 0 && isMp3) {
        // a3 >= 0 and the name resolves to an mp3 variation group: the original
        // plays it via VIBE_Sound_StartVariation(bank, a3) rather than appending.
        useVariationOut = true;
    }

    return name;
}

// gilde.exe 0x582074 — VIBE_Voice_LoadLanguageBank
// (__usercall, eax = load(a1=file@eax)); sprintf("sprache\\%s", file).
std::string LanguageBankPath(const std::string& file) {
    return "sprache\\" + file;
}

// gilde.exe 0x58209c — VIBE_Voice_LoadWorkerCommentBank (bank-name selection)
WorkerCommentBanks SelectWorkerCommentBanks(u8 type) {
    WorkerCommentBanks b;

    // command/click pair by worker class (the leading switch).
    switch (type) {
        case 4: // thieves
            b.command = "ARBEITER_KOMMENTARE_BEFEHL_DIEBE.sbf";
            b.click   = "ARBEITER_KOMMENTARE_KLICK_DIEBE.sbf";
            break;
        case 16: // robbers
            b.command = "ARBEITER_KOMMENTARE_BEFEHL_RAEUBER.sbf";
            b.click   = "ARBEITER_KOMMENTARE_KLICK_RAEUBER.sbf";
            break;
        case 19: // mercenaries
            b.command = "ARBEITER_KOMMENTARE_BEFEHL_SOELDNER.sbf";
            b.click   = "ARBEITER_KOMMENTARE_KLICK_SOELDNER.sbf";
            break;
        default: // craft / everything else
            b.command = "ARBEITER_KOMMENTARE_BEFEHL_HANDWERK.sbf";
            b.click   = "ARBEITER_KOMMENTARE_KLICK_HANDWERK.sbf";
            break;
    }

    // Noise bank is constant.
    b.noise = "ARBEITER_KOMMENTARE_KLICK_GERAEUSCHE.sbf";

    // Greeting bank by building type (only set for matched types).
    switch (type) {
        case 7:  b.greeting = "BEGRUESSUNG_KIRCHE.sbf"; break;
        case 5:  b.greeting = "BEGRUESSUNG_GELDLEIHE.sbf"; break;
        case 8:
        case 14: b.greeting = "BEGRUESSUNG_KRAEUTERLADEN_PARFUEMERIE.sbf"; break;
        case 9:  b.greeting = "BEGRUESSUNG_LAGERHAUS.sbf"; break;
        case 18:
        case 20:
        case 21: b.greeting = "BEGRUESSUNG_SCHMIEDE_STEIMETZ_TISCHLER.sbf"; break;
        case 11:
        case 12:
        case 13: b.greeting = "BEGRUESSUNG_WALDSTUECK_STEINBRUCH_MINE.sbf"; break;
        case 22: b.greeting = "BEGRUESSUNG_WIRTSHAUS.sbf"; break;
        default: b.greeting.clear(); break;
    }

    return b;
}

} // namespace guild::audio
