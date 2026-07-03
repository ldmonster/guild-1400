// =============================================================================
// guild::play — SESSION PANELS implementation.  See session_panels.h for the
// reconstruction inventory and the named gaps.
//
// Every pixel written here goes through a reconstructed leaf (the SessionHud
// set): gui::MenuFillRect -> render::SurfaceDrawHLine (0x423c70 -> 0x423ffc),
// render::SurfaceDrawRectOutline (0x4242d4), render::DrawText -> DrawGlyph
// (0x434E18 -> 0x434D0C), and the HudRenderHooks::drawSprite bridge to the
// REAL render::ShapeShowFromBank (0x5d861c) chain.
// =============================================================================
#include "play/session_panels.h"

#include "gui/menu_render.h"        // MenuFillRect (0x423c70)
#include "gui/text/textdb.h"        // gui::text::TextDb (string resolve)
#include "gui/tooltip_build.h"      // Tooltip_ResolveContact (0x4f83e8 core)
#include "play/hud_render.h"        // HudPalette / hooks / HudBarFillPixels
#include "render/font.h"            // FontInitGlyphTable (0x42E350)
#include "render/surface.h"         // SurfaceDrawRectOutline (0x4242d4)
#include "render/surface_present.h" // PresentGlobals (DrawText lock target)
#include "render/text_raster.h"     // DrawText / DrawGlyph (0x434E18/0x434D0C)

#include <cstdio>
#include <cstring>

namespace guild::play {

// ---------------------------------------------------------------------------
// One recorded builder emission (a VIBE_ runtime call with its original args).
// ---------------------------------------------------------------------------
struct SessionPanels::Op {
    enum class K { TextId, TextFmt, Icon, Label, Bar, Card, Slider };
    K   k = K::TextId;
    int win = 0;                  // sub-window selected when emitted
    int textId = 0;               // TextId/Label: the text/template id
    int argc = 0;                 // TextId: trailing args used
    int a0 = 0, a1 = 0;           // args (original order)
    const char* fmt = nullptr;    // TextFmt: the .rdata format string
    int objId = 0;                // Icon: object id / Bar: gfx base
    int x = 0, y = 0;
    int range = 0, lo = 0, hi = 0, value = 0; // Slider geometry + value
    std::string literal;          // pre-synthesized line (panel header)
};

namespace {

// ---- 16bpp draw plumbing (the SessionHud model; internal linkage) ----------

const u8* GlyphMap() {
    static u8 table[256];
    static bool init = false;
    if (!init) { render::FontInitGlyphTable(table); init = true; }
    return table;
}

render::PresentGlobals SurfacePresent16(render::Surface& s) {
    render::PresentGlobals g;
    g.mode         = render::PresentBackend::DDrawLockBlt;
    g.ppvBits      = reinterpret_cast<std::uintptr_t>(s.pixels);
    g.dibPitch     = s.pitch;
    g.dibStride    = s.width;
    g.screenHeight = s.height;
    g.pitchExtra   = 2;
    g.lockBitDepth = 16;
    g.primary      = nullptr;
    return g;
}

int DrawLine16(render::Surface& s, int x, int y, const char* text,
               u8 r, u8 g, u8 b) {
    if (!text || !text[0]) return 0;
    render::PresentGlobals pg = SurfacePresent16(s);
    render::DrawText(x, y, reinterpret_cast<const u8*>(text), r, g, b,
                     GlyphMap(), pg, s.fmt);
    return static_cast<int>(std::strlen(text));
}

// ---- text-id / rich-template projection (named gap: 0x59d6e8) --------------

std::string ResolveTextId(const gui::text::TextDb* db, int id) {
    if (db) {
        if (const char* t = db->Text(id))
            return t;
    }
    char b[24];
    std::snprintf(b, sizeof(b), "T%d", id);
    return b;
}

// Plain-glyph projection of a recovered rich template: "$x" control tokens are
// stripped, %i/%a take the next int arg, %s resolves the next arg as a text id,
// %<d>N<d> (the person-name tokens) renders "P<id>".  The template/argument
// pairs themselves are the byte-exact recovered ones (tooltip_content).
std::string ExpandRich(const gui::text::TextDb* db, const char* fmt,
                       const int* args, int argc) {
    std::string out;
    int ai = 0;
    auto nextArg = [&]() -> int { return ai < argc ? args[ai++] : 0; };
    for (const char* p = fmt; *p;) {
        if (*p == '$') {                       // control token: skip "$x"
            ++p;
            if (*p) ++p;
            continue;
        }
        if (*p != '%') { out += *p++; continue; }
        ++p;                                   // past '%'
        if (*p == '%') { out += '%'; ++p; continue; }
        if (*p == 'i' || *p == 'a') {
            char b[16];
            std::snprintf(b, sizeof(b), "%d", nextArg());
            out += b; ++p; continue;
        }
        if (*p == 's') { out += ResolveTextId(db, nextArg()); ++p; continue; }
        if (*p >= '0' && *p <= '9' && p[1] == 'N' && p[2] >= '0' && p[2] <= '9') {
            char b[16];
            std::snprintf(b, sizeof(b), "P%d", nextArg());
            out += b; p += 3; continue;
        }
        out += '%';                            // unknown spec: keep literally
    }
    return out;
}

// A TextId op line: the text entry itself is the template; without a TextDb the
// projection is "T<id>" plus its args (deterministic fallback).
std::string ExpandTextIdOp(const gui::text::TextDb* db, int textId,
                           const int* args, int argc) {
    if (db) {
        if (const char* t = db->Text(textId))
            return ExpandRich(db, t, args, argc);
    }
    std::string out;
    char b[24];
    std::snprintf(b, sizeof(b), "T%d", textId);
    out = b;
    for (int i = 0; i < argc; ++i) {
        std::snprintf(b, sizeof(b), " %d", args[i]);
        out += b;
    }
    return out;
}

// Tooltip_ResolveContact takes a plain function pointer; route it through the
// active TextDb (file-static, set around the call).
const gui::text::TextDb* g_contactDb = nullptr;
int ContactFindIndex(const char* key) {
    return g_contactDb ? g_contactDb->FindIndex(key) : -1;
}

} // namespace

// ---------------------------------------------------------------------------
// Recording hosts.
// ---------------------------------------------------------------------------
class SessionPanels::TipHost : public gui::TooltipContentHost {
public:
    TipHost(SessionPanels* p, std::vector<Op>* ops, std::string* formName,
            int boxX, int boxY, int boxW, int screenW)
        : p_(p), ops_(ops), formName_(formName), boxX_(boxX), boxY_(boxY),
          boxW_(boxW), screenW_(screenW) {}

    int  LoadForm(const char* name) override {
        *formName_ = name;
        return ++p_->formSeq_;
    }
    void CenterChildWindows(int form) override { (void)form; }
    bool WindowGeom(int form, int* x, int* y, int* w) override {
        (void)form;
        *x = boxX_; *y = boxY_; *w = boxW_;
        return true;
    }
    void MoveWindow(int x, int y) override {   // the 0x4f7aa5 clamp landing
        boxX_ = x; boxY_ = y; clamped_ = true;
    }
    void SelectWindow(int form, int sub) override { (void)form; win_ = sub; }
    void AddIconObject(int objId) override {
        Op o; o.k = Op::K::Icon; o.win = win_; o.objId = objId;
        ops_->push_back(o);
    }
    void AddAnimatedObject(int objId, int x, int y) override {
        Op o; o.k = Op::K::Icon; o.win = win_; o.objId = objId; o.x = x; o.y = y;
        ops_->push_back(o);
    }
    void Text(int textId) override {
        Op o; o.k = Op::K::TextId; o.win = win_; o.textId = textId;
        ops_->push_back(o);
    }
    void TextArg(int templateId, int a0) override {
        Op o; o.k = Op::K::TextId; o.win = win_; o.textId = templateId;
        o.argc = 1; o.a0 = a0;
        ops_->push_back(o);
    }
    void TextArg2(int templateId, int a0, int a1) override {
        Op o; o.k = Op::K::TextId; o.win = win_; o.textId = templateId;
        o.argc = 2; o.a0 = a0; o.a1 = a1;
        ops_->push_back(o);
    }
    void TextFmt(const char* fmt, int a0, int a1) override {
        Op o; o.k = Op::K::TextFmt; o.win = win_; o.fmt = fmt;
        o.argc = 2; o.a0 = a0; o.a1 = a1;
        ops_->push_back(o);
    }
    void TextLabel(int x, int y, int textId) override {
        Op o; o.k = Op::K::Label; o.win = win_; o.textId = textId;
        o.x = x; o.y = y;
        ops_->push_back(o);
    }
    void SkillBar(int cur, int y, int skillRow, int baseGfx) override {
        Op o; o.k = Op::K::Bar; o.win = win_; o.a0 = cur; o.a1 = skillRow;
        o.y = y; o.objId = baseGfx;
        ops_->push_back(o);
    }
    void PersonCard(int x, int y) override {
        Op o; o.k = Op::K::Card; o.win = win_; o.x = x; o.y = y;
        ops_->push_back(o);
    }

    int  boxX() const { return boxX_; }
    int  boxY() const { return boxY_; }
    bool clamped() const { return clamped_; }

private:
    SessionPanels* p_;
    std::vector<Op>* ops_;
    std::string* formName_;
    int boxX_, boxY_, boxW_, screenW_;
    int win_ = 0;
    bool clamped_ = false;
};

class SessionPanels::PanelHost : public gui::InfoPanelHost {
public:
    PanelHost(SessionPanels* p, std::vector<Op>* ops, std::string* formName,
              int panelH)
        : p_(p), ops_(ops), formName_(formName), panelH_(panelH) {}

    int LoadForm(const char* name) override {
        *formName_ = name;
        return ++p_->formSeq_;
    }
    void SelectWindow(int form, int sub) override { (void)form; win_ = sub; }
    // ((window.h >> 16) - 48) >> 1 — the original's icon-row half-height; the
    // session panel box height stands in for the .form window height.
    int CurrentWindowHalfHeight() override { return (panelH_ - 48) >> 1; }
    void RenderRichString(const char* fmt) override {
        Op o; o.k = Op::K::TextFmt; o.win = win_; o.fmt = fmt;
        ops_->push_back(o);
    }
    int AddObject(int gfx, int y, int objId) override {
        Op o; o.k = Op::K::Icon; o.win = win_; o.objId = objId;
        o.x = gfx; o.y = y;
        ops_->push_back(o);
        return ++widgetSeq_;
    }
    int AddSprite(int x, int y, int gfxId) override {
        Op o; o.k = Op::K::Icon; o.win = win_; o.objId = gfxId;
        o.x = x; o.y = y;
        ops_->push_back(o);
        return ++widgetSeq_;
    }
    int AddSlider(int x, int y, int value, int range, int maxVal, int gfxBase,
                  int flags) override {
        (void)gfxBase; (void)flags;
        Op o; o.k = Op::K::Slider; o.win = win_; o.x = x; o.y = y;
        o.range = range; o.lo = 0; o.hi = maxVal; o.value = value;
        ops_->push_back(o);
        sliderOp_ = static_cast<int>(ops_->size()) - 1;
        return ++widgetSeq_;
    }
    void SetValueOrText(int widget, int text, int a3, int value, int a5) override {
        (void)widget; (void)a5;
        if (sliderOp_ < 0) return;
        Op& o = (*ops_)[static_cast<size_t>(sliderOp_)];
        o.lo = text; o.hi = a3; o.value = value;   // (lo, hi, value)
    }
    int BuildingUpgradeLevel(const void* record) override {
        const auto* b = static_cast<const gui::InfoBuildingRecord*>(record);
        return b ? b->upgradeLevel : 0;
    }

private:
    SessionPanels* p_;
    std::vector<Op>* ops_;
    std::string* formName_;
    int panelH_;
    int win_ = 0;
    int widgetSeq_ = 0;
    int sliderOp_ = -1;
};

// ---------------------------------------------------------------------------
// The dispatch sink: runs the reconstructed content builder for the subject
// kind the REAL dispatch selected.
// ---------------------------------------------------------------------------
class SessionPanels::Sink : public gui::TooltipDispatchSink {
public:
    Sink(SessionPanels* p, const SessionPanelsInputs* in, TipHost* host,
         int screenW)
        : p_(p), in_(in), host_(host), screenW_(screenW) {}

    void Destroy(int handle) override { (void)handle; destroyed_ = true; }

    int Build(gui::TooltipKind kind, const gui::TooltipSubject& subject,
              int& anchorId) override {
        (void)subject;
        const SessionPanelsInputs& in = *in_;
        int handle = -1;
        switch (kind) {
        case gui::TooltipKind::kObject: {
            const PanelObjectHover def{};
            const PanelObjectHover& h = in.object ? *in.object : def;
            gui::TooltipObjectEnv env = h.env;
            if (!env.screenW) env.screenW = screenW_;  // dword_69FFBC >> 16
            handle = gui::Tooltip_BuildObjectContent(
                static_cast<i16>(in.hoverObjectCode), h.view, env, *host_);
            break;
        }
        case gui::TooltipKind::kUpgrade: {
            const PanelObjectHover def{};
            const PanelObjectHover& h = in.object ? *in.object : def;
            gui::TooltipUpgradeEnv env = h.upgradeEnv;
            if (!env.screenW) env.screenW = screenW_;  // dword_69FFBC >> 16
            handle = gui::Tooltip_BuildUpgradeContent(
                static_cast<i16>(in.hoverObjectCode), in.hoverObjectClass,
                h.view, h.upgradeOwner, env, *host_);
            break;
        }
        case gui::TooltipKind::kBuilding: {
            const PanelBuildingHover def{};
            const PanelBuildingHover& h = in.building ? *in.building : def;
            handle = gui::Tooltip_BuildBuildingContent(
                in.hoverBuildingCode, h.colorSelector, h.extraField,
                h.salePrice, *host_);
            break;
        }
        case gui::TooltipKind::kPerson: {
            const PanelPersonHover def{};
            const PanelPersonHover& h = in.person ? *in.person : def;
            handle = gui::Tooltip_BuildPersonContent(h.view, h.env, *host_);
            break;
        }
        case gui::TooltipKind::kContact: {
            // VIBE_Tooltip_BuildContact @0x4f83e8: key resolve first; the form
            // is only loaded when "_HILFE_<NAME>+0" resolves (else -1).
            g_contactDb = in.textDb;
            gui::ContactTooltip ct = gui::Tooltip_ResolveContact(
                in.contactName ? in.contactName : "", &ContactFindIndex);
            g_contactDb = nullptr;
            if (ct.hasText)
                handle = gui::Tooltip_BuildContactContent(ct.textIndex, *host_);
            break;
        }
        case gui::TooltipKind::kNone:
            break;
        }
        // dword_676A64[171*handle] — the form's anchor object; the recording
        // form IS its own anchor here.
        anchorId = handle;
        builtKind_ = kind;
        ++builds_;
        return handle;
    }

    void Raise(int handle) override { (void)handle; }

    int builds() const { return builds_; }
    bool destroyed() const { return destroyed_; }

private:
    SessionPanels* p_;
    const SessionPanelsInputs* in_;
    TipHost* host_;
    int screenW_ = 0;
    gui::TooltipKind builtKind_ = gui::TooltipKind::kNone;
    int builds_ = 0;
    bool destroyed_ = false;
};

// ---------------------------------------------------------------------------
SessionPanels::SessionPanels() {
    objectShadow_.assign(static_cast<size_t>(gui::kObjectSpan), 0);
    buildingShadow_.assign(static_cast<size_t>(gui::kBuildingSpan), 0);
    Reset();
}

SessionPanels::~SessionPanels() = default;

void SessionPanels::Reset() {
    tip_ = gui::TooltipDispatchState{};
    snap_ = gui::InfoSnapshot{};
    snapValid_ = false;
    snap_.valid = false;
    tipOps_.clear();
    panelOps_.clear();
    tipFormName_.clear();
    panelFormName_.clear();
    last_ = SessionPanelsResult{};
}

// ---------------------------------------------------------------------------
void SessionPanels::Frame(render::Surface& s, const SessionPanelsInputs& in) {
    last_ = SessionPanelsResult{};
    if (!s.pixels || s.width <= 0 || s.height <= 0 || s.bpp != 16)
        return;
    RunTooltip(s, in);
    RunPanel(s, in);
}

// ---------------------------------------------------------------------------
// Tooltip: the REAL dispatch (@0x4f7424) over the session's shadow tables.
// ---------------------------------------------------------------------------
void SessionPanels::RunTooltip(render::Surface& s, const SessionPanelsInputs& in) {
    // ---- materialise the scene-table view the classification reads --------
    gui::TooltipTables tables;
    const u8* sceneRef = nullptr;
    switch (in.hoverKind) {
    case gui::TooltipKind::kObject:
    case gui::TooltipKind::kUpgrade: {
        tables.objectBase = objectShadow_.data();
        const int code = static_cast<i16>(in.hoverObjectCode);
        const size_t off = static_cast<size_t>(gui::kObjectStride) *
                           static_cast<size_t>(code > 0 ? code : 0);
        if (off < objectShadow_.size())
            objectShadow_[off] = in.hoverObjectClass;  // record +0 class byte
        sceneRef = objectShadow_.data() + off;
        break;
    }
    case gui::TooltipKind::kBuilding:
        tables.buildingBase = buildingShadow_.data();
        buildingShadow_[0] = static_cast<u8>(in.hoverBuildingCode);
        sceneRef = buildingShadow_.data();
        break;
    case gui::TooltipKind::kPerson:
        std::memcpy(personShadow_, &in.hoverPersonId, sizeof(u16));
        tables.personRecLo = personShadow_;
        tables.personRecHi = personShadow_ + sizeof(personShadow_);
        sceneRef = personShadow_;
        break;
    case gui::TooltipKind::kContact:
    case gui::TooltipKind::kNone:
    default:
        break;
    }

    // ---- thread the frame's hover state into the dispatch state -----------
    tip_.hoveredTooltipId = in.hoveredTooltipId;   // dword_75BF3C
    tip_.scenePickActive  = in.scenePickActive;    // dword_672238
    tip_.contactReqId     = in.contactReqId;       // dword_631724
    contactShadowWord_    = 0;                     // *(u16*)dword_63172C == 0
    tip_.contactReqPtr    = in.contactReqId ? &contactShadowWord_ : nullptr;

    // The composited box anchors at the cursor at BUILD time (the
    // Form_CenterChildWindows-on-cursor model), then the REAL clamp block
    // (0x4f7aa5) repositions it against the right screen edge.
    const int pendX = in.cursorX + layout.tooltipOffsetX;
    const int pendY = in.cursorY + layout.tooltipOffsetY;

    std::vector<Op> freshOps;
    std::string freshName;
    TipHost host(this, &freshOps, &freshName, pendX, pendY, layout.tooltipW,
                 s.width);
    Sink sink(this, &in, &host, s.width);

    const gui::TooltipAction act =
        gui::Tooltip_Dispatch(tip_, sink, tables, sceneRef);

    last_.tooltipAction = act;
    last_.tooltipBuilds = sink.builds();
    if (sink.builds() > 0 && tip_.formHandle != -1) {
        // A new form was built and survived: adopt its content + position.
        tipOps_ = std::move(freshOps);
        tipFormName_ = freshName;
        tipBoxX_ = host.boxX();
        tipBoxY_ = host.boxY();
        tipClamped_ = host.clamped();
    } else if (tip_.formHandle == -1) {
        tipOps_.clear();                  // hidden / never built
        tipFormName_.clear();
        tipClamped_ = false;
    }

    last_.tooltipVisible = tip_.formHandle != -1 && !tipOps_.empty();
    last_.tooltipClamped = tipClamped_;
    std::snprintf(last_.tooltipForm, sizeof(last_.tooltipForm), "%s",
                  tipFormName_.c_str());
    if (last_.tooltipVisible)
        DrawTooltipBox(s, in);
}

// ---------------------------------------------------------------------------
void SessionPanels::DrawTooltipBox(render::Surface& s,
                                   const SessionPanelsInputs& in) {
    const HudPalette pal;
    const HudRenderHooks& hooks = GetHudRenderHooks();

    // Box extent: line stack vs the builders' own row coordinates.
    int textLines = 0, maxOpY = 0;
    for (const Op& o : tipOps_) {
        if (o.k == Op::K::TextId || o.k == Op::K::TextFmt || o.k == Op::K::Label)
            ++textLines;
        if (o.y > maxOpY) maxOpY = o.y;
    }
    const int w = layout.tooltipW;
    int h = 2 * layout.pad + textLines * layout.lineH;
    if (2 * layout.pad + maxOpY + layout.lineH > h)
        h = 2 * layout.pad + maxOpY + layout.lineH;

    const int bx = tipBoxX_, by = tipBoxY_;
    gui::MenuFillRect(&s, bx, by, w, h, pal.barTrackR, pal.barTrackG,
                      pal.barTrackB);
    render::SurfaceDrawRectOutline(&s, bx, by, w, h, pal.barFrameR,
                                   pal.barFrameG, pal.barFrameB);

    int line = 0;
    for (const Op& o : tipOps_) {
        switch (o.k) {
        case Op::K::TextId: {
            const int args[2] = {o.a0, o.a1};
            std::string t = ExpandTextIdOp(in.textDb, o.textId, args, o.argc);
            if (DrawLine16(s, bx + layout.pad, by + layout.pad + line * layout.lineH,
                           t.c_str(), pal.textR, pal.textG, pal.textB))
                ++last_.tooltipTextOps;
            ++line;
            break;
        }
        case Op::K::TextFmt: {
            const int args[2] = {o.a0, o.a1};
            std::string t = ExpandRich(in.textDb, o.fmt ? o.fmt : "", args,
                                       o.argc);
            if (DrawLine16(s, bx + layout.pad, by + layout.pad + line * layout.lineH,
                           t.c_str(), pal.textR, pal.textG, pal.textB))
                ++last_.tooltipTextOps;
            ++line;
            break;
        }
        case Op::K::Label: {
            std::string t = ResolveTextId(in.textDb, o.textId) + ":"; // "%s:"
            DrawLine16(s, bx + layout.pad + o.x, by + layout.pad + o.y,
                       t.c_str(), pal.textR, pal.textG, pal.textB);
            ++last_.tooltipTextOps;
            ++line;
            break;
        }
        case Op::K::Icon: {
            ++last_.tooltipIconOps;
            if (hooks.drawSprite &&
                hooks.drawSprite(&s, bx + layout.pad + o.x,
                                 by + layout.pad + o.y, o.objId,
                                 hooks.userData))
                ++last_.tooltipIconBlits;
            break;
        }
        case Op::K::Bar: {
            // The person skill bar (VIBE_Hud_BuildScaledTiledBar edge): the
            // track only — the bar VALUE reads the live person table (sim
            // cluster edge), so a fill here would be invented (rule 8).
            render::SurfaceDrawRectOutline(&s, bx + layout.pad + 60,
                                           by + layout.pad + o.y, 50, 5,
                                           pal.barFrameR, pal.barFrameG,
                                           pal.barFrameB);
            break;
        }
        case Op::K::Card: {
            // VIBE_Hud_BuildPersonCardSimple @0x55433c edge: the card frame.
            render::SurfaceDrawRectOutline(&s, bx + layout.pad + o.x,
                                           by + layout.pad + o.y, 24, 24,
                                           pal.barFrameR, pal.barFrameG,
                                           pal.barFrameB);
            break;
        }
        case Op::K::Slider:
            break;
        }
    }

    last_.tooltipX = bx;
    last_.tooltipY = by;
    last_.tooltipW = w;
    last_.tooltipH = h;
}

// ---------------------------------------------------------------------------
// Info panel: the REAL InfoPanel_Update (@0x4b84c0) + builders (@0x4b64b0..).
// ---------------------------------------------------------------------------
namespace {

// The command sink InfoPanel_Update fires on a selection change.
struct PanelBuildRunner : gui::InfoPanelCommandSink {
    gui::InfoPanelHost* host = nullptr;
    const SessionPanelsInputs* in = nullptr;
    gui::InfoBuilder ran = gui::InfoBuilder::kNone;

    void Build(gui::InfoBuilder b) override {
        ran = b;
        const SessionPanelsInputs& i = *in;
        gui::ResetInfoPanelBuild();          // dword_631768 = -1 (panel free)
        gui::InfoPanel_SetHost(host);
        switch (b) {
        case gui::InfoBuilder::kTransporter: {
            // VIBE_InfoPanel_BuildTransporter @0x4b6c80 — the owner resolve
            // (GameObject_ResolveOwnerOrParentB) is the session's record feed.
            gui::InfoObjectRecord def{};
            gui::InfoPanel_BuildTransporter(
                i.selObject ? *i.selObject : def, i.selObject != nullptr,
                /*loadPct*/ 0, /*sliderValue*/ 0);
            break;
        }
        case gui::InfoBuilder::kPerson: {
            gui::InfoPersonRecord def{};
            gui::InfoPanel_BuildPerson(i.selPerson ? *i.selPerson : def);
            break;
        }
        case gui::InfoBuilder::kObject: {
            gui::InfoObjectRecord def{};
            gui::InfoPanel_BuildObject(i.selObject ? *i.selObject : def,
                                       /*room*/ nullptr, /*building*/ nullptr,
                                       i.selOwnerMatches, i.selSlotsAfter);
            break;
        }
        case gui::InfoBuilder::kBuilding: {
            gui::InfoBuildingRecord def{};
            gui::InfoPanel_BuildBuilding(
                i.selBuilding ? *i.selBuilding : def, i.selSelectionFlags,
                i.selCategory, i.selItemIsOwn,
                /*noBuildingSelected*/ i.selection.building == 0);
            break;
        }
        case gui::InfoBuilder::kStandard:
            gui::InfoPanel_BuildStandard(
                i.selItemIsOwn ? i.selBuilding : nullptr);
            break;
        case gui::InfoBuilder::kNone:
            break;
        }
        gui::InfoPanel_SetHost(nullptr);
    }
};

} // namespace

void SessionPanels::RunPanel(render::Surface& s, const SessionPanelsInputs& in) {
    if (!snapValid_) {
        // First frame: force the change detection to fire exactly like the
        // original's zeroed BSS snapshot vs a live selection.
        snap_ = gui::InfoSnapshot{};
        snap_.valid = false;
        snapValid_ = true;
    }

    std::vector<Op> freshOps;
    std::string freshName;
    PanelHost host(this, &freshOps, &freshName, layout.panelH);
    PanelBuildRunner runner;
    runner.host = &host;
    runner.in = &in;

    gui::InfoPanel_SetCommandSink(&runner);
    const gui::InfoBuilder b = gui::InfoPanel_Update(in.selection, snap_);
    gui::InfoPanel_SetCommandSink(nullptr);

    if (b != gui::InfoBuilder::kNone) {       // a rebuild fired
        panelOps_ = std::move(freshOps);
        panelFormName_ = freshName;
        last_.panelRebuilt = true;
        last_.panelBuilder = b;
    }

    last_.panelVisible = !panelOps_.empty();
    std::snprintf(last_.panelForm, sizeof(last_.panelForm), "%s",
                  panelFormName_.c_str());
    if (last_.panelVisible)
        DrawPanelBox(s, in);
}

// ---------------------------------------------------------------------------
void SessionPanels::DrawPanelBox(render::Surface& s,
                                 const SessionPanelsInputs& in) {
    const HudPalette pal;
    const HudRenderHooks& hooks = GetHudRenderHooks();
    const int px = layout.panelX, py = layout.panelY;
    const int pw = layout.panelW, ph = layout.panelH;

    if (layout.drawBox) {
        gui::MenuFillRect(&s, px, py, pw, ph, pal.barTrackR, pal.barTrackG,
                          pal.barTrackB);
        render::SurfaceDrawRectOutline(&s, px, py, pw, ph, pal.barFrameR,
                                       pal.barFrameG, pal.barFrameB);
    }

    int line = 0;
    for (const Op& o : panelOps_) {
        switch (o.k) {
        case Op::K::TextFmt: {
            // The header line: the recovered format with this selection's
            // argument fields (see SynthesizePanelLine provenance).
            std::string t = !o.literal.empty()
                                ? o.literal
                                : SynthesizePanelLine(o.fmt ? o.fmt : "", in);
            // Sidebar-card mode: the small gold face, centred, word-wrapped.
            if (!layout.drawBox && cardText.draw) {
                const int lines = cardText.draw(
                    s, px + pw / 2, py + layout.pad + line * (layout.lineH + 5),
                    pw + 8, t.c_str(), cardText.user);
                if (lines > 0) {
                    ++last_.panelTextOps;
                    line += lines;
                    break;
                }
            }
            if (DrawLine16(s, px + layout.pad,
                           py + layout.pad + line * layout.lineH, t.c_str(),
                           pal.textR, pal.textG, pal.textB))
                ++last_.panelTextOps;
            ++line;
            break;
        }
        case Op::K::Icon: {
            ++last_.panelIconOps;
            // Sidebar-card mode (drawBox off): the op's x/y are relative to the
            // engine's full-width info-panel form; in the narrow card slot the
            // thumbnail (the ~48px GEB building shape) centres horizontally in
            // the card's middle band, like the original's card.
            int ix = px + o.x, iy = py + o.y;
            if (!layout.drawBox) {
                ix = px + (pw - 48) / 2;
                iy = py + ph / 3;
            }
            if (hooks.drawSprite &&
                hooks.drawSprite(&s, ix, iy, o.objId, hooks.userData))
                ++last_.panelIconBlits;
            break;
        }
        case Op::K::Slider: {
            const int trackW = o.range > 0 ? o.range : 48;
            gui::MenuFillRect(&s, px + o.x, py + o.y, trackW, 6, pal.barTrackR,
                              pal.barTrackG, pal.barTrackB);
            double ratio = 0.0;
            if (o.hi > o.lo)
                ratio = static_cast<double>(o.value - o.lo) /
                        static_cast<double>(o.hi - o.lo);
            const int fill = HudBarFillPixels(ratio, trackW);
            if (fill > 0)
                gui::MenuFillRect(&s, px + o.x, py + o.y, fill, 6, pal.barFillR,
                                  pal.barFillG, pal.barFillB);
            render::SurfaceDrawRectOutline(&s, px + o.x, py + o.y, trackW, 6,
                                           pal.barFrameR, pal.barFrameG,
                                           pal.barFrameB);
            last_.panelSliderFillPx = fill;
            break;
        }
        default:
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// The panel header projection.  The builders' RenderRichString formats are
// recovered 1:1 in gui/infopanel_build (with their argument lists documented at
// each call site); the host edge passes only the format, so the line is
// re-assembled here from the SAME record fields the original formats:
//   building:  "$Z%s$A%s[...]"  args (14*code+1078 [, customName])  @0x4b64b0
//   object:    "$Z%s[...]"      args (2*code+2151 [, host fields])  @0x4b6930
//   person:    "%i %s" (aggregate) / "%s %i%%" (detail)              @0x4b7104
// (rich renderer @0x59d6e8 = named gap; this is its plain-glyph projection).
// ---------------------------------------------------------------------------
std::string SessionPanels::SynthesizePanelLine(const char* fmt,
                                               const SessionPanelsInputs& in) {
    const gui::text::TextDb* db = in.textDb;
    std::string name;
    if (in.selBuilding) {
        // Keyed localized name first: "_GEB_<TYPE>_NAME+0" (the textbin key
        // namespace; '~' is the engine's soft wrap hint -> space here, the
        // card renderer word-wraps on width).
        if (db && in.selBuildingTypeName && in.selBuildingTypeName[0]) {
            std::string key = "_GEB_";
            key += in.selBuildingTypeName;
            key += "_NAME+0";
            const int ki = db->FindIndex(key.c_str());
            if (ki >= 0 && db->Text(ki)) {
                name = db->Text(ki);
                for (char& c : name)
                    if (c == '~') c = ' ';
            }
        }
        if (name.empty())
            name = ResolveTextId(db, gui::kNameTextStride * in.selBuilding->code +
                                         gui::kNameTextBias);
        if (in.selBuilding->customName && in.selBuilding->customName[0]) {
            name += " >";
            name += in.selBuilding->customName;
            name += "<";
        }
    } else if (in.selObject) {
        name = ResolveTextId(db, gui::kObjNameStride * in.selObject->code +
                                     gui::kObjNameBias);
        if (in.selObject->customName && in.selObject->customName[0]) {
            name += " >";
            name += in.selObject->customName;
            name += "<";
        }
    } else if (in.selPerson) {
        char b[32];
        if (in.selPerson->aggregate) {
            std::snprintf(b, sizeof(b), "%d", in.selPerson->aggregateCount);
            name = b;                         // "%i %s" aggregate count
        } else {
            name = ResolveTextId(db, gui::kNameTextStride *
                                             in.selPerson->nameCode +
                                         gui::kNameTextBias);
            std::snprintf(b, sizeof(b), " %d%%", in.selPerson->outputRatioPct);
            name += b;                        // "%s %i%%"
        }
    }
    if (name.empty()) {
        // Token-stripped format (deterministic fallback).
        const int none[2] = {0, 0};
        return ExpandRich(db, fmt, none, 0);
    }
    return name;
}

} // namespace guild::play
