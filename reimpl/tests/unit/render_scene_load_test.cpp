// Unit tests for the .ed3 scene-file loader (render/scene_load) and the .esc
// script load+run chain (sim/script_run). Synthetic golden buffers exercise the
// recovered scene-file format byte-for-byte and the script preprocessor/runner.
#include "test.h"
#include "render/scene_load.h"
#include "sim/script_run.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::render;

// --- little-endian scene-buffer builder (mirrors the Bio writer side) --------
namespace {
struct Buf {
    std::vector<u8> b;
    void dword(u32 v) { for (int i = 0; i < 4; ++i) b.push_back((u8)(v >> (8 * i))); }
    void f32(float f) { u32 v; std::memcpy(&v, &f, 4); dword(v); }
    void vec3(float x, float y, float z) { f32(x); f32(y); f32(z); }
    void byte(u8 v) { b.push_back(v); }
    void cstr(const char* s) { while (*s) b.push_back((u8)*s++); b.push_back(0); }
};

// Build a minimal version-0xBB scene header: name, ambient, camera, target,
// camFlag, fog, 7 lights (pos + color + 6 keyframes), then objCount.
Buf BuildSceneV(u32 tag, u32 objCount) {
    Buf s;
    s.dword(tag);
    s.cstr("MegaCam");
    s.f32(10.5f);            // ambient
    s.vec3(14, 0, 58);       // camPos
    if (tag >= kVerCamTarget) s.vec3(1, 2, 3);  // camTarget
    if (tag >= kVerFogBlock) {
        if (tag >= kVerCamFlag) s.dword(64);    // camFlag
        s.dword(0x05000000);                    // fogColor
        s.f32(0.0f); s.f32(0.0f);               // fogNear/Far
    }
    if (tag >= kVerLightRig) {
        int cnt = SceneLightCount(tag);
        for (int i = 0; i < cnt; ++i) {
            s.vec3((float)i, (float)(2 * i), (float)(3 * i));   // pos
            if (tag >= kVerFogBlock) {
                if (tag >= kVerCamTarget) s.vec3(0.1f, 0.2f, 0.3f);  // color
                for (int k = 0; k < 6; ++k) { s.dword(k); s.f32(1.0f); s.f32(2.0f); }
            }
        }
    }
    s.dword(objCount);
    return s;
}
} // namespace

TEST(SceneLoad, TagGate) {
    CHECK(SceneTagValid(0x3A6C00BBu));
    CHECK(SceneTagValid(0x3A6C0001u));
    CHECK(!SceneTagValid(0x3A6D00BBu));   // wrong high half
    CHECK(!SceneTagValid(0x3A6C0000u));   // == base, below min
    CHECK(!SceneTagValid(0x00000000u));
}

TEST(SceneLoad, LightCountLadder) {
    CHECK_EQ(SceneLightCount(0x3A6C00BBu), 7);   // >= 0xBA
    CHECK_EQ(SceneLightCount(0x3A6C00BAu), 7);
    CHECK_EQ(SceneLightCount(0x3A6C00B9u), 6);   // >= 0xA5
    CHECK_EQ(SceneLightCount(0x3A6C00A5u), 6);
    CHECK_EQ(SceneLightCount(0x3A6C00A4u), 4);   // else
    CHECK_EQ(SceneLightCount(0x3A6C00A2u), 4);
}

TEST(SceneLoad, ParseHeaderV0xBB) {
    Buf s = BuildSceneV(0x3A6C00BBu, 3);
    SceneReader r(s.b);
    SceneHeader h;
    CHECK(ParseSceneHeader(r, h));
    CHECK_EQ(h.tag, 0x3A6C00BBu);
    CHECK(h.camName == "MegaCam");
    CHECK(h.ambient == 10.5f);
    CHECK(h.camPos.x == 14.0f);
    CHECK(h.camPos.z == 58.0f);
    CHECK(h.hasCamTarget);
    CHECK(h.camTarget.y == 2.0f);
    CHECK_EQ(h.camFlag, 64u);
    CHECK(h.hasFog);
    CHECK_EQ(h.fogColor, 0x05000000u);
    CHECK_EQ((int)h.lights.size(), 7);
    CHECK(h.lights[1].pos.x == 1.0f);
    CHECK(h.lights[1].hasColor);
    CHECK_EQ((int)h.lights[1].keyframes.size(), 6);
    CHECK_EQ(h.lights[3].keyframes[2].id, 2u);
    // After the header the reader is positioned at the objCount dword.
    CHECK_EQ(r.ReadDword(), 3u);
}

TEST(SceneLoad, ParseHeaderOlderVersionNoTargetNoFog) {
    // Version 0xA4: light rig present (4 lights), but no camTarget, no fog block,
    // and no per-light color/keyframes (those need >= 0xB3/0xB5).
    Buf s = BuildSceneV(0x3A6C00A4u, 0);
    SceneReader r(s.b);
    SceneHeader h;
    CHECK(ParseSceneHeader(r, h));
    CHECK(!h.hasCamTarget);
    CHECK(!h.hasFog);
    CHECK_EQ((int)h.lights.size(), 4);
    CHECK(!h.lights[0].hasColor);
    CHECK_EQ((int)h.lights[0].keyframes.size(), 0);
    CHECK_EQ(r.ReadDword(), 0u);   // objCount
}

TEST(SceneLoad, RejectTooOldAndBadMagic) {
    Buf old; old.dword(0x3A6C0005u);  // < 0x3A6C000B "too old"
    SceneReader r1(old.b); SceneHeader h1;
    CHECK(!ParseSceneHeader(r1, h1));

    Buf bad; bad.dword(0xDEADBEEFu);
    SceneReader r2(bad.b); SceneHeader h2;
    CHECK(!ParseSceneHeader(r2, h2));
}

TEST(SceneLoad, ParseSceneHeaderOnlyCountsObjects) {
    Buf s = BuildSceneV(0x3A6C00BBu, 17);
    SceneObjectHooks noHooks;  // null consumeBody -> header + count only
    ParsedScene ps = ParseScene(s.b, noHooks);
    CHECK(ps.headerOk);
    CHECK_EQ(ps.objectCount, 17u);
    CHECK_EQ((int)ps.objects.size(), 0);  // records not walked without a body hook
}

TEST(SceneLoad, ObjectFramingChildSiblingRecursion) {
    // Build a version-0xBB scene with one top-level object that has a child and a
    // sibling. Each object body in this fixture is a single dword the hook eats.
    // Byte order mirrors VIBE_WorldIo_ReadObject: a record is
    //   present, name, field116, field115, kind, kindByte, body,
    //   hasChild [+ child record], hasSibling [+ sibling record].
    Buf s = BuildSceneV(0x3A6C00BBu, 1);
    auto obj = [&](const char* name) {
        s.byte(1); s.cstr(name); s.dword(0xAAAA); s.dword(0xBBBB);
        s.dword(0); s.byte(0); s.dword(0x1234);
    };
    obj("root");
    s.byte(1);                  // root.hasChild
      obj("child");
      s.byte(0);                // child.hasChild
      s.byte(0);                // child.hasSibling
    s.byte(1);                  // root.hasSibling
      obj("sib");
      s.byte(0);                // sib.hasChild
      s.byte(0);                // sib.hasSibling
    s.byte(0);                  // floor flag

    SceneObjectHooks hooks;
    hooks.consumeBody = [](SceneReader& rr, u32 /*ver*/, SceneObjectRecord& rec) -> bool {
        (void)rec;
        rr.ReadDword();   // eat the fixture's single body dword
        return true;
    };
    bool floor = true;
    SceneReader r(s.b);
    SceneHeader h; CHECK(ParseSceneHeader(r, h));
    auto objs = ReadObjectList(r, h.tag, hooks, &floor);
    CHECK_EQ((int)objs.size(), 1);
    CHECK(objs[0].name == "root");
    CHECK_EQ(objs[0].field116, 0xAAAAu);
    CHECK_EQ(objs[0].childCount, 1);    // one child sub-tree
    CHECK_EQ(objs[0].siblingCount, 1);  // one sibling sub-tree
    CHECK(!floor);                      // floor flag was 0
}

// =============================== script_run ================================
using namespace guild::sim;

TEST(ScriptRun, StripCommentsAndWhitespace) {
    // tabs/CR/LF -> spaces; /* */ comment removed; runs of spaces collapse.
    std::string src = "int\tx ;\r\n/* hi there */x   =   5 ;\n";
    std::string out = StripCommentsAndWhitespace(src);
    // No tabs/newlines remain; no double spaces; the comment is gone.
    CHECK(out.find('\t') == std::string::npos);
    CHECK(out.find('\n') == std::string::npos);
    CHECK(out.find('\r') == std::string::npos);
    CHECK(out.find("  ") == std::string::npos);
    CHECK(out.find("hi there") == std::string::npos);
    CHECK(out.find("int x") != std::string::npos);
    CHECK(out.find("x = 5") != std::string::npos);
}

TEST(ScriptRun, StripLineMap) {
    std::vector<int> lines;
    StripCommentsAndWhitespace("a\nb\nc\n", &lines);
    // Three LFs -> three entries + the -1 terminator.
    CHECK_EQ((int)lines.size(), 4);
    CHECK_EQ(lines.back(), -1);
}

TEST(ScriptRun, LoadCompileRunMain) {
    // A .esc with a main() that sets a global and calls a command.
    const char* src =
        "int result ;\n"
        "void main ( void )\n"
        "{\n"
        "    result = 7 ;\n"
        "    Beep ( result ) ;\n"
        "}\n";
    std::vector<std::string> cmds = {"Beep"};
    LoadedScript ls = LoadScriptFromSource("test.esc", src, cmds);
    CHECK(ls.ok);
    CHECK(ls.mainCursor >= 0);
    CHECK_EQ(ls.compiled.symbols.LookupFunction("main"), ls.compiled.symbols.LookupFunction("main"));
    CHECK(ls.compiled.symbols.LookupFunction("main") >= 0);

    std::vector<std::pair<std::string, std::vector<i32>>> emitted;
    ScriptHost host;
    host.invokeCommand = [&](const std::string& name, std::vector<i32>& args) -> i32 {
        emitted.push_back({name, args});
        return 0;
    };
    RunMain(ls, host);
    CHECK_EQ((int)emitted.size(), 1);
    if (!emitted.empty()) {
        CHECK(emitted[0].first == "Beep");
        CHECK_EQ((int)emitted[0].second.size(), 1);
        CHECK_EQ(emitted[0].second[0], 7);
    }
}

TEST(ScriptRun, NoMainEntry) {
    const char* src = "int x ;\nvoid helper ( void ) { x = 1 ; }\n";
    LoadedScript ls = LoadScriptFromSource("nomain.esc", src, {});
    CHECK(!ls.ok);                       // no main -> not runnable
    CHECK(ls.mainCursor < 0);
    ScriptHost host;
    CHECK_EQ(RunMain(ls, host), 0);      // RunMain is a no-op
}

TEST(ScriptRun, ScriptsDisabledShortCircuits) {
    // dword_6315BC set -> LoadAndRun returns false without touching the VFS.
    ScriptHost host;
    bool ran = CutsceneLoadAndRunScript("anything.esc", {}, host,
                                        /*scriptsDisabled=*/true);
    CHECK(!ran);
}
