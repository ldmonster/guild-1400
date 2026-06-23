// Golden-vector unit tests for character_recon2_cmds — the VIBE_Character
// action-queue builders and script-command dispatchers (gilde.exe).
//
// Self-contained: installs a capturing Recon2Hooks table, drives each command
// body, and asserts the exact control flow, field writes (byte offsets), error
// messages and the YIELD/RESUME re-arm protocol from the decompile.
#include "tests/framework/test.h"
#include "sim/character_recon2_cmds.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild::sim::character_recon2;

namespace {

// ---- A test action-entry backing buffer (opaque type -> raw bytes). --------
constexpr int kEntrySize = 512;
struct EntryBuf {
    alignas(16) unsigned char bytes[kEntrySize];
    EntryBuf() { std::memset(bytes, 0xAB, sizeof(bytes)); }
    ActionEntry* as() { return reinterpret_cast<ActionEntry*>(bytes); }
    guild::i32  dword(int off) const { guild::i32 v; std::memcpy(&v, bytes + off, 4); return v; }
    guild::u8   byte(int off) const { return bytes[off]; }
    void*       ptr(int off) const { void* p; std::memcpy(&p, bytes + off, sizeof(void*)); return p; }
    Handle      handle(int off) const { Handle h; std::memcpy(&h, bytes + off, sizeof(Handle)); return h; }
    const char* str(int off) const { return reinterpret_cast<const char*>(bytes + off); }
};

// ---- Shared capture state for hooks. ---------------------------------------
struct Capture {
    std::vector<std::string> errors;
    EntryBuf entry;
    bool allocate = true;            // queueInsertEntry returns &entry when true
    bool unlinked = false;

    // insertActionArgs capture
    Handle ia_char = 0; void* ia_cb = nullptr; guild::i64 ia_arg = 0; guild::i32 ia_extra = 0;
    bool ia_called = false;

    // createTake/DropObjectAction capture (normal + alt variants)
    Handle ct_char = 0; std::string ct_name; Handle ct_obj = 0; bool ct_called = false;
    Handle cd_char = 0; Handle cd_ecx = 0; std::string cd_name; bool cd_called = false;
    Handle cta_char = 0; std::string cta_name; Handle cta_obj = 0; bool cta_called = false;
    Handle cda_char = 0; Handle cda_param = 0; std::string cda_name; bool cda_called = false;

    // attach / preload / camera capture
    Handle at_char = 0; guild::i32 at_bone = 0; Handle at_item = 0; bool at_called = false;
    Handle ao_char = 0; guild::i32 ao_mode = -99; bool ao_called = false;
    Handle pa_char = 0; guild::i32 pa_kind = 0, pa_a = 0, pa_b = 0, pa_c = 0, pa_d = 0; bool pa_called = false;

    // vararg capture
    guild::i64 vp_packed = 0; guild::i32 vp_angle = 0; bool vp_called = false;

    int objectFindRet = 1;  // VIBE_Object_FindByHandle return
    bool of_called = false;

    // recordField: emulate a tiny object graph via a map of (rec+off)->value.
    // We use a simple linear table.
    struct RF { Handle rec; guild::i32 off; intptr_t val; };
    std::vector<RF> rf;
    void setField(Handle rec, guild::i32 off, intptr_t val) { rf.push_back({rec, off, val}); }
    intptr_t getField(Handle rec, guild::i32 off) const {
        for (auto& e : rf) if (e.rec == rec && e.off == off) return e.val;
        return 0;
    }
};
Capture* g_cap = nullptr;

ActionEntry* h_queueInsert(Handle) { return g_cap->allocate ? g_cap->entry.as() : nullptr; }
void h_unlink(ActionEntry*) { g_cap->unlinked = true; }
void h_report(guild::u8*, guild::u32, const char* msg) { g_cap->errors.emplace_back(msg); }
ActionEntry* h_insertArgs(Handle c, void* cb, guild::i64 arg, guild::i32 extra) {
    g_cap->ia_called = true; g_cap->ia_char = c; g_cap->ia_cb = cb; g_cap->ia_arg = arg; g_cap->ia_extra = extra;
    return g_cap->allocate ? g_cap->entry.as() : nullptr;
}
void h_insertVararg(guild::i64 packed, guild::i32 angle, guild::i32) {
    g_cap->vp_called = true; g_cap->vp_packed = packed; g_cap->vp_angle = angle;
}
void h_createTake(Handle c, const char* n, Handle o) {
    g_cap->ct_called = true; g_cap->ct_char = c; g_cap->ct_name = n ? n : ""; g_cap->ct_obj = o;
}
void h_createDrop(Handle c, Handle ecx, const char* n) {
    g_cap->cd_called = true; g_cap->cd_char = c; g_cap->cd_ecx = ecx; g_cap->cd_name = n ? n : "";
}
void h_createTakeAlt(Handle c, const char* n, Handle o) {
    g_cap->cta_called = true; g_cap->cta_char = c; g_cap->cta_name = n ? n : ""; g_cap->cta_obj = o;
}
void h_createDropAlt(Handle c, Handle p, const char* n) {
    g_cap->cda_called = true; g_cap->cda_char = c; g_cap->cda_param = p; g_cap->cda_name = n ? n : "";
}
int h_objFind(guild::i32, guild::i32, guild::i32, Handle, Handle) { g_cap->of_called = true; return g_cap->objectFindRet; }
void h_attach(Handle c, guild::i32 b, Handle it) { g_cap->at_called = true; g_cap->at_char = c; g_cap->at_bone = b; g_cap->at_item = it; }
void h_applyAttach(Handle c, guild::i32 m, Handle) { g_cap->ao_called = true; g_cap->ao_char = c; g_cap->ao_mode = m; }
void h_preload(Handle c, guild::i32 k, guild::i32 a, guild::i32 b, guild::i32 cc, guild::i32 d) {
    g_cap->pa_called = true; g_cap->pa_char = c; g_cap->pa_kind = k; g_cap->pa_a = a; g_cap->pa_b = b; g_cap->pa_c = cc; g_cap->pa_d = d;
}
intptr_t h_recordField(Handle rec, guild::i32 off) { return g_cap->getField(rec, off); }
int h_strCmp(const char* a, const char* b) { return std::strcmp(a, b); }
std::size_t h_strLen(const char* s) { return s ? std::strlen(s) : 0; }
void h_strNCopyPad(char* d, const char* s, std::size_t max) {
    std::size_t i = 0; for (; s && s[i] && i < max; ++i) d[i] = s[i];
    for (; i <= max; ++i) d[i] = 0;  // pad with NUL through max inclusive
}
float h_angle(Handle, const float*) { return 2.0f; }  // golden angle
void h_pointChain(const void*, const float*, float* out) { out[0] = 1; out[1] = 2; out[2] = 3; }

Recon2Hooks makeHooks() {
    Recon2Hooks h{};
    h.reportError = h_report;
    h.queueInsertEntry = h_queueInsert;
    h.unlinkEntry = h_unlink;
    h.insertActionArgs = h_insertArgs;
    h.insertActionVararg = h_insertVararg;
    h.createTakeObjectAction = h_createTake;
    h.createDropObjectAction = h_createDrop;
    h.createTakeObjectAlt = h_createTakeAlt;
    h.createDropObjectAlt = h_createDropAlt;
    h.objectFindByHandle = h_objFind;
    h.attachItemToBone = h_attach;
    h.applyAttachOffset = h_applyAttach;
    h.preloadAniSet = h_preload;
    h.recordField = h_recordField;
    h.strCmp = h_strCmp;
    h.strLen = h_strLen;
    h.strNCopyPad = h_strNCopyPad;
    h.angleToTargetSigned = h_angle;
    h.pointThroughBoneChain = h_pointChain;
    return h;
}

// RAII scope that installs a fresh capture + hooks and resets engine state.
struct Scope {
    Capture cap;
    Recon2Hooks hooks;
    Scope() {
        g_cap = &cap;
        hooks = makeHooks();
        SetRecon2Hooks(&hooks);
        Recon2Engine() = Recon2EngineState{};
        SetRecon2ExecCmdFn(nullptr);
    }
    ~Scope() { SetRecon2Hooks(nullptr); g_cap = nullptr; }
};

// The action-name copy in the original is a NARROW strcpy unrolled 2 bytes per
// iteration; the name fields therefore hold an ordinary NUL-terminated string.
// `nstr` just yields a mutable narrow buffer for use as a copy source.
std::vector<char> nstr(const char* s) {
    std::vector<char> v(s, s + std::strlen(s) + 1);
    return v;
}

} // namespace

// ===========================================================================
// CmdTakeObject (0x43d260)
// ===========================================================================
TEST(Character2ReconCmds, CmdTakeObject_Success) {
    Scope s;
    Handle ch = 0x1000; char* nm = const_cast<char*>("nm"); Handle obj = 0x2000;
    int r = CmdTakeObject(&ch, &nm, &obj);
    CHECK_EQ(r, 0);
    CHECK(s.cap.ct_called);
    CHECK_EQ(s.cap.ct_char, (Handle)0x1000);
    CHECK_EQ(s.cap.ct_obj, (Handle)0x2000);
}

TEST(Character2ReconCmds, CmdTakeObject_InvalidCharacter) {
    Scope s;
    Handle ch = 0; char* nm = const_cast<char*>("nm"); Handle obj = 1;
    int r = CmdTakeObject(&ch, &nm, &obj);
    CHECK_EQ(r, 1);
    CHECK(!s.cap.ct_called);
    CHECK_EQ(s.cap.errors.size(), (size_t)1);
    CHECK(s.cap.errors[0] == "TakeObject(): Invalid character");
}

TEST(Character2ReconCmds, CmdTakeObject_InvalidDummy) {
    Scope s;
    Handle ch = 1; char* nm = const_cast<char*>("nm"); Handle obj = 0;
    int r = CmdTakeObject(&ch, &nm, &obj);
    CHECK_EQ(r, 1);
    CHECK(s.cap.errors[0] == "TakeObject(): Invalid dummy");
}

TEST(Character2ReconCmds, CmdTakeObject_ResumeGuardYields) {
    Scope s;
    // execCmd set, execCmd->fn == self -> guard fires, returns 0, does not act.
    int execRec = 0;
    Recon2Engine().execCmd = &execRec;
    SetRecon2ExecCmdFn(kFnCmdTakeObject);
    Handle ch = 0x1000; char* nm = const_cast<char*>("nm"); Handle obj = 0x2000;
    int r = CmdTakeObject(&ch, &nm, &obj);
    CHECK_EQ(r, 0);
    CHECK(!s.cap.ct_called);   // yielded, no action created
}

// ===========================================================================
// CmdTakeObjectLeft (0x43d30c) -> CreateTakeObjectActionAlt hook
// ===========================================================================
TEST(Character2ReconCmds, CmdTakeObjectLeft_ForwardsToAlt) {
    Scope s;
    char* nm = const_cast<char*>("dum"); Handle ch = 0x1000; Handle obj = 0x2000;
    int r = CmdTakeObjectLeft(&ch, &nm, &obj);
    CHECK_EQ(r, 0);
    CHECK(s.cap.cta_called);
    CHECK_EQ(s.cap.cta_char, (Handle)0x1000);
    CHECK_EQ(s.cap.cta_obj, (Handle)0x2000);   // CreateTakeObjectActionAlt(char, name, obj)
    CHECK(s.cap.cta_name == "dum");
    CHECK(!s.cap.ct_called);                   // normal take builder NOT used
}

TEST(Character2ReconCmds, CmdTakeObjectLeft_InvalidDummy) {
    Scope s;
    char* nm = const_cast<char*>("dum"); Handle ch = 1; Handle obj = 0;
    int r = CmdTakeObjectLeft(&ch, &nm, &obj);
    CHECK_EQ(r, 1);
    CHECK(s.cap.errors[0] == "TakeObjectLeft(): Invalid dummy");
    CHECK(!s.cap.cta_called);
}

// ===========================================================================
// CmdDropObjectLeft (0x43d600) -> CreateDropObjectActionAlt hook
// ===========================================================================
TEST(Character2ReconCmds, CmdDropObjectLeft_ForwardsToAlt) {
    Scope s;
    char* nm = const_cast<char*>("h"); Handle ch = 0x10; Handle obj = 0x20;
    int r = CmdDropObjectLeft(&ch, &nm, &obj);
    CHECK_EQ(r, 0);
    CHECK(s.cap.of_called);
    CHECK(s.cap.cda_called);
    CHECK_EQ(s.cap.cda_char, (Handle)0x10);
    CHECK_EQ(s.cap.cda_param, (Handle)0x20);   // CreateDropObjectActionAlt(*a1, *a3, *a2)
    CHECK(s.cap.cda_name == "h");
}

// ===========================================================================
// CmdTakeObjectScript (0x43d3b8)
// ===========================================================================
TEST(Character2ReconCmds, CmdTakeObjectScript_PacksArgAndCopies) {
    Scope s;
    Recon2Engine().ownerId = 0x77;
    auto dum = nstr("dummy");
    auto scr = nstr("script.esc");
    auto on  = nstr("oname");
    s.cap.setField(0x2000, 0x1EC, 0x4000);
    s.cap.setField(0x4000, 0x104, reinterpret_cast<intptr_t>(on.data()));
    Handle ch = 0x1000; char* nmp = dum.data(); Handle obj = 0x2000;
    guild::i32 extra = 0x55; guild::u8 flags = 0x12; char* scrp = scr.data();
    int r = CmdTakeObjectScript(&ch, &nmp, &obj, &extra, &flags, &scrp);
    CHECK_EQ(r, 0);
    CHECK(s.cap.ia_called);
    CHECK_EQ(s.cap.ia_cb, kCbPlayAnimationScript);
    CHECK_EQ(s.cap.ia_arg, (guild::i64)0x12 | 0x3100000000LL);
    CHECK_EQ(s.cap.ia_extra, 0x55);
    CHECK(std::strcmp(s.cap.entry.str(240), "dummy") == 0);
    CHECK(std::strcmp(s.cap.entry.str(304), "oname") == 0);
    CHECK(std::strcmp(s.cap.entry.str(144), "script.esc") == 0);
    CHECK_EQ(s.cap.entry.dword(380), 0x77);   // ownerId
}

// ===========================================================================
// CmdDropObject (0x43d548) — validity before resume guard, object-handle gate
// ===========================================================================
TEST(Character2ReconCmds, CmdDropObject_Success) {
    Scope s;
    Handle ch = 0x10; char* nm = const_cast<char*>("h"); Handle obj = 0x20;
    int r = CmdDropObject(&ch, &nm, &obj);
    CHECK_EQ(r, 0);
    CHECK(s.cap.of_called);            // *namePtr nonzero -> FindByHandle called
    CHECK(s.cap.cd_called);
    CHECK_EQ(s.cap.cd_char, (Handle)0x10);
    CHECK_EQ(s.cap.cd_ecx, (Handle)0x20);   // CreateDropObjectAction(char, *objPtr, ...)
}

TEST(Character2ReconCmds, CmdDropObject_FindByHandleFails) {
    Scope s; s.cap.objectFindRet = 0;
    Handle ch = 0x10; char* nm = const_cast<char*>("h"); Handle obj = 0x20;
    int r = CmdDropObject(&ch, &nm, &obj);
    CHECK_EQ(r, 1);
    CHECK(!s.cap.cd_called);
}

TEST(Character2ReconCmds, CmdDropObject_InvalidCharacter) {
    Scope s;
    Handle ch = 0; char* nm = const_cast<char*>("h"); Handle obj = 1;
    int r = CmdDropObject(&ch, &nm, &obj);
    CHECK_EQ(r, 1);
    CHECK(s.cap.errors[0] == "DropObject(): Invalid character");
    CHECK(!s.cap.of_called);  // returns before the handle check
}

TEST(Character2ReconCmds, CmdDropObject_NullNameSkipsHandleCheck) {
    Scope s;
    Handle ch = 0x10; char* nm = nullptr; Handle obj = 0x20;
    int r = CmdDropObject(&ch, &nm, &obj);
    CHECK_EQ(r, 0);
    CHECK(!s.cap.of_called);
    CHECK(s.cap.cd_called);
}

// ===========================================================================
// CmdPlayAnimationScript (0x43d0f8)
// ===========================================================================
TEST(Character2ReconCmds, CmdPlayAniScript_Success) {
    Scope s;
    Recon2Engine().ownerId = 0x99;
    Handle ch = 0x10; guild::u8 flags = 0x07; guild::i32 obj = 0x33;
    std::string scr = "anim/run";   char* scrp = const_cast<char*>(scr.c_str());
    std::string extraNm = "extra";  char* extp = const_cast<char*>(extraNm.c_str());
    guild::i32 finalExtra = 0x44;
    int r = CmdPlayAnimationScript(&ch, &flags, &obj, &scrp, &extp, &finalExtra);
    CHECK_EQ(r, 0);
    CHECK(s.cap.ia_called);
    CHECK_EQ(s.cap.ia_cb, kCbLoadRunAndStoreResult);
    CHECK_EQ(s.cap.ia_arg, (guild::i64)0x07 | 0x2E00000000LL);
    CHECK_EQ(s.cap.ia_extra, 0x33);                 // extra == *objPtr
    CHECK(std::strcmp(s.cap.entry.str(240), "anim/run") == 0);  // strNCopyPad (narrow)
    CHECK(std::strcmp(s.cap.entry.str(144), "extra") == 0);
    CHECK_EQ(s.cap.entry.dword(380), 0x99);
    CHECK_EQ(s.cap.entry.dword(384), 0x44);
}

TEST(Character2ReconCmds, CmdPlayAniScript_NameTooLong) {
    Scope s;
    Handle ch = 0x10; guild::u8 flags = 0; guild::i32 obj = 0;
    std::string longName(0x5F, 'x');  // length 0x5F -> >= 0x5F triggers error
    char* scrp = const_cast<char*>(longName.c_str());
    char* extp = const_cast<char*>("e"); guild::i32 finalExtra = 0;
    int r = CmdPlayAnimationScript(&ch, &flags, &obj, &scrp, &extp, &finalExtra);
    CHECK_EQ(r, 1);
    CHECK(s.cap.errors.size() == 1);
}

TEST(Character2ReconCmds, CmdPlayAniScript_InvalidChar) {
    Scope s;
    Handle ch = 0; guild::u8 flags = 0; guild::i32 obj = 0;
    char* scrp = const_cast<char*>("a"); char* extp = const_cast<char*>("e");
    guild::i32 finalExtra = 0;
    int r = CmdPlayAnimationScript(&ch, &flags, &obj, &scrp, &extp, &finalExtra);
    CHECK_EQ(r, 1);
    CHECK(s.cap.errors[0] == "PlayCharacterAniScriptInt(): invalid character");
}

// ===========================================================================
// CmdSetCharacterCamera (0x43d844)
// ===========================================================================
TEST(Character2ReconCmds, CmdSetCharacterCamera_Modes) {
    const char* names[] = {"CLOSEUP", "LEFT_SHOULDER", "RIGHT_SHOULDER", "EGO"};
    int expect[] = {0, 1, 2, 3};
    for (int i = 0; i < 4; ++i) {
        Scope s;
        Handle ch = 0x10;
        char* np = const_cast<char*>(names[i]);
        int r = CmdSetCharacterCamera(&ch, &np);
        CHECK_EQ(r, 0);
        CHECK(s.cap.ao_called);
        CHECK_EQ(s.cap.ao_mode, expect[i]);
    }
}

TEST(Character2ReconCmds, CmdSetCharacterCamera_UnknownModeNoop) {
    Scope s;
    Handle ch = 0x10;
    char* np = const_cast<char*>("ZOOM");
    int r = CmdSetCharacterCamera(&ch, &np);
    CHECK_EQ(r, 0);
    CHECK(!s.cap.ao_called);
}

TEST(Character2ReconCmds, CmdSetCharacterCamera_InvalidChar) {
    Scope s;
    Handle ch = 0;
    char* np = const_cast<char*>("CLOSEUP");
    int r = CmdSetCharacterCamera(&ch, &np);
    CHECK_EQ(r, 1);
    CHECK(s.cap.errors[0] == "SetCharacterCamera(): Invalid character");
}

// ===========================================================================
// CmdAttachObjectToBone (0x43debc)
// ===========================================================================
TEST(Character2ReconCmds, CmdAttachObjectToBone_Bones) {
    struct { const char* name; int bone; } cases[] = {
        {"d3_LeftHand", 1}, {"d3_RightHand", 2}, {"d3_Head", 3},
    };
    for (auto& c : cases) {
        Scope s;
        Handle ch = 0x10; char* bn = const_cast<char*>(c.name); Handle item = 0x99;
        int r = CmdAttachObjectToBone(&ch, &bn, &item);
        CHECK_EQ(r, 1);
        CHECK(s.cap.at_called);
        CHECK_EQ(s.cap.at_bone, c.bone);
        CHECK_EQ(s.cap.at_item, (Handle)0x99);
    }
}

TEST(Character2ReconCmds, CmdAttachObjectToBone_NullChar) {
    Scope s;
    Handle ch = 0; char* bn = const_cast<char*>("d3_Head"); Handle item = 1;
    int r = CmdAttachObjectToBone(&ch, &bn, &item);
    CHECK_EQ(r, 0);
    CHECK(!s.cap.at_called);
}

// ===========================================================================
// CmdPlayCharacterAni (0x43df30) — invalid path only (transparency is hooked)
// ===========================================================================
TEST(Character2ReconCmds, CmdPlayCharacterAni_InvalidChar) {
    Scope s;
    Handle ch = 0; guild::u8 flag = 1;
    int r = CmdPlayCharacterAni(&ch, &flag);
    CHECK_EQ(r, 1);
    CHECK(s.cap.errors[0] == "PlayCharacterAni(): invalid character or dummy");
}

// ===========================================================================
// PreloadAnimation (0x43d9a4)
// ===========================================================================
TEST(Character2ReconCmds, PreloadAnimation_Success) {
    Scope s;
    Handle ch = 0x10;
    guild::i32 a = 1, c = 3, b = 2, d = 4;  // PreloadAniSet(char, 4, *a, *b, *c, *d)
    int r = PreloadAnimation(&ch, &a, &c, &b, &d);
    CHECK_EQ(r, 0);
    CHECK(s.cap.pa_called);
    CHECK_EQ(s.cap.pa_kind, 4);
    CHECK_EQ(s.cap.pa_a, 1);
    CHECK_EQ(s.cap.pa_b, 2);
    CHECK_EQ(s.cap.pa_c, 3);
    CHECK_EQ(s.cap.pa_d, 4);
}

TEST(Character2ReconCmds, PreloadAnimation_InvalidChar) {
    Scope s;
    Handle ch = 0;
    guild::i32 a = 0, c = 0, b = 0, d = 0;
    int r = PreloadAnimation(&ch, &a, &c, &b, &d);
    CHECK_EQ(r, 1);
    CHECK(s.cap.errors[0] == "PreloadAnimation(): Invalid character");
}

// ===========================================================================
// CmdLookAtObject (0x43dcb0) — angle scaling 180/pi and vararg packing.
// ===========================================================================
TEST(Character2ReconCmds, CmdLookAtObject_PacksVararg) {
    Scope s;
    Handle ch = 0x1000; Handle obj = 0x2000;
    int r = CmdLookAtObject(&ch, &obj);
    CHECK_EQ(r, 0);
    CHECK(s.cap.vp_called);
    // packed = (7<<32) | (u32)char
    CHECK_EQ(s.cap.vp_packed, ((guild::i64)7 << 32) | (guild::u32)0x1000);
    // angle 2.0 * 180 * (1/pi) ~= 114.59 -> truncated to int 114
    CHECK_EQ(s.cap.vp_angle, 114);
}

TEST(Character2ReconCmds, CmdLookAtObject_Invalid) {
    Scope s;
    Handle ch = 0; Handle obj = 0x2000;
    int r = CmdLookAtObject(&ch, &obj);
    CHECK_EQ(r, 1);
    CHECK(s.cap.errors[0] == "LookAtObject(): invalid dest- or targetobject");
}

TEST(Character2ReconCmds, CmdLookAtCharacter_Invalid) {
    Scope s;
    Handle ch = 1; Handle tgt = 0;
    int r = CmdLookAtCharacter(&ch, &tgt);
    CHECK_EQ(r, 1);
    CHECK(s.cap.errors[0] == "LookAtCharacter(): invalid dest- or targetcharacter");
}
