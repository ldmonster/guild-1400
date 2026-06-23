// See wire_worldnet.h. Audit installer for the four world-net / render-tail hook
// bridges. Most fields are host-boundary leaves or reconstructed siblings whose
// signatures do not faithfully match the hook field shape (see the header for the
// per-field rationale) and stay pinned at their inert defaults; the EXCEPTION is
// RenderLeaves9Hooks.ConvertRgbTo16/Convert8To16, which now bind the REAL
// render/shape_convert16 converters (0x5d7c0c / 0x5d7924).
#include "world/wire_worldnet.h"

#include "world/world_history2.h"        // WorldHistory2Hooks / Get/SetWorldHistory2Hooks
#include "play/menu_recon_transition.h"  // TransitionHooks / SetTransitionHooks
#include "render/render_leaves9.h"       // RenderLeaves9Hooks / Leaves9Hooks / SetLeaves9Hooks
#include "render/shape_convert16.h"      // InstallShapeConvertersIntoLeaves9 (real 0x5d7c0c/0x5d7924)
#include "gui/netfile_run.h"             // NetModeHooks / SetNetModeHooks

namespace guild::world {

void InstallRealWorldNetWiring() {
    // --- WorldHistory2Hooks (world/world_history2.h) -------------------------
    // POD table; SetWorldHistory2Hooks substitutes the inert default for any null
    // member, so re-installing the current (default) table is a faithful no-op.
    // processPlayerNews (VIBE_He_ProcessPlayerNews @0x4c4be8) is unreconstructed;
    // logMessage / universeSwitchActiveSlot / objectFindByHandle /
    // objectDetachAndRelease / arrangeIconsInCircle / createIconMesh / iconsEnabled
    // / parentEntityId / storeSlotInParent are diagnostic-sink / universe-node /
    // object-handle / icon-mesh render / entity-record host writes -> all INERT.
    {
        WorldHistory2Hooks h = GetWorldHistory2Hooks();
        SetWorldHistory2Hooks(&h);
    }

    // --- TransitionHooks (play/menu_recon_transition.h) ----------------------
    // fadeRegister (string tag vs the reconstructed Fade_Register's u8 flags byte +
    // back-buffer ptr, verified @0x41f0e8) / fadeDone (raw *(handle)&4) /
    // fadeUnregister (arity mismatch vs reconstructed Fade_Unregister(i32)) /
    // runFrameLoop / renderEntityScene / renderEntityList / groundplan* /
    // hudToggleHighlight: window+scene render with no signature-compatible
    // reconstructed target -> all INERT. nullptr re-installs the module defaults.
    guild::play::SetTransitionHooks(nullptr);

    // --- RenderLeaves9Hooks (render/render_leaves9.h) ------------------------
    // FrameDataProcess (reconstructed sibling needs a FrameBlitState, not the
    // hook's int a4 — fabricating it would violate rule 8): INERT, re-seeded from
    // the live table. ConvertRgbTo16 / Convert8To16 are NO LONGER inert: the real
    // VIBE_Shape_ConvertRgbTo16 @0x5d7c0c / VIBE_Shape_Convert8To16 @0x5d7924 are
    // reconstructed in render/shape_convert16 and installed here, so the
    // Shape_ConvertToNew @0x5d8080 driver (and ShapeBankConvertNew @0x5d80a8)
    // run the REAL depth-2/-0 -> depth-1 conversion chain.
    guild::render::InstallShapeConvertersIntoLeaves9();

    // --- NetModeHooks (gui/netfile_run.h) ------------------------------------
    // A virtual-vtable bridge; SetNetModeHooks(nullptr) installs the module's inert
    // default vtable. FormLoad / FormCenterChildWindows / FormSelectWindow /
    // RenderRichString / GetChildObjectId / RadioGroupCreate / RadioGroupFreeSurface
    // / FormDestroy / RunFrameLoop / InitStateReader / ClickReady / HoverId /
    // CancelEdge / SetObjectsVisible are menu-form-build / frame-loop / edge-source
    // render; RunHostNetworkSetup / SearchNetworkGames / ChooseNetworkProfile are
    // full GUI sub-screens (net/lobby only reconstructs the advert-build slice, not
    // signature-compatible) -> all INERT.
    guild::gui::SetNetModeHooks(nullptr);
}

} // namespace guild::world
