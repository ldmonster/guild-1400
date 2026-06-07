#include "play/hud_binder.h"

namespace guild::play {

using gui::kObjectStride;
using gui::kObjIdLo;
using gui::kObjIdHi;

void HudOverlayBinder::reset(const std::vector<std::uint8_t>& objectClasses) {
    // Bring every HUD gui table this binder writes back to its create-time state so
    // a bind pass starts from a clean, deterministic slot layout.
    gui::ResetStatusText();
    gui::ResetDamageLabels();
    gui::ResetPlayerBar();

    // Build the owned scene-object table: N records of kObjectStride (65) bytes, the
    // first byte of each is the object's class byte (32 = "object" builder). When no
    // classes are supplied, default to 8 plain objects so a hover always resolves.
    std::vector<std::uint8_t> classes = objectClasses;
    if (classes.empty())
        classes.assign(8, 32);

    objectCount_ = static_cast<int>(classes.size());
    objectTable_.assign(static_cast<std::size_t>(objectCount_) * kObjectStride, 0);
    for (int i = 0; i < objectCount_; ++i)
        objectTable_[static_cast<std::size_t>(i) * kObjectStride] = classes[i];
}

gui::TooltipTables HudOverlayBinder::tables() const {
    gui::TooltipTables t{};
    if (!objectTable_.empty())
        t.objectBase = objectTable_.data();
    return t;
}

gui::HudLabelLayout HudOverlayBinder::layoutCaption(gui::HudLabelAlign align,
                                                    int anchorX, gui::i16 width) {
    return gui::Hud_LabelLayout(align, anchorX, width);
}

int HudOverlayBinder::layoutButtonRow(const std::vector<int>& widths, int windowWidth,
                                      std::vector<int>& outX) {
    int count = static_cast<int>(widths.size());
    outX.assign(static_cast<std::size_t>(count), 0);
    if (count == 0)
        return 0;
    return gui::Hud_ButtonRowLayout(widths.data(), count, windowWidth, outX.data());
}

int HudOverlayBinder::registerStatus(int key, int tag) {
    return gui::StatusText_Register(key, tag);
}

int HudOverlayBinder::registerDamage(int source, int amount, int now) {
    return gui::DamageLabel_Register(source, amount, now);
}

std::vector<HudBarSlot> HudOverlayBinder::buildPlayerBar(
    const std::vector<std::uint16_t>& objIds) {
    std::vector<HudBarSlot> out;
    out.reserve(objIds.size());
    for (std::uint16_t id : objIds) {
        HudBarSlot s;
        s.objId     = id;
        s.slotIndex = gui::PlayerBar_AssignSlot(id);
        if (s.slotIndex >= 0)
            s.layout = gui::PlayerBar_SlotLayout(s.slotIndex);
        out.push_back(s);
    }
    return out;
}

gui::TooltipKind HudOverlayBinder::classifyHover(int sceneObjectIndex, int tooltipId,
                                                 gui::TooltipSubject& outSubject) const {
    gui::TooltipTables t = tables();
    const gui::u8* sceneRef = nullptr;
    if (sceneObjectIndex >= 0 && sceneObjectIndex < objectCount_ && !objectTable_.empty()) {
        // The widget's +736 scene reference points at the hovered object's record.
        sceneRef = objectTable_.data() +
                   static_cast<std::size_t>(sceneObjectIndex) * kObjectStride;
    }
    outSubject = gui::Tooltip_ClassifySubject(t, sceneRef, tooltipId);
    return outSubject.kind;
}

int HudOverlayBinder::TooltipTextId(int tooltipId) {
    // The id-fallback object range the HUD maps a hovered widget id into an object
    // code (status-text id): [206,1010) -> id-206, otherwise no object (0).
    if (tooltipId >= kObjIdLo && tooltipId < kObjIdHi)
        return tooltipId - kObjIdLo;
    return 0;
}

HudOverlay HudOverlayBinder::bind(int hoverSceneObject, int hoverTooltipId,
                                  const std::vector<std::uint16_t>& ownedObjects) {
    HudOverlay ov;

    // Caption: a centered title at the top-centre of a 640-wide window.
    ov.caption = layoutCaption(gui::HudLabelAlign::kCentered, /*anchorX=*/320, /*width=*/80);

    // Action button row: three measured buttons across the window.
    std::vector<int> widths = {40, 60, 50};
    ov.buttonRowPitch = layoutButtonRow(widths, /*windowWidth=*/640, ov.buttonRowX);

    // Player bar: assign + lay out each owned object at the bottom strip.
    ov.barSlots = buildPlayerBar(ownedObjects);

    // Status text: register one selection-status entry per owned object (de-duped by
    // key), so the real status-text de-dup/LRU table code runs over real content.
    for (std::uint16_t id : ownedObjects)
        ov.statusSlots.push_back(registerStatus(/*key=*/id, /*tag=*/100));

    // Damage label: one floating label for the hovered/selected object.
    ov.damageSlot = registerDamage(/*source=*/1, /*amount=*/5, /*now=*/0);

    // Hovered subject: classify the hovered scene object (or id-fallback) to a builder.
    gui::TooltipSubject subj{};
    ov.hoverKind    = classifyHover(hoverSceneObject, hoverTooltipId, subj);
    ov.hoverObjCode = subj.objectCode;
    ov.hoverTextId  = TooltipTextId(hoverTooltipId);

    return ov;
}

} // namespace guild::play
