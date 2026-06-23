// See wire_apply_input.h. Binds the cursor-click ORDER-ROUTER (IssueOnObjectHooks)
// and the ACTION target-pick GUI bridge (ActionTargetPickHooks) to their real
// reconstructed leaves. Glue only — no module logic.
//
// SURVEYED BRIDGES (this wiring agent's slice) and their disposition:
//
//   IssueOnObjectHooks    (sim/command_apply12.h)  -> WIRED (labelStrncmp).
//   ActionTargetPickHooks (gui/action_target_pickn.h) -> WIRED (lightSetGrayThunk).
//   InputCommandApplyHooks (play/input_command.h) -> ALREADY REAL by default: the
//       inert-default applyOrder (input_command.cpp DefaultApplyOrder) already
//       mutates the live entity arrays (BuildingFindById / PersonFindRecordById),
//       i.e. the genuine order-apply effect IS the default. No real leaf to bind
//       beyond what is already active; nothing installed.
//   ItemLabelHooks        (play/text_recon3_itemlabel.h) -> ZERO bindable: the
//       string-table resolver (VIBE_String_GetDelimitedField over dword_8C36B0)
//       and the language fallback name tables (byte_13CD6A0 / dword_8C4784) are
//       NOT reconstructed (string-table cluster). Stays inert.
//   AvatarStreamHooks     (sim/personnel_recruit2.h) -> ZERO bindable: Write/Read
//       are the VIBE_Vfs_WriteStream / ReadStreamBool boundary, which need a LIVE
//       VfsHandle (file-I/O, not a pre-approved swap); no standalone reconstructed
//       buffer leaf matches the (src,size,count)->bool contract. Stays inert.
#include "sim/wire_apply_input.h"

#include "sim/command_apply12.h"        // IssueOnObjectHooks / SetIssueOnObjectHooks
#include "gui/action_target_pickn.h"    // ActionTargetPickHooks / SetActionTargetPickHooks
#include "util/string_ops.h"            // util::StrncmpN (VIBE_Util_StrncmpN @0x5e9ee0)
#include "render/light.h"               // render::BroadcastGrayDword (0x5c6af0)

namespace guild::sim {

namespace {

// =========================================================================
// IssueOnObjectHooks.labelStrncmp -> VIBE_Util_StrncmpN @0x5e9ee0.
// The router classifies the selected order-label with two prefix compares
// ("sp_CONQUER",10 / "WARE",4); the original used VIBE_Util_StrncmpN. Forward
// straight into the reconstructed util sibling (signatures are identical:
// int(const char*, const char*, int)).
// =========================================================================
int WaiLabelStrncmp(const char* a, const char* b, int n) {
    return util::StrncmpN(a, b, n);
}

// =========================================================================
// ActionTargetPickHooks.lightSetGrayThunk -> VIBE_Light_SetGrayColorThunk
// @0x5c6af0. ABI (verified by disasm @0x5c6af0 + call site @0x548c0c):
//   VIBE_Light_SetGrayColorThunk(level@edx, count@ebx, dst@eax)
//   - the value broadcast across all four dword byte-lanes is `level` (edx),
//     i.e. the FIRST positional arg (`mov dh,dl; shl edx,8; ...` runs on edx);
//   - `count` (ebx) is the byte-fill length passed to the dword memory fill;
//   - `dst` (eax) is the record pointer being filled.
// At BOTH live call sites (@0x548c20 `xor edx,edx; mov ebx,28h` and the
// PromptTargetSelect twin @0x5488c3) the broadcast level is 0 and the fill
// count is 0x28 (40 bytes). The reimpl hook is laid out positionally as
// (a@edx /*level*/, level@ebx /*count*/, recOut@eax /*dst*/) — so the genuine
// broadcast SOURCE is the first hook param `a`, NOT the (misnamed) `level`
// param, which actually carries the fill byte-count. We broadcast `a` and write
// one dword to recOut (the original immediately overwrites the rest of the
// 40-byte record, so the first dword == broadcast(level) is the only surviving
// fill effect). The inert default stored the count verbatim — this binds the
// GENUINE broadcast of the real grey level.
// =========================================================================
void WaiLightSetGrayThunk(int a /*level@edx*/, int /*count@ebx*/, std::int32_t* recOut) {
    if (recOut)
        *recOut = static_cast<std::int32_t>(
            guild::render::BroadcastGrayDword(static_cast<u8>(a)));
}

// --- process-lifetime wired hook tables (the installed pointers reference these).
gui::ActionTargetPickHooks g_targetPick{};
IssueOnObjectHooks         g_issueOnObject{};

} // namespace

void InstallRealApplyInputWiring() {
    // --- IssueOnObjectHooks (command_apply12.h) ------------------------------
    // SEED-FROM-DEFAULTS: a fresh std::function table whose UNSET fields are the
    // inert defaults — every IssueOnObject field is null-guarded at the call site
    // (orderArmed/pickedObject/.../raycastGround/rejectBanner), so leaving them
    // empty preserves the safe inert behaviour. Bind ONLY the reconstructed leaf.
    g_issueOnObject = IssueOnObjectHooks{};
    g_issueOnObject.labelStrncmp = &WaiLabelStrncmp;
    // orderArmed / pickedObject / pickedLabel / pickedIsUnit / pickedUnitOwner /
    // attackAllowed / raycastGround / rejectBanner: game-state globals
    // (dword_67221C / dword_631720 / *(picked+535) / +512 / +364 / byte_671D96 /
    // VIBE_Heightmap_RaycastFromCursor cursor+map / VIBE_Hud_SetStatusBannerText)
    // with no standalone reconstructed home -> inert.
    SetIssueOnObjectHooks(&g_issueOnObject);

    // --- ActionTargetPickHooks (action_target_pickn.h) -----------------------
    // SEED-FROM-DEFAULTS: copy the module's inert defaults (all five fields are
    // raw function pointers invoked WITHOUT a null-check), then override only the
    // wireable leaf. Keeps the unbound fields on their safe inert stubs.
    g_targetPick = *gui::ActionTargetPickHooks_Default();
    g_targetPick.lightSetGrayThunk = &WaiLightSetGrayThunk;
    // amtRunOfficeOverviewWindow (VIBE_Amt_RunOfficeOverviewWindow @0x5575c8 — a
    // window leaf; the reconstruction is decomposed into per-slot helpers, not a
    // single int(cfg,table,callback) entry) / textRenderFormattedMessage
    // (VIBE_Text_RenderFormattedMessage @0x59f99c — the text-id message formatter
    // itself is not reconstructed, only its inline value formatters) /
    // hudUpdateEdgeScroll (VIBE_Hud_UpdateEdgeScroll @0x4bc07c — different ABI,
    // needs live CameraState) / confirmAbductCallback (VIBE_ActionDialog_
    // ConfirmAbduct @0x54891c — UI callback): no clean target -> inert.
    gui::SetActionTargetPickHooks(&g_targetPick);
}

} // namespace guild::sim
