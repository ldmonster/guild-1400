// Headless e2e for the native player wizard (VIBE_Menu_RunChoosePlayer @0x52ccd8): a
// per-frame scripted platform types a name, advances the text pages with Enter, clicks the
// gender / faith rows and a wappen cell, and the wizard commits with the collected identity.
// Proves the SDL text-input plumbing (pollText) + the page input mapping end to end.
#include "tests/framework/test.h"
#include "play/sdl_chooseplayer_screen.h"
#include "shim_impl/memory_graphics.h"
#include "shim/IPlatform.h"
#include <string>
#include <vector>
using namespace guild;

namespace {
struct Frame { int mx=0, my=0; bool left=false, ret=false, esc=false, back=false; std::string text; };
// A per-frame scripted platform: pumpMessages advances one frame; getMouse/keyDown/pollText
// report the current frame so the screen sees precise per-frame input edges.
struct FramePlatform : shim::IPlatform {
    std::vector<Frame> frames; int i = -1;
    bool createMainWindow(const char*,int,int,bool) override { return true; }
    void destroyMainWindow() override {}
    bool pumpMessages() override { ++i; return i < (int)frames.size(); }
    std::uint32_t timeMs() override { return (std::uint32_t)(i<0?0:i)*16u; }
    void sleepMs(std::uint32_t) override {}
    void getMouse(shim::MouseState& o) override { const Frame& f=cur(); o.x=f.mx; o.y=f.my; o.left=f.left; }
    bool keyDown(int vk) override { const Frame& f=cur();
        if (vk==0x0D) return f.ret; if (vk==0x1B) return f.esc; if (vk==0x08) return f.back; return false; }
    std::string pollText() override { std::string s=cur().text; const_cast<Frame&>(cur()).text.clear(); return s; }
    const Frame& cur() const { static Frame empty; int j=(i<0)?0:i; return j<(int)frames.size()?frames[j]:empty; }
};
} // namespace

TEST(ChoosePlayerScreen, ScriptedWalkthroughCommits) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(800, 600, 32, false));
    // Row/cell hit coords matching the screen's 800x600 layout (panel 128,72,441,490).
    const int rowX=168, rowMidX=348, rowsY=192, rowH=46;       // radio rows
    auto rowY=[&](int r){ return rowsY + r*rowH + (rowH-8)/2; };
    const int gx=158, gy=202, cw=95, chh=70;                   // wappen cells (4 cols)
    auto cellX=[&](int i){ return gx+(i%4)*cw+(cw-8)/2; };
    auto cellY=[&](int i){ return gy+(i/4)*(chh+10)+chh/2; };
    (void)rowX;

    FramePlatform plat;
    plat.frames = {
        Frame{0,0,false,false,false,false,"Hans"},          // f0 page0: type name
        Frame{0,0,false,true, false,false,""},               // f1 page0: Enter -> page1
        Frame{0,0,false,false,false,false,"Fugger"},         // f2 page1: type family (ret released)
        Frame{0,0,false,true, false,false,""},               // f3 page1: Enter -> page2
        Frame{rowMidX,rowY(1),true,false,false,false,""},    // f4 page2: click gender row1 (female)
        Frame{rowMidX,rowY(1),false,false,false,false,""},   // f5 release
        Frame{rowMidX,rowY(0),true,false,false,false,""},    // f6 page3: click faith row0
        Frame{rowMidX,rowY(0),false,false,false,false,""},   // f7 release
        Frame{cellX(3),cellY(3),true,false,false,false,""},  // f8 page4: click wappen cell 3
        Frame{0,0,false,false,false,false,""},               // f9 page5: auto-commit
        Frame{0,0,false,false,false,false,""},               // f10 spare
    };

    play::ChoosePlayerConfig cfg; cfg.fbW=800; cfg.fbH=600; cfg.frameCapMs=0; cfg.maxFrames=64;
    cfg.seedFirstName = ""; cfg.seedFamilyName = "";   // empty fields -> the typed text is exact
    auto r = play::RunChoosePlayerScreen(dev, plat, cfg);

    CHECK(r.confirmed);
    CHECK_EQ(r.firstName, std::string("Hans"));
    CHECK_EQ(r.familyName, std::string("Fugger"));
    CHECK_EQ(r.gender, 1);          // clicked female row
    CHECK_EQ(r.faith, 0);           // clicked catholic row
    CHECK_EQ(r.wappenIndex, 3);     // clicked wappen cell 3
    CHECK_EQ(r.pageReached, 5);
    CHECK(r.framesPresented > 0);
}
