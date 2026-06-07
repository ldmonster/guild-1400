// guild::gui — action_target_pickn implementation (P6 / Wave 29 GUI coverage slice).
// 1:1 reconstructions of deferred VIBE_ActionDialog_* target-pick builders.

#include "gui/action_target_pickn.h"

#include <cstdint>
#include <cstring>

namespace guild::gui {

// The abduct office-table pointer (original dword_8C845C). Modeled as a settable
// handle so tests can observe which table the window is pointed at.
const void* g_abductOfficeTable = nullptr; // dword_8C845C

// ===========================================================================
// Pure deterministic core: pack the target-pick config record.
//   Light_SetGrayColorThunk(0, grayLevel, &rec);  // rec[0] <- gray descriptor
//   rec[1] = 1024;                                 // +4 flag
//   rec[2] = payload;                              // +8 payload
//   kind   = 6;                                    // +12 byte
// The original first fills rec[0] via the gray-color thunk; we record grayLevel as the
// descriptor value (the thunk's deterministic effect on the first dword).
// ===========================================================================
TargetPickConfig ActionDialog_BuildTargetPickConfig(int grayLevel, std::int32_t payload) {
    TargetPickConfig cfg;
    cfg.overlayColor = grayLevel;
    cfg.flag = kTargetPickFlag;     // 1024
    cfg.payload = payload;
    cfg.kind = kTargetPickKind;     // 6
    return cfg;
}

// ===========================================================================
// Hooks (inert defaults).
// ===========================================================================
namespace {

void DefLightSetGrayThunk(int, int level, std::int32_t* recOut) {
    if (recOut) *recOut = level; // deterministic: first dword = gray level
}
int  DefAmtRunOfficeOverviewWindow(const TargetPickConfig*, const void*, const void*) { return 0; }
void DefTextRenderFormattedMessage(char* buf, int) { if (buf) buf[0] = '\0'; }
void DefHudUpdateEdgeScroll(int, int, int) {}
int  DefConfirmAbductCallback() { return 0; }

const ActionTargetPickHooks kDefaultHooks = {
    &DefLightSetGrayThunk, &DefAmtRunOfficeOverviewWindow,
    &DefTextRenderFormattedMessage, &DefHudUpdateEdgeScroll, &DefConfirmAbductCallback,
};

const ActionTargetPickHooks* g_hooks = &kDefaultHooks;

} // namespace

const ActionTargetPickHooks* SetActionTargetPickHooks(const ActionTargetPickHooks* hooks) {
    const ActionTargetPickHooks* prev = g_hooks;
    g_hooks = hooks ? hooks : &kDefaultHooks;
    return prev;
}
const ActionTargetPickHooks* ActionTargetPickHooks_Default() { return &kDefaultHooks; }
const ActionTargetPickHooks& ActionTargetPickHooksActive() { return *g_hooks; }

void ResetActionTargetPick() {
    g_abductOfficeTable = 0;
    g_hooks = &kDefaultHooks;
}

// ===========================================================================
// 0x548c0c — VIBE_ActionDialog_BeginAbductTargetPick.
// ===========================================================================
int ActionDialog_BeginAbductTargetPick(std::int32_t self) {
    const ActionTargetPickHooks& h = ActionTargetPickHooksActive();
    TargetPickConfig cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    h.lightSetGrayThunk(0, kAbductGrayLevel, &cfg.overlayColor);
    cfg.payload = self;             // v3[2] = this
    cfg.flag = kTargetPickFlag;     // v3[1] = 1024
    cfg.kind = kTargetPickKind;     // v4 = 6
    return h.amtRunOfficeOverviewWindow(&cfg, g_abductOfficeTable,
                                        reinterpret_cast<const void*>(h.confirmAbductCallback));
}

// ===========================================================================
// 0x5488ac — VIBE_ActionDialog_PromptTargetSelect.
// ===========================================================================
void ActionDialog_PromptTargetSelect(std::int32_t self, int a2, int a3, int a4) {
    (void)a2;
    const ActionTargetPickHooks& h = ActionTargetPickHooksActive();
    char msg[256];
    TargetPickConfig cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    h.lightSetGrayThunk(0, kAbductGrayLevel, &cfg.overlayColor);
    cfg.payload = 0;                // v8 = 0
    cfg.kind = kTargetPickKind;     // v9 = 6
    cfg.flag = kTargetPickFlag;     // v7 = 1024
    (void)self;
    h.textRenderFormattedMessage(msg, kPromptTextId);
    h.amtRunOfficeOverviewWindow(&cfg, msg, nullptr);
    h.hudUpdateEdgeScroll(0, a3, a4);
}

} // namespace guild::gui
