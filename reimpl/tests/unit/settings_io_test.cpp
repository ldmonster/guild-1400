// Unit tests for play::settings_io — LoadSettings / SaveSettings and the
// WritePrivateProfileStringA file-merge core (IniWriteProfileString).
//
// The merge semantics under test are the Win32 WritePrivateProfileStringA
// contract the original leans on (gilde.exe 0x56af54 VIBE_Config_WriteGfxSettings
// calls it once per key against Gilde.INI): case-insensitive section/key match,
// in-place single-line value update preserving the file's key spelling and
// every other line, end-of-section insert for new keys, end-of-file append for
// new sections.
#include "test.h"
#include "play/settings_io.h"
#include "app/config_write.h"
#include "config/ini.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace guild;

namespace {
shim::MemFileSystem::Blob ToBlob(const std::string& s) {
    return shim::MemFileSystem::Blob(s.begin(), s.end());
}
std::string FromBlob(const shim::MemFileSystem::Blob& b) {
    return std::string(b.begin(), b.end());
}
std::string GameDir() {
    if (const char* e = std::getenv("GUILD_GAME_DIR")) return e;
    return "europe_guild_1400_original";
}
} // namespace

// ---------------------------------------------------------------------------
// IniWriteProfileString — the WritePrivateProfileStringA merge core.
// ---------------------------------------------------------------------------

TEST(SettingsIo, MergeReplacesValueInPlacePreservingEverythingElse) {
    const std::string in =
        "[General]\r\nBildmodus=FULLSCREEN\r\n\r\n"
        "[Sound]\r\n; comment line\r\nmaster_vol=127\r\nsfx_vol=114\r\n\r\n"
        "[Network]\r\nPort=7531\r\n";
    const std::string out = play::IniWriteProfileString(in, "Sound", "master_vol", "64");
    CHECK(out.find("master_vol=64\r\n") != std::string::npos);
    CHECK(out.find("master_vol=127") == std::string::npos);
    // Every other line byte-identical (replace ONLY the matched line).
    CHECK(out.find("[General]\r\nBildmodus=FULLSCREEN\r\n\r\n") != std::string::npos);
    CHECK(out.find("; comment line\r\n") != std::string::npos);
    CHECK(out.find("sfx_vol=114\r\n") != std::string::npos);
    CHECK(out.find("[Network]\r\nPort=7531\r\n") != std::string::npos);
}

TEST(SettingsIo, MergeMatchesSectionAndKeyCaseInsensitively) {
    const std::string in = "[SOUND]\r\nMaster_Vol=10\r\n";
    const std::string out = play::IniWriteProfileString(in, "Sound", "master_vol", "20");
    // The FILE's key spelling is preserved (Win32 keeps the stored name and
    // replaces the value).
    CHECK(out.find("Master_Vol=20\r\n") != std::string::npos);
    CHECK(out.find("master_vol=20") == std::string::npos);
    CHECK(out.find("[SOUND]") != std::string::npos);   // header untouched
}

TEST(SettingsIo, MergeInsertsMissingKeyAtEndOfSection) {
    const std::string in =
        "[Sound]\r\nmaster_vol=127\r\n\r\n[Network]\r\nPort=7531\r\n";
    const std::string out = play::IniWriteProfileString(in, "Sound", "msx_freq", "3");
    // Inserted after the section's last non-blank line, before the blank
    // separator / next header.
    CHECK(out.find("master_vol=127\r\nmsx_freq=3\r\n\r\n[Network]") != std::string::npos);
}

TEST(SettingsIo, MergeAppendsMissingSectionAtEndOfFile) {
    const std::string in = "[General]\r\nBildmodus=FULLSCREEN\r\n";
    const std::string out = play::IniWriteProfileString(in, "Game", "speed", "0");
    CHECK(out.find("[General]\r\nBildmodus=FULLSCREEN\r\n") == 0);
    CHECK(out.find("[Game]\r\nspeed=0\r\n") != std::string::npos);
    CHECK(out.rfind("[Game]") > out.find("[General]"));
}

TEST(SettingsIo, MergeCreatesFileTextFromScratchWithCrlf) {
    // Win32 writes CRLF; a new (empty) file gets CRLF lines.
    const std::string out = play::IniWriteProfileString("", "Gfx", "details", "2");
    CHECK_EQ(out, std::string("[Gfx]\r\ndetails=2\r\n"));
}

TEST(SettingsIo, MergeKeepsLfConventionOfLfOnlyFiles) {
    const std::string in = "[Sound]\nmaster_vol=1\n";
    const std::string upd = play::IniWriteProfileString(in, "Sound", "master_vol", "2");
    CHECK_EQ(upd, std::string("[Sound]\nmaster_vol=2\n"));
    const std::string ins = play::IniWriteProfileString(in, "Sound", "sfx_vol", "3");
    CHECK_EQ(ins, std::string("[Sound]\nmaster_vol=1\nsfx_vol=3\n"));
}

TEST(SettingsIo, MergeIgnoresCommentLinesAsKeys) {
    // ';master_vol=9' is a comment, not the key — the real key is updated and
    // the comment survives verbatim.
    const std::string in = "[Sound]\r\n;master_vol=9\r\nmaster_vol=1\r\n";
    const std::string out = play::IniWriteProfileString(in, "Sound", "master_vol", "5");
    CHECK(out.find(";master_vol=9\r\n") != std::string::npos);
    CHECK(out.find("\r\nmaster_vol=5\r\n") != std::string::npos);
}

// ---------------------------------------------------------------------------
// LoadSettings / SaveSettings over the shim filesystem.
// ---------------------------------------------------------------------------

TEST(SettingsIo, LoadSettingsReadsThroughRealReader) {
    shim::MemFileSystem fs;
    fs.put("Gilde.INI", ToBlob(
        "[Gfx]\r\ndetails=2\r\ncur_res=1\r\n"
        "[Sound]\r\nmaster_vol=100\r\nmsx_freq=3\r\n"
        "[Game]\r\nspeed=42\r\nstadt=Berlin\r\n"));
    play::SettingsBundle s;
    CHECK(play::LoadSettings(fs, "Gilde.INI", s));
    CHECK((int)s.gfx.details == 2);
    CHECK((int)s.gfx.curRes == 1);
    CHECK(s.gfx.resWidth == 768 && s.gfx.resHeight == 1024);  // derived (0x63D70C table)
    CHECK((int)s.sound.masterVol == 100);
    CHECK((int)s.sound.msxFreq == 3);
    CHECK(s.game.speed == 42);
    CHECK(s.game.stadt == "Berlin");
    CHECK((int)s.game.panelMode == 1);   // absent -> reader default
}

TEST(SettingsIo, LoadSettingsMissingFileYieldsReaderDefaults) {
    shim::MemFileSystem fs;
    play::SettingsBundle s;
    CHECK(!play::LoadSettings(fs, "Gilde.INI", s));
    CHECK((int)s.gfx.characterDetail == 1);   // def 1 (0x56b834)
    CHECK((int)s.game.nachtwaechter == 1);    // def 1
    CHECK(s.game.stadt == "Augsburg");        // def "Augsburg"
}

TEST(SettingsIo, SaveSettingsCreatesFileWithSerializerKeyOrder) {
    shim::MemFileSystem fs;
    play::SettingsBundle s;
    s.sound.masterVol = 64;
    CHECK(play::SaveSettings(fs, "Gilde.INI", s));
    const std::string text = FromBlob(fs.get("Gilde.INI"));
    // The real serializer's first key is texture_scale (0x56af54 order).
    CHECK(text.find("[Gfx]\r\ntexture_scale=0\r\ndetails=0\r\n") == 0);
    CHECK(text.find("master_vol=64") != std::string::npos);
    CHECK(text.find("[Sound]") != std::string::npos);
    CHECK(text.find("[Game]") != std::string::npos);
    CHECK(text.find("panel_help=") != std::string::npos);   // last serializer key
}

TEST(SettingsIo, SaveLoadRoundTripsEveryWriterOwnedField) {
    shim::MemFileSystem fs;
    play::SettingsBundle a;
    a.gfx.textureScale = 1; a.gfx.details = 2; a.gfx.lodHandling = 1;
    a.gfx.shadowDetail = 2; a.gfx.floorMipmapping = 1; a.gfx.gfxSet = 1;
    a.gfx.cameraLimits = 2; a.gfx.floorLod = 1; a.gfx.characterDetail = 2;
    a.gfx.fogPlane = 25; a.gfx.curRes = 2;
    a.gfx.brightness[3] = 0.5f; a.gfx.contrast[0] = 0.75f; a.gfx.gamma[2] = 1.25f;
    a.sound.masterVol = 127; a.sound.sfxVol = 114; a.sound.msxVol = 50;
    a.sound.speechVol = 127; a.sound.msxFreq = 3;
    a.game.speed = 160; a.game.mouseSpeed = 452; a.game.scrollSpeed = 75;
    a.game.cameraSpeed = 75; a.game.nachtwaechter = 0; a.game.stadt = "Wien";
    a.game.historie = 2; a.game.mission = -1; a.game.netMission = -1;
    a.game.showCursorTxt = 0; a.game.showGebInfo = 1; a.game.panelMode = 4;
    a.game.helpEvents = 0; a.game.difficulty = 2; a.game.hints = 0; a.game.panelHelp = 1;
    CHECK(play::SaveSettings(fs, "Gilde.INI", a));

    play::SettingsBundle b;
    CHECK(play::LoadSettings(fs, "Gilde.INI", b));
    CHECK((int)b.gfx.textureScale == 1);  CHECK((int)b.gfx.details == 2);
    CHECK((int)b.gfx.lodHandling == 1);   CHECK((int)b.gfx.shadowDetail == 2);
    CHECK((int)b.gfx.floorMipmapping == 1); CHECK((int)b.gfx.gfxSet == 1);
    CHECK((int)b.gfx.cameraLimits == 2);  CHECK((int)b.gfx.floorLod == 1);
    CHECK((int)b.gfx.characterDetail == 2); CHECK((int)b.gfx.fogPlane == 25);
    CHECK((int)b.gfx.curRes == 2);
    // float fields: *100 truncate on write, *0.01 on read — bit-stable here.
    CHECK(b.gfx.brightness[3] == 0.5f);
    CHECK(b.gfx.contrast[0] == 0.75f);
    CHECK(b.gfx.gamma[2] == 1.25f);
    CHECK((int)b.sound.masterVol == 127); CHECK((int)b.sound.sfxVol == 114);
    CHECK((int)b.sound.msxVol == 50);     CHECK((int)b.sound.speechVol == 127);
    CHECK((int)b.sound.msxFreq == 3);
    CHECK(b.game.speed == 160);           CHECK(b.game.mouseSpeed == 452);
    CHECK(b.game.scrollSpeed == 75);      CHECK((int)b.game.cameraSpeed == 75);
    CHECK((int)b.game.invertMouse == 0);  // reader forces 0 (0x56b834)
    CHECK((int)b.game.nachtwaechter == 0);
    CHECK(b.game.stadt == "Wien");
    CHECK(b.game.historie == 2); CHECK(b.game.mission == -1); CHECK(b.game.netMission == -1);
    CHECK((int)b.game.showCursorTxt == 0); CHECK((int)b.game.showGebInfo == 1);
    CHECK((int)b.game.panelMode == 4);     CHECK((int)b.game.helpEvents == 0);
    CHECK((int)b.game.difficulty == 2);    CHECK((int)b.game.hints == 0);
    CHECK((int)b.game.panelHelp == 1);
}

TEST(SettingsIo, RealGildeIniRoundTripPreservesForeignKeys) {
    // GUARDED on the real game dir: load the shipped Gilde.INI, mutate one
    // setting via SaveSettings into a COPY (MemFileSystem), reload, and assert
    // (a) the mutation, (b) WritePrivateProfileStringA merge semantics kept
    // every line the serializer does not own.
    namespace fsx = std::filesystem;
    std::error_code ec;
    const fsx::path ini = fsx::path(GameDir()) / "Gilde.INI";
    if (!fsx::exists(ini, ec)) {
        std::printf("[ SKIP ] real Gilde.INI not found (%s); set GUILD_GAME_DIR.\n",
                    ini.string().c_str());
        CHECK(true); return;
    }

    // Copy the real file into the in-memory fs (the original is never written).
    shim::DiskFileSystem disk(GameDir());
    shim::IFile* f = disk.open("Gilde.INI", "rb");
    CHECK(f != nullptr);
    std::string original(static_cast<std::size_t>(f->size()), '\0');
    CHECK(f->read(&original[0], original.size()) == original.size());
    disk.close(f);
    shim::MemFileSystem mem;
    mem.put("Gilde.INI", ToBlob(original));

    // Load through the real reader; mutate ONE setting; save through the
    // real serializer.
    play::SettingsBundle s;
    CHECK(play::LoadSettings(mem, "Gilde.INI", s));
    const int before = s.sound.masterVol;
    s.sound.masterVol = (u8)((before + 16) & 0x7F);
    CHECK(play::SaveSettings(mem, "Gilde.INI", s));

    // (a) the mutation persisted and reloads through the real reader.
    play::SettingsBundle r;
    CHECK(play::LoadSettings(mem, "Gilde.INI", r));
    CHECK((int)r.sound.masterVol == (int)s.sound.masterVol);
    CHECK((int)r.sound.masterVol != before);

    // (b) merge semantics: every line the serializer does NOT own survives
    // byte-for-byte. The shipped INI has plenty of foreign material:
    const std::string text = FromBlob(mem.get("Gilde.INI"));
    const char* foreign[] = {
        "Bildmodus=FULLSCREEN",            // [General] — not serializer-owned
        "show_intro=0",
        "fog=0",                           // [Gfx] keys the writer never writes
        "screen_x=800", "screen_y=600",
        "msx=1", "weather=1", "ambient=1", // [Sound] foreign keys
        "; Localhost",                     // [Network] comments
        "; Neo Tokyo",
        "Port=7531",
        "[Compat]", "SingleCore=1",        // whole foreign section
    };
    for (const char* needle : foreign) {
        if (original.find(needle) != std::string::npos)
            CHECK(text.find(needle) != std::string::npos);
    }
    // And the untouched writer-owned settings still hold their on-disk values.
    config::IniFile reparsed(text);
    config::IniFile orig(original);
    const char* keys[][2] = {
        {"Gfx", "details"}, {"Gfx", "fog_plane"}, {"Gfx", "character_detail"},
        {"Sound", "sfx_vol"}, {"Sound", "msx_freq"},
        {"Game", "mouse_speed"}, {"Game", "difficulty"}, {"Game", "historie"},
    };
    for (auto& k : keys)
        CHECK_EQ(reparsed.getInt(k[0], k[1], -999), orig.getInt(k[0], k[1], -999));
    std::printf("[settings-io] real Gilde.INI round-trip: master_vol %d -> %d, "
                "%zu foreign lines checked\n", before, (int)r.sound.masterVol,
                sizeof(foreign) / sizeof(foreign[0]));
}
