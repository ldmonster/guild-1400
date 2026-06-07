// End-to-end: mount a gfx catalogue + a .form-style screen + a localized text .res
// in a mock VFS, load them BY NAME through the real VFS dispatch, build a form
// (form -> windows -> objects via markup), and verify the tree + resolved text vs a
// reference.
//
// Exercises gui/form_asset (VIBE_Gui_LoadGfxFile open-by-name) + gui/form_loader
// (.gfx parse + table init) + gui/text_load (VIBE_Text_LoadTextFile/.res parse) +
// gui/markup_build + the widget-create leaves + the real guild::io VFS on top of a
// mock IFileSystem.
#include "tests/framework/test.h"

#include "gui/form_asset.h"
#include "gui/form_loader.h"
#include "gui/text_load.h"
#include "gui/text/textdb.h"
#include "gui/markup_build.h"
#include "gui/widget_create.h"
#include "gui/window.h"
#include "gui/object.h"
#include "gui/form.h"

#include "io/vfs.h"
#include "shim/IFileSystem.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::gui;
using guild::gui::text::TextDb;

// ---- Renderer/property edge stubs (shared across this TU) ---------------------------
namespace guild::gui {
i16 Property_Get(const char* text, int) { return static_cast<i16>(text ? std::strlen(text) : 0); }
int Property_Validate(const char* name) {
    return (name && std::strcmp(name, "_BUTTON_RED") == 0) ? 7 : 0;
}
void RegisterGfxState(int) {}
void Widget_LayoutBounds(int, int, int) {}
void Widget_RefreshText(int) {}
} // namespace guild::gui

// ---- Mock IFileSystem + in-memory files (local to avoid symbol clashes) -------------
namespace {

class MemFile : public guild::shim::IFile {
public:
    explicit MemFile(const std::vector<u8>* d) : data_(d) {}
    std::size_t read(void* dst, std::size_t n) override {
        std::size_t avail = data_->size() - pos_;
        if (n > avail) n = avail;
        if (n) std::memcpy(dst, data_->data() + pos_, n);
        pos_ += n; return n;
    }
    std::size_t write(const void*, std::size_t) override { return 0; }
    std::int64_t seek(std::int64_t off, int whence) override {
        std::int64_t base = whence == 1 ? (std::int64_t)pos_
                          : whence == 2 ? (std::int64_t)data_->size() : 0;
        std::int64_t t = base + off;
        if (t < 0 || (std::size_t)t > data_->size()) return -1;
        pos_ = (std::size_t)t; return pos_;
    }
    std::int64_t tell() override { return (std::int64_t)pos_; }
    std::int64_t size() override { return (std::int64_t)data_->size(); }
private:
    const std::vector<u8>* data_;
    std::size_t pos_ = 0;
};

class MockFs : public guild::shim::IFileSystem {
public:
    void add(const std::string& p, const std::vector<u8>& d) { files_[p] = d; }
    guild::shim::IFile* open(const char* path, const char*) override {
        auto it = files_.find(path);
        return it == files_.end() ? nullptr : new MemFile(&it->second);
    }
    void close(guild::shim::IFile* f) override { delete f; }
    bool exists(const char* path) override { return files_.count(path) != 0; }
private:
    std::map<std::string, std::vector<u8>> files_;
};

// ---- .gfx catalogue bytes: u32 count + count*84-byte records ----
std::array<u8, 84> GfxRec(i32 flags68, i32 scene56) {
    std::array<u8, 84> r{}; r.fill(0);
    r[68] = static_cast<u8>(flags68);
    std::memcpy(&r[56], &scene56, 4);
    return r;
}
std::vector<u8> GfxBytes(const std::vector<std::array<u8, 84>>& recs) {
    std::vector<u8> b;
    u32 n = static_cast<u32>(recs.size());
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<u8>((n >> (8 * i)) & 0xFF));
    for (const auto& r : recs) b.insert(b.end(), r.begin(), r.end());
    return b;
}

// ---- .res text bytes (the recovered localized-text format) ----
struct ResEntry { std::string text; std::string name; u8 tag; };
void PushU32(std::vector<u8>& b, u32 v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<u8>((v >> (8 * i)) & 0xFF));
}
std::vector<u8> ResBytes(u32 baseIndex, const std::vector<ResEntry>& es) {
    std::vector<u8> b;
    u32 count = static_cast<u32>(es.size());
    PushU32(b, count);
    PushU32(b, baseIndex);
    PushU32(b, baseIndex + count - 1);
    std::vector<u8> blob; std::vector<u32> offs;
    for (const auto& e : es) {
        offs.push_back(static_cast<u32>(blob.size()));
        blob.insert(blob.end(), e.text.begin(), e.text.end());
        blob.push_back(0);
    }
    for (u32 o : offs) PushU32(b, o);
    for (const auto& e : es) {
        u8 f[80] = {0};
        std::memcpy(f, e.name.data(), e.name.size() < 80 ? e.name.size() : 80);
        b.insert(b.end(), f, f + 80);
    }
    for (const auto& e : es) b.push_back(e.tag);
    PushU32(b, static_cast<u32>(blob.size()));
    b.insert(b.end(), blob.begin(), blob.end());
    return b;
}

} // namespace

TEST(GuiFormAssetE2E, MountLoadByNameBuildFormResolveText) {
    // ---- 1. Mount the catalogue + text resource in a mock VFS ----
    MockFs fs;
    // .gfx catalogue: opened as "gfx\\gilde.gfx".
    fs.add("gfx\\gilde.gfx", GfxBytes({
        GfxRec(0x00, 0),     // obj 0
        GfxRec(0x01, 1234),  // obj 1: drawable -> scene-state
        GfxRec(0x00, 99),    // obj 2
    }));
    // localized text: opened as "textbin_german\\main.res".
    fs.add("textbin_german\\main.res", ResBytes(/*base=*/0, {
        { "OK",      "BTN_OK",     0xFF },
        { "Cancel",  "BTN_CANCEL", 0xFF },
        { "Welcome", "MSG_HELLO",  0xFF },
    }));

    guild::io::VfsInit(&fs, /*caseInsensitive=*/false);

    // Clean GUI state.
    ResetGuiState();
    ResetWidgetCreate();
    ResetGfxObjects();
    ResetMarkupBuild();

    // ---- 2. Load the gfx catalogue BY NAME (VIBE_Gui_LoadGfxFile open path) ----
    bool gfxOk = Gui_LoadGfxFile("gilde.gfx");
    CHECK(gfxOk);
    CHECK_EQ(g_gfxObjectCount, 3);
    CHECK_EQ(g_gfxObjects[1].sceneHandle(), 1234);
    // Table baseline init ran (slot stamped with its own index).
    CHECK_EQ(g_forms[2].dw[0], 2);
    CHECK_EQ(g_windows[7].at<i32>(0), 7);

    // The "Form_LoadByName" alias resolves the same path.
    CHECK(Form_LoadByName("gilde.gfx"));

    // ---- 3. Load the localized text DB BY NAME (VIBE_Text_LoadTextFile) ----
    TextDb db;
    bool txtOk = guild::gui::text::Text_LoadTextFile("main", db);
    CHECK(txtOk);
    CHECK_EQ(db.Count(), 3);

    // ---- 4. Build a "form": one window with two markup red buttons + an embedded
    //         object referencing the drawable gfx id 1234. ----
    int win = Window_Create(40, 40, 260, 140, 0);
    CHECK(win >= 0);
    // Use the localized strings as the button labels (resolved by name).
    std::string okLabel = "$ia[" + std::string(db.Text(db.FindIndex("BTN_OK"))) + "]";
    std::string caLabel = "$ia[" + std::string(db.Text(db.FindIndex("BTN_CANCEL"))) + "]";
    auto objs = BuildMarkupIntoWindow(win, (okLabel + caLabel).c_str());
    CHECK_EQ(static_cast<int>(objs.size()), 2);

    std::vector<int> pending = { 1234 };
    auto emb = BuildMarkupIntoWindow(win, "$ic", pending);
    CHECK_EQ(static_cast<int>(emb.size()), 1);

    // Wire the window into form id 3 (window-id table + count).
    int formId = 3;
    g_forms[formId].windowCount() = 1;
    g_forms[formId].windowId(0) = win;

    // ---- 5. Verify the form -> window -> object tree vs the reference ----
    g_currentFormId = formId;
    CHECK(Form_SelectWindow(formId, 0) == 1);
    CHECK_EQ(g_currentWindowId, win);

    // The window owns three children: two sprite buttons + one embedded clickable.
    CHECK_EQ(static_cast<int>(g_windows[win].objCount()), 3);
    i32* list = WindowChildList(win);
    CHECK_EQ(g_widgets[list[0]].type(), static_cast<u8>(kTypeSprite));
    CHECK_EQ(g_widgets[list[1]].type(), static_cast<u8>(kTypeSprite));
    CHECK(emb[0].kind == MarkupObjectKind::Embedded);
    CHECK_EQ(emb[0].objId, 1234);
    CHECK_EQ(g_widgets[list[2]].at<i32>(68), 1); // 'c' selector -> clickable

    // Accessor resolves through the form: group 0, local object 2 == the embed.
    CHECK_EQ(Form_GetChildObjectId(formId, 0, 2), list[2]);

    // ---- 6. Resolved text vs reference ----
    CHECK(std::strcmp(db.Text(db.FindIndex("MSG_HELLO")), "Welcome") == 0);
    CHECK(std::strcmp(db.Text(0), "OK") == 0);
    CHECK_EQ(db.FindIndex("btn_cancel"), 1); // case-insensitive

    guild::io::VfsShutdown();
}
