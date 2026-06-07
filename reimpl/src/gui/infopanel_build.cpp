#include "gui/infopanel_build.h"
#include "gui/object.h" // g_widgets (dword_69FFB4) — reused, never redefined

namespace guild::gui {

// ---------------------------------------------------------------------------
// Slot globals + host wiring.
// ---------------------------------------------------------------------------
InfoPanelState g_infoPanel;

void ResetInfoPanelBuild() {
    g_infoPanel = InfoPanelState{};
}

namespace {
InfoPanelHost  g_defaultHost;
InfoPanelHost* g_host = &g_defaultHost;
} // namespace

void InfoPanel_SetHost(InfoPanelHost* host) {
    g_host = host ? host : &g_defaultHost;
}

namespace {

// The original computes the icon-row Y from the current window's packed height:
//   ((*(int*)((char*)&dword_67EB84[238*win] + 2) >> 16) - 48) >> 1
// We surface that through the host (CurrentWindowHalfHeight) so the geometry edge is
// mockable; the "- 1" / "+ 47" / "- 8" adjustments stay inline at each call site.
int HalfHeight() { return g_host->CurrentWindowHalfHeight(); }

// Mark a freshly-added object as a caption / clickable widget:
//   *(_DWORD *)(dword_69FFB4 + 740*id + off) = 1
void SetWidgetFlag(int id, int byteOff) {
    if (id >= 0 && id < kMaxWidgets)
        g_widgets[id].at<i32>(byteOff) = 1;
}

// Stash a scene-reference pointer at widget +736 (the tooltip/back reference).
void SetWidgetRef736(int id, i32 ref) {
    if (id >= 0 && id < kMaxWidgets)
        g_widgets[id].at<i32>(736) = ref;
}

} // namespace

// ---------------------------------------------------------------------------
// gilde.exe 0x4b6454 — VIBE_InfoPanel_AddIconSprite.
//   v2 = VIBE_Widget_AddSpriteToWindow(a1 + 3, a2, 180, dword_62D230);
//   VIBE_Object_SetColor(v2, 67);
//   *(_DWORD *)(740 * v2 + dword_69FFB4 + 88) = 1;
//   VIBE_Widget_SetTextColor(v2, 96);
// (The decompile's v3/v4 are the same widget index recovered after the call.)
// ---------------------------------------------------------------------------
int InfoPanel_AddIconSprite(int x, int y, int gfxId) {
    int w = g_host->AddSprite(x + 3, y, gfxId);
    g_host->SetColor(w, 67);
    SetWidgetFlag(w, 88);
    g_host->SetTextColor(w, 96);
    return w;
}

namespace {

// The action-sprite pair every builder places: an AddSprite at (3, y) coloured 67, with a
// +88 caption flag and text-colour 96.  Returns the widget id.
int AddActionSprite(int y, int gfxId) {
    int w = g_host->AddSprite(3, y, gfxId);
    g_host->SetColor(w, 67);
    SetWidgetFlag(w, 88);
    g_host->SetTextColor(w, 96);
    return w;
}

} // namespace

// ---------------------------------------------------------------------------
// gilde.exe 0x4b6930 — VIBE_InfoPanel_BuildObject.
// Only builds when the panel is free (dword_631768 == -1) and the object code is NOT in
// [146,151] (those are handled elsewhere).  Loads infopanel_gebaeude, selects window 1,
// renders the header, selects window 0, places the 72-icon, and — for class 2/6 objects —
// the building/room action sprites.
// ---------------------------------------------------------------------------
int InfoPanel_BuildObject(const InfoObjectRecord& obj,
                          const InfoObjectRecord* room,
                          const InfoObjectRecord* building,
                          bool ownerMatches, bool slotsAfter) {
    // if ( dword_631768 == -1 && (*result < 146 || *result > 151) )
    if (g_infoPanel.form != -1)
        return -1;
    if (obj.code >= 146 && obj.code <= 151)
        return -1;

    g_infoPanel.form = g_host->LoadForm(kFormPanelBuilding);
    g_host->SelectWindow(g_infoPanel.form, 1);

    // v3 = room ? room : building ? building : nullptr  (dword_631744 / dword_631748)
    const InfoObjectRecord* host = room ? room : building;
    if (host) {
        if (host->customName && host->customName[0]) {
            // VIBE_Text_RenderRichString("$Z%s$A>%s<$A%s",
            //   14*host.code+1078, host.customName, 2*obj.code+2151);
            g_host->RenderRichString("$Z%s$A>%s<$A%s");
        } else {
            // "$Z%s$A%s", 14*host.code+1078, 2*obj.code+2151
            g_host->RenderRichString("$Z%s$A%s");
        }
    } else {
        // "$Z%s", 2*obj.code+2151
        g_host->RenderRichString("$Z%s");
    }

    g_host->SelectWindow(g_infoPanel.form, 0);

    // Icon: VIBE_Object_AddToWindow(win, 72, halfHeight-1, obj.code+206); +72 flag = 1.
    int icon = g_host->AddObject(72, HalfHeight() - 1, obj.code + kIconObjLoBias);
    SetWidgetFlag(icon, 72);

    // result = dword_13CE27C + 65*obj.code; class byte 2 or 6 -> the action-sprite block.
    int cls = obj.code; // the original reads *(_BYTE*)(objectBase + 65*code); modelled via flag below
    (void)cls;
    if (!(ownerMatches))
        return g_infoPanel.form; // class not 2/6 (or owner mismatch) -> no action sprites

    // The class-2/6 block: when the host building "owns" the object (v8==word_63CC5C or
    // selection-flag 0x10), place the action sprites (8CA3D4 + 8CA3D8); else just 8CA3D8.
    if (slotsAfter) {
        // VIBE_Building_CollectSlotsAfterObject -> place the 140 sprite first.
        g_infoPanel.sprite2 = AddActionSprite(140, /*dword_8CA3D4*/ 0);
    }
    g_infoPanel.sprite0 = AddActionSprite(166, /*dword_8CA3D8*/ 0);
    return g_infoPanel.form;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4b64b0 — VIBE_InfoPanel_BuildBuilding.
// ---------------------------------------------------------------------------
int InfoPanel_BuildBuilding(const InfoBuildingRecord& bld, int selectionFlags,
                            int category, bool itemIsOwn, bool noBuildingSelected) {
    int upgrade = g_host->BuildingUpgradeLevel(&bld);
    if (g_infoPanel.form != -1)
        return -1;

    g_infoPanel.form = g_host->LoadForm(kFormPanelBuilding);
    g_host->SelectWindow(g_infoPanel.form, 1);

    // Header.
    if (bld.code == kBuildingTypeFairground) {
        // "$Z%s$A%s", 14*code+1078, byte_13CD6A0[756*record[101]]
        g_host->RenderRichString("$Z%s$A%s");
    } else {
        // VIBE_Text_FormatItemLabelWithIcon(record[39], 2, buf, 1) then:
        if (bld.customName && bld.customName[0])
            g_host->RenderRichString("$Z%s$A%s$A>%s<");
        else
            g_host->RenderRichString("$Z%s$A%s");
    }

    g_host->SelectWindow(g_infoPanel.form, 0);

    // The building icon (object code+1010), +72 flag, and the +736 item back-reference.
    g_infoPanel.iconWidget = g_host->AddObject(72, HalfHeight(), bld.code + kIconObjBias);
    SetWidgetFlag(g_infoPanel.iconWidget, 72);
    // if ( item != 0xFFFF && item != *(u16*)dword_6498E4 ) +736 = &word_12CE910[268*item]
    if (bld.item != 0xFFFF)
        SetWidgetRef736(g_infoPanel.iconWidget, bld.item); // 268*item base resolved by runtime

    // Slider: skipped for fairground / well; else 0..100 with the upgrade level.
    if (bld.code == kBuildingTypeFairground || bld.code == kBuildingTypeWell) {
        g_infoPanel.slider = -1;
    } else {
        g_infoPanel.slider =
            g_host->AddSlider(21, 123, 0, 56, 100, 100, 1090);
        g_host->SetValueOrText(g_infoPanel.slider, 0, 100, upgrade, 0);
    }

    // Action sprites: skipped when (record[90]&1) set, or fairground/well.
    if (bld.noSlider || bld.code == kBuildingTypeFairground || bld.code == kBuildingTypeWell) {
        g_infoPanel.sprite2 = -1;
        g_infoPanel.sprite1 = -1;
    } else {
        // Primary action sprite: 8CA3C8 (own) vs 8CA3D0 (other) by the selection test.
        bool own = itemIsOwn || (selectionFlags & 0x10) != 0;
        g_infoPanel.sprite2 = AddActionSprite(140, own ? /*8CA3C8*/ 0 : /*8CA3D0*/ 1);

        // Secondary "demolish"-style sprite (unk_61DF3C), placed for certain categories
        // with no building currently selected.
        bool catWantsSecond = category == 1 || category == 2 || category == 4 ||
                              category == 7 || category == 8 || /*record byte==16*/ false;
        if (catWantsSecond && noBuildingSelected) {
            g_infoPanel.sprite1 = AddActionSprite(166, /*unk_61DF3C*/ 2);
        } else {
            g_infoPanel.sprite1 = -1;
        }
    }

    g_infoPanel.detail[0] = -1;
    return g_infoPanel.form;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4b6c80 — VIBE_InfoPanel_BuildTransporter.
// Only builds when the panel is free AND the owner resolves non-null.
// ---------------------------------------------------------------------------
int InfoPanel_BuildTransporter(const InfoObjectRecord& transporter, bool ownerResolved,
                               int loadPct, int sliderValue) {
    if (g_infoPanel.form != -1)
        return -1;
    if (!ownerResolved)
        return -1; // VIBE_GameObject_ResolveOwnerOrParentB returned 0

    g_infoPanel.form = g_host->LoadForm(kFormPanelTransporter);
    g_host->SelectWindow(g_infoPanel.form, 1);

    // VIBE_Text_RenderRichString("$Z%2N1$A%s", owner.code, 2*transporter.code+2151)
    g_host->RenderRichString("$Z%2N1$A%s");
    g_host->SelectWindow(g_infoPanel.form, 0);

    // The transporter icon (gfx 48, halfHeight-1, code+206); +72 flag.
    int icon = g_host->AddObject(48, HalfHeight() - 1, transporter.code + kIconObjLoBias);
    SetWidgetFlag(icon, 72);

    // The load slider (gfx 24 at 102), value from transporter[18], a2 forwarded.
    g_infoPanel.slider = g_host->AddSlider(24, 102, 0, 48, 100, 100, 1090);
    g_host->SetValueOrText(g_infoPanel.slider, 0, 100, sliderValue, loadPct);

    // VIBE_Form_SelectWindow(form, 2); dword_631784 = current window.
    g_host->SelectWindow(g_infoPanel.form, 2);
    g_infoPanel.transWindow = 0; // dword_631784 = dword_62D230 (current win); modelled as set
    return g_infoPanel.form;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4b6db8 — VIBE_InfoPanel_BuildStandard.
//   if ( building || room ):
//       if ( building && building.item == player ): own-building detail panel
//   else: the empty "standard" panel (two corner sprites).
// ---------------------------------------------------------------------------
int InfoPanel_BuildStandard(const InfoBuildingRecord* ownBuilding) {
    if (g_infoPanel.form != -1)
        return -1;

    if (ownBuilding) {
        // The own-building detail branch (building selected and item == player).
        int upgrade = g_host->BuildingUpgradeLevel(ownBuilding);
        g_infoPanel.form = g_host->LoadForm(kFormPanelBuilding);
        g_host->SelectWindow(g_infoPanel.form, 0);
        g_host->SelectWindow(g_infoPanel.form, 1);
        // FormatItemLabelWithIcon(item, 2, buf, 1); then:
        // "$Z%s$A%s$A%s", buf, 14*code+1078, customName
        g_host->RenderRichString("$Z%s$A%s$A%s");
        g_host->SelectWindow(g_infoPanel.form, 0);

        // Icon gfx 76 at halfHeight-1, code+1010.
        g_host->AddObject(76, HalfHeight() - 1, ownBuilding->code + kIconObjBias);
        // Slider gfx 21 at 122, upgrade level.
        g_infoPanel.slider = g_host->AddSlider(21, 122, 0, 48, 100, 100, 1090);
        g_host->SetValueOrText(g_infoPanel.slider, 0, 100, upgrade, 0);
        // Primary action sprite (8CA3C8 at 140).
        g_infoPanel.sprite2 = AddActionSprite(140, /*8CA3C8*/ 0);
        // Market (type 30) gets the extra sprite (8CA3CC at 166).
        if (ownBuilding->code == kBuildingTypeMarket)
            g_infoPanel.sprite3 = AddActionSprite(166, /*8CA3CC*/ 3);
        return g_infoPanel.form;
    }

    // The empty standard panel: load infopanel_standard, two corner sprites.
    g_infoPanel.form = g_host->LoadForm(kFormPanelStandard);
    g_host->SelectWindow(g_infoPanel.form, 0);
    g_infoPanel.sprite0 = AddActionSprite(140, /*8CA3C4*/ 4); // dword_63178C
    g_infoPanel.sprite0 = AddActionSprite(166, /*8CA3D8*/ 0); // dword_631790
    return g_infoPanel.form;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4b7104 — VIBE_InfoPanel_BuildPerson.
// ---------------------------------------------------------------------------
int InfoPanel_BuildPerson(const InfoPersonRecord& person) {
    if (g_infoPanel.form != -1)
        return -1;

    g_infoPanel.form = g_host->LoadForm(kFormPanelPerson);
    g_host->SelectWindow(g_infoPanel.form, 1);

    // if ( !person || personMode >= 2 ): the aggregate header "%i persons".
    if (person.aggregate) {
        // VIBE_Crt_Sprintf_0(buf, "%i %s", dword_6317B0, byte_61DFD0);
        // VIBE_Text_RenderRichString(buf);
        g_host->RenderRichString("%i %s");
        g_host->SelectWindow(g_infoPanel.form, 0);
        // No person -> add the placeholder portrait (object 1222) and return.
        // person -> portrait obj from person+396, +72 flag + the +736 back-ref.
        int half = HalfHeight();
        if (person.portraitObj == 0) {
            g_host->AddObject(56, half, 1222);
            return g_infoPanel.form;
        }
        g_infoPanel.personIcon = g_host->AddObject(56, half, person.portraitObj);
        SetWidgetFlag(g_infoPanel.personIcon, 72);
        SetWidgetRef736(g_infoPanel.personIcon, person.nameCode);
        return g_infoPanel.form;
    }

    // The per-person detail header: job label + output-ratio + "%s %i%%".
    //   v6 = ComputeOutputRatio(person) * dbl_61DFE8
    //   FormatItemLabelWithIcon(person.nameCode, ..., 4)
    //   RenderRichString("%s %i%%", buf, (int)v6)
    g_host->RenderRichString("%s %i%%");
    g_host->SelectWindow(g_infoPanel.form, 0);

    int half = HalfHeight();
    // The satisfaction slider (gfx 24 at 108), value = relation-matrix entry (caller-supplied).
    g_infoPanel.slider = g_host->AddSlider(24, 108, 0, 48, 100, 100, 1090);
    g_host->SetValueOrText(g_infoPanel.slider, -127, 127, person.satisfaction, 0);

    // class byte 5/6/7 -> an extra job icon (object person+84, gfx 48 at half-8).
    int c = person.classByte;
    if (c == 5 || c == 6 || c == 7) {
        g_host->AddObject(48, half - 8, person.job2 /* person+21 dword */);
    }

    // job group 10/11/12 -> two tiled rows (gfx 126/140) of the person's two need bytes.
    int grp = person.job1 ? person.job1 : person.job2;
    if (grp == 11 || grp == 12 || grp == 10) {
        g_host->AddObject(126, 25, 1406);
        // VIBE_Hud_BuildTiledRow(41, 126, person[130]) — modelled via the host AddObject row.
        g_host->AddObject(140, 25, 1407);
    }
    return g_infoPanel.form;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4b7468 (teardown tail) — reset the dword_631774[count] detail slots.
//   v19 = -1; do { if (v19 != slot[i]) { DestroyByType(slot[i], -1, ..); slot[i] = -1; } }
// ---------------------------------------------------------------------------
int InfoPanel_ResetDetailSlots(int count) {
    if (count > 4)
        count = 4;
    int cleared = 0;
    for (int i = 0; i < count; ++i) {
        if (g_infoPanel.detail[i] != -1) {
            g_host->DestroyWidget(g_infoPanel.detail[i]);
            g_infoPanel.detail[i] = -1;
            ++cleared;
        }
    }
    return cleared;
}

} // namespace guild::gui
