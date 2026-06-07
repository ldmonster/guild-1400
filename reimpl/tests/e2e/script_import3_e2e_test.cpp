#include "test.h"
// End-to-end: a host installs the script_import3 hooks, registers ALL three
// command tables (core + sound + object), then drives CmdCreateCharacterAtDummy
// through a fully-wired set of hooks (transform -> create-from-model -> name
// copy -> orient -> place).  Verifies the table is fully populated and the
// command body threads its operands through the host exactly once each.
#include "sim/script_import3.h"
#include "sim/script_import.h"
#include "sim/script_import2.h"
#include "sim/script_vm.h"
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {
// The "character" is a raw byte buffer so the body's byte-exact +5 (name) and
// +52 (sub-object pointer) writes land at the original offsets.
struct Host {
    static Host* self;
    std::vector<float> boneCall;
    int   createCalls = 0;
    int   rotateCalls = 0;
    int   angleCalls = 0;
    int   setTransCalls = 0;
    int   setPosCalls = 0;
    float lastAngle = 0.0f;
    float lastPos[3] = {0,0,0};
    u8    charBuf[256] = {};   // the "character" object
    u8    subObj[256] = {};    // its sub-object (pointer stored at char+52)

    static void Bone(const float*, const float*, float* out) {
        out[0] = 1.0f; out[1] = 2.0f; out[2] = 3.0f;
        self->boneCall.push_back(out[0]);
    }
    static ScriptHandle Create(const char*, const char**) {
        self->createCalls++;
        ScriptHandle h = reinterpret_cast<ScriptHandle>(self->charBuf);
        *reinterpret_cast<ScriptHandle*>(self->charBuf + 52) =
            reinterpret_cast<ScriptHandle>(self->subObj);
        return h;
    }
    static void Rotate(ScriptHandle, const float*, float* out) {
        self->rotateCalls++;
        out[0] = 0.0f; out[1] = 0.0f; out[2] = 1.0f;
    }
    static float Angle(const float*, const float*) {
        self->angleCalls++;
        self->lastAngle = 0.7853982f;   // pi/4 sentinel
        return self->lastAngle;
    }
    static void SetTrans(ScriptHandle, const float* v) {
        self->setTransCalls++;
        self->lastAngle = v[1];   // orient vec's y == yaw
    }
    static void SetPos(ScriptHandle, const float* v) {
        self->setPosCalls++;
        self->lastPos[0] = v[0]; self->lastPos[1] = v[1]; self->lastPos[2] = v[2];
    }
};
Host* Host::self = nullptr;

std::string ReadName(const u8* p) {
    return std::string(reinterpret_cast<const char*>(p));
}
} // namespace

// Full registration flow: all three tables coexist in one command table without
// collision, and the SND_* event tokens accumulate.
TEST(ScriptImport3E2E, RegisterAllTables) {
    ResetCommands();
    ResetEventTokens();
    CHECK_EQ(RegisterCommands(), 1);
    CHECK_EQ(RegisterSoundCommands(), 1);
    CHECK_EQ(RegisterObjectCommands(), 1);

    // Names from all three groups resolve via the real lookup.
    CHECK(FindCommandByName(Commands().bytes, "Sleep") != nullptr);
    CHECK(FindCommandByName(Commands().bytes, "PlaySample3D") != nullptr);
    CHECK(FindCommandByName(Commands().bytes, "CreateEmitter") != nullptr);
    CHECK(FindCommandByName(Commands().bytes, "SelectAllTextures") != nullptr);
    // 8 SND_* tokens from the sound group.
    CHECK_EQ(EventTokens().count, 8u);
    ResetCommands();
    ResetEventTokens();
}

// CmdCreateCharacterAtDummy drives a fully-wired host: bone-chain -> create ->
// name copy -> orient -> place, threading each operand exactly once.
TEST(ScriptImport3E2E, CreateCharacterAtDummyFlow) {
    Host host;
    Host::self = &host;
    ScriptImport3Hooks h{};
    h.pointThroughBoneChain = Host::Bone;
    h.createFromModel       = Host::Create;
    h.rotateByHierarchy     = Host::Rotate;
    h.angleBetween          = Host::Angle;
    h.setWorldTranslation   = Host::SetTrans;
    h.setPosition           = Host::SetPos;
    SetScriptImport3Hooks(&h);
    SetScriptCmdHooks(nullptr);   // ReportError not expected on the happy path

    // A nonzero "dummy" object pointer + a name to copy in.
    static u8 dummyObj[256] = {};
    ScriptHandle dummy = reinterpret_cast<ScriptHandle>(dummyObj);
    char nameBuf[] = "Bob";
    char* name = nameBuf;

    ScriptHandle result = CmdCreateCharacterAtDummy(&dummy, &name);
    CHECK_EQ(result, reinterpret_cast<ScriptHandle>(host.charBuf));

    // Each leaf called exactly once.
    CHECK_EQ(host.createCalls, 1);
    CHECK_EQ(host.rotateCalls, 1);
    CHECK_EQ(host.angleCalls, 1);
    CHECK_EQ(host.setTransCalls, 1);
    CHECK_EQ(host.setPosCalls, 1);
    CHECK_EQ(static_cast<int>(host.boneCall.size()), 1);

    // Name copied into the character (+5) and the sub-object (+0).
    CHECK_EQ(ReadName(host.charBuf + 5), std::string("Bob"));
    CHECK_EQ(ReadName(host.subObj), std::string("Bob"));

    // The yaw flowed orient.y -> SetWorldTranslation; the world position flowed
    // bone-chain -> SetPosition.
    CHECK(host.lastAngle == 0.7853982f);
    CHECK(host.lastPos[0] == 1.0f);
    CHECK(host.lastPos[1] == 2.0f);
    CHECK(host.lastPos[2] == 3.0f);

    SetScriptImport3Hooks(nullptr);
}
