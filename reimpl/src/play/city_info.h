#pragma once
// =============================================================================
// guild::play — ChooseCity INFO WINDOW: the real per-city description card.
//
// VIBE_Menu_RunChooseCity @0x52e6d8, on selection change, renders into the
// "Menu\CHOOSECITY" form window the picked city's name ($C) plus its description
// text (_STADTAUSWAHL_<CITY>_BESCHR) and info line (_STADTAUSWAHL_<CITY>_INFO),
// looked up in the localized text DB (VIBE_Text_FindTextArrayIndex). The BESCHR
// string is rich-text markup: $[title$] then $N, then rows of
// "$B label : $N> value $A $0>" (a two-column card: label left, value at a tab).
//
// This module loads that REAL data (textbin_<lang>.BIN -> gui::text::TextDb via
// the reconstructed BuildTextArray) and renders the card into an info-window panel
// with render::DrawTextCp1251 (so the localized — here Russian CP1251 — text shows
// in the reconstructed bitmap font). Host-side composition of the engine's form/
// rich-text path; the data + markup are real.
// =============================================================================
#include "gui/text/textdb.h"
#include "render/gfx_archive.h"

#include <map>
#include <memory>
#include <string>

namespace guild::shim { class IFileSystem; }
namespace guild::render { struct Surface; }

namespace guild::play {

// Loads + caches the localized text DB and resolves per-city ChooseCity strings.
class CityInfoText {
public:
    // Mount `Resources/<archive>` (default textbin_deutsch.BIN, the descriptions'
    // home) and load every ".res" member into the text DB. Returns true on success.
    bool Load(shim::IFileSystem* fs,
              const char* archive = "Resources/textbin_deutsch.BIN");

    bool loaded() const { return loaded_; }
    int  entryCount() const { return db_.Count(); }

    // The city's _STADTAUSWAHL_<CITY>_BESCHR description (rich-text markup), or ""
    // if absent. `city` is matched case-insensitively (key is upper-cased).
    std::string Beschr(const std::string& city) const;
    // The city's _STADTAUSWAHL_<CITY>_INFO line, or "".
    std::string Info(const std::string& city) const;

private:
    gui::text::TextDb db_;
    bool loaded_ = false;
};

// The real ChooseCity info-window art from gilde.gfx (the textures the original
// blits): the parchment panel `_PERGAMENT_MB`, the per-city crest
// `_STADTWAPPEN_<CITY>`, and the choose button `_AUSWAHL` (gfx record 1210 — the
// confirm id dword_75BF38 == 1210). Decoded shapes are cached.
class CityInfoGfx {
public:
    bool Load(shim::IFileSystem* fs, const char* archive = "gfx/gilde.gfx");
    bool loaded() const { return loaded_; }
    const render::DecodedShape* Panel();                 // _PERGAMENT_MB
    // _BUTTON_RED frames (the red main-menu button): 0 = left cap, 1 = right cap,
    // 2 = stretchable centre face — composed into the choose button.
    const render::DecodedShape* ButtonShape(int shape);
    const render::DecodedShape* Crest(const std::string& city);  // _STADTWAPPEN_<CITY>
private:
    const render::DecodedShape* Decode(const std::string& name, int shape = 0);
    render::GfxArchive arc_;
    bool loaded_ = false;
    std::map<std::string, std::unique_ptr<render::DecodedShape>> cache_;
};

// The on-screen rectangle of the rendered info window + its choose button (device
// pixels), so the caller can hit-test the button. `valid` is false if nothing drew.
struct InfoWindowLayout {
    int panelX = 0, panelY = 0, panelW = 0, panelH = 0;
    int btnX = 0, btnY = 0, btnW = 0, btnH = 0;
    bool valid = false;
};

// Render the ChooseCity info window 1:1 at the form's bottom-centre position
// (Menu/CHOOSECITY window 0 = (120,392,548,204) on the 800x600 layout, scaled to
// fbW x fbH): the real `_PERGAMENT_MB` parchment panel, the city's `_STADTWAPPEN`
// crest, the BESCHR description (dark text on parchment via the markup layout), and
// the `_AUSWAHL` choose button on the bottom strip. Returns the button rect for the
// caller to hit-test (click => confirm). Falls back to a framed panel if gfx absent.
InfoWindowLayout RenderCityInfoWindow(render::Surface* fb, int fbW, int fbH,
                                      const CityInfoText& text, CityInfoGfx& gfx,
                                      const std::string& cityName);

// Render the ChooseCity info-window card at panel rect (x,y,w,h) onto `fb`
// (16/32 bpp): a framed panel, then the BESCHR rich-text laid out 1:1 with the
// engine markup — $[title$] as a highlighted header, $N/$A newlines, $N> column
// tabs (label left / value column). `title` (the ASCII city name, the engine's $C)
// is shown as the panel caption when non-empty. A null/empty beschr draws just the
// framed panel + caption.
void RenderCityInfoCard(render::Surface* fb, int x, int y, int w, int h,
                        const std::string& title, const std::string& beschr);

} // namespace guild::play
