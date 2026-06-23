// guild::play — native player-identity wizard (VIBE_Menu_RunChoosePlayer @0x52ccd8).
// Drives the headless page state machine gui::Menu_RunChoosePlayer through hooks and renders
// the six pages (text entry via SDL text-input, radios, wappen grid) with the real labels.
#include "play/sdl_chooseplayer_screen.h"

#include "shim/IGraphicsDevice.h"
#include "shim/IPlatform.h"
#include "shim_impl/disk_filesystem.h"
#include "io/archive_mount.h"
#include "gui/text_load.h"
#include "gui/text/textdb.h"
#include "gui/menu_render.h"
#include "play/menu_assets.h"
#include "render/text_cp1251.h"
#include "render/gfx_archive.h"
#include "render/types.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace guild::play {

namespace {
constexpr int kVkEsc = 0x1B, kVkReturn = 0x0D, kVkBack = 0x08;

void BlitToDevice(const std::uint32_t* src, int w, int h, shim::IGraphicsDevice& dev) {
    shim::Surface* bb = dev.backbuffer();
    if (!bb || !bb->pixels) return;
    const int W = bb->width < w ? bb->width : w, H = bb->height < h ? bb->height : h;
    auto* base = static_cast<std::uint8_t*>(bb->pixels);
    if (bb->bpp == 32) {
        for (int y = 0; y < H; ++y)
            std::memcpy(base + (std::size_t)y * bb->pitch, src + (std::size_t)y * w, (std::size_t)W * 4);
    } else if (bb->bpp == 16) {
        for (int y = 0; y < H; ++y) {
            auto* row = reinterpret_cast<std::uint16_t*>(base + (std::size_t)y * bb->pitch);
            const std::uint32_t* s = src + (std::size_t)y * w;
            for (int x = 0; x < W; ++x) { const std::uint32_t c = s[x];
                row[x] = (std::uint16_t)((((c>>16&0xFF)>>3)<<11)|(((c>>8&0xFF)>>2)<<5)|((c&0xFF)>>3)); }
        }
    }
}
void DrawBackground(std::uint32_t* dst, int W, int H, const std::uint32_t* bg, int bw, int bh) {
    if (!bg || bw <= 0 || bh <= 0) return;
    for (int y = 0; y < H; ++y) { const int sy=(int)((std::int64_t)y*bh/H);
        for (int x = 0; x < W; ++x) dst[(std::size_t)y*W+x]=bg[(std::size_t)sy*bw+(int)((std::int64_t)x*bw/W)]; }
}
void Outline(std::uint32_t* px, int W, int H, int x, int y, int w, int h, std::uint32_t c) {
    auto put=[&](int X,int Y){ if(X>=0&&X<W&&Y>=0&&Y<H) px[(std::size_t)Y*W+X]=c; };
    for (int X=x; X<x+w; ++X){put(X,y);put(X,y+h-1);} for (int Y=y; Y<y+h; ++Y){put(x,Y);put(x+w-1,Y);}
}
int TW(const std::string& s){ return (int)s.size()*6; }
void DrawC(render::Surface* s,int cx,int y,const std::string& t,int r,int g,int b){
    render::DrawTextCp1251(s, cx-TW(t)/2, y, t.c_str(), (guild::u8)r,(guild::u8)g,(guild::u8)b); }

// Strip `$X` control tokens (and `$[`/`$]`) from a markup string -> plain text; if a
// `$[heading]` is present, return just the heading.
std::string StripMarkup(const std::string& s) {
    std::string br; std::size_t bo = s.find("$[");
    if (bo != std::string::npos) { std::size_t e = s.find("$]", bo+2);
        if (e != std::string::npos) return s.substr(bo+2, e-(bo+2)); }
    std::string out;
    for (std::size_t i=0; i<s.size(); ) {
        if (s[i]=='$' && i+1<s.size()) { i+=2; continue; }
        if (s[i]=='%' && i+1<s.size()) { i+=2; continue; }
        out += s[i++];
    }
    std::size_t a=out.find_first_not_of(" \t\r\n"), b=out.find_last_not_of(" \t\r\n");
    return (a==std::string::npos)?std::string():out.substr(a,b-a+1);
}

struct Labels {
    std::string heroHeading="Your hero", nameLabel="Name", familyHeading="Family name?", familyLabel="Family";
    std::string genderHeading="Gender", genderOpt[2]={"Male","Female"};
    std::string faithHeading="Faith",  faithOpt[2]={"Catholic","Cathar"};
    std::string wappenHeading="Choose your colours!";
    bool real=false;
};
bool LoadLabels(const std::string& gameDir, Labels& L) {
    if (gameDir.empty()) return false;
    shim::DiskFileSystem fs(gameDir);
    if (!fs.exists("Resources/textbin_deutsch.BIN")) return false;
    io::ArchiveMount m; if (!m.Mount(&fs,"Resources/textbin_deutsch.BIN",true)) return false;
    gui::text::TextDb db;
    for (const io::ArchiveMember& e : m.members()) { const std::string& n=e.name;
        if (n.size()<4) continue; std::string ext=n.substr(n.size()-4);
        for(auto&c:ext)c=(char)std::tolower((unsigned char)c); if(ext!=".res") continue;
        std::vector<u8> b; if(m.OpenMember(n.c_str(),b)&&!b.empty()) gui::text::BuildTextArray(b.data(),b.size(),db); }
    auto get=[&](const char* key)->std::string{ int i=db.FindIndex(key); return (i>=0&&db.Text(i))?db.Text(i):""; };
    std::string s;
    if (!(s=get("_M0_PERSOENLICH_VORNAME+0")).empty())      L.heroHeading=StripMarkup(s);
    if (!(s=get("_M0_PERSOENLICH_VORNAME_SUB0+0")).empty()) L.nameLabel=StripMarkup(s);
    if (!(s=get("_M0_PERSOENLICH_VORNAME_SUB1+0")).empty()) L.familyLabel=StripMarkup(s);
    if (!(s=get("_M0_PERSOENLICH_VORNAME+2")).empty())      L.familyHeading=StripMarkup(s);
    if (!(s=get("_M0_PERSOENLICH_GESCHLECHT+0")).empty())   L.genderHeading=StripMarkup(s);
    if (!(s=get("_M0_PERSOENLICH_GESCHLECHT+1")).empty())   L.genderOpt[0]=StripMarkup(s);
    if (!(s=get("_M0_PERSOENLICH_GESCHLECHT+2")).empty())   L.genderOpt[1]=StripMarkup(s);
    if (!(s=get("_M0_PERSOENLICH_GLAUBE+0")).empty())       L.faithHeading=StripMarkup(s);
    if (!(s=get("_M0_PERSOENLICH_GLAUBE+1")).empty())       L.faithOpt[0]=StripMarkup(s);
    if (!(s=get("_M0_PERSOENLICH_GLAUBE+2")).empty())       L.faithOpt[1]=StripMarkup(s);
    if (!(s=get("_M0_PERSOENLICH_WAPPEN+0")).empty())       L.wappenHeading=StripMarkup(s);
    L.real = !L.heroHeading.empty();
    return L.real;
}
} // namespace

std::size_t ApplyTextEdit(std::string& buf, const std::string& typed, bool backspaceEdge, std::size_t maxLen) {
    for (char c : typed) {
        if ((unsigned char)c >= 0x20 && buf.size() < maxLen) buf += c;   // printable
    }
    if (backspaceEdge && !buf.empty()) buf.pop_back();
    return buf.size();
}

namespace {
using namespace guild::gui;

// The native front: a ChoosePlayerRunHooks that renders + reads input per page.
struct PlayerScreenHooks : ChoosePlayerRunHooks {
    shim::IGraphicsDevice* dev; shim::IPlatform* plat; const ChoosePlayerConfig* cfg;
    Labels lab; MenuAssets* assets; bool haveAssets=false;
    std::vector<std::uint32_t> scratch;
    int W, H;
    std::string firstBuf, familyBuf;
    int gender=0, faith=0, wappen=0;
    bool prevLeft=false, prevEnter=false, prevEsc=false, prevBack=false;
    int frames=0; bool quitByWindow=false;

    int FormLoad(const char*) override { return 1; }
    void FormDestroy(int) override {}
    void ReadIniDefaults(ChoosePlayerState& st) override {
        st.firstName = cfg->seedFirstName; st.familyName = cfg->seedFamilyName;
        st.gender = cfg->seedGender; st.faith = cfg->seedFaith; st.wappenIndex = cfg->seedWappen;
        firstBuf = st.firstName; familyBuf = st.familyName;
        gender = st.gender; faith = st.faith; wappen = st.wappenIndex;
    }
    void WriteIni(const ChoosePlayerState&) override {}   // persistence deferred (flows downstream)

    int RunFrameLoop(int frame) override {
        frames = frame + 1;
        if (!plat->pumpMessages()) { quitByWindow = true; return 0; }
        return 1;
    }

    void panel(int& px,int& py,int& pw,int& ph,int& cx) {
        px=128*W/800; py=72*H/600; pw=441*W/800; ph=490*H/600; cx=px+pw/2;
    }
    PlayerPageAction PageAction(int page, int /*frame*/) override {
        int px,py,pw,ph,cx; panel(px,py,pw,ph,cx);
        // ---- render ----
        std::fill(scratch.begin(), scratch.end(), 0u);
        gui::MenuRenderTarget t = gui::MenuRenderTarget::Wrap(scratch.data(), W, H, W*4);
        if (haveAssets) DrawBackground(scratch.data(),W,H,assets->background().data(),assets->backgroundWidth(),assets->backgroundHeight());
        else gui::MenuFillRect(&t.surf,0,0,W,H,16,18,32);
        gui::MenuFillRect(&t.surf,px,py,pw,ph,34,26,16); Outline(scratch.data(),W,H,px,py,pw,ph,0xFFB08C46u);
        std::string pageTag = "[ " + std::to_string(page+1) + " / 6 ]";
        DrawC(&t.surf,cx,py+12,pageTag,150,140,120);

        shim::MouseState ms{}; plat->getMouse(ms);
        const int rowsY=py+120, rowH=46, rowW=pw-80, rowX=px+40;
        auto rowRect=[&](int i,int&rx,int&ry,int&rw,int&rh){ rx=rowX; ry=rowsY+i*rowH; rw=rowW; rh=rowH-8; };
        auto hitRow=[&](int n)->int{ for(int i=0;i<n;++i){int rx,ry,rw,rh;rowRect(i,rx,ry,rw,rh);
            if(ms.x>=rx&&ms.x<rx+rw&&ms.y>=ry&&ms.y<ry+rh)return i;} return -1; };
        auto drawField=[&](const std::string& label,const std::string& val){
            DrawC(&t.surf,cx,py+150,label+":",230,220,200);
            int fx=px+50,fy=py+180,fw=pw-100,fh=40;
            gui::MenuFillRect(&t.surf,fx,fy,fw,fh,20,16,10); Outline(scratch.data(),W,H,fx,fy,fw,fh,0xFFFFD060u);
            std::string shown=val+"_";   // caret
            render::DrawTextCp1251(&t.surf,fx+10,fy+fh/2-3,shown.c_str(),255,240,180);
        };
        auto drawRows=[&](const std::string opts[],int n,int sel){
            for(int i=0;i<n;++i){int rx,ry,rw,rh;rowRect(i,rx,ry,rw,rh);bool hov=(i==hitRow(n));bool s=(i==sel);
                gui::MenuFillRect(&t.surf,rx,ry,rw,rh, s?90:hov?64:44, s?60:hov?50:36, s?28:hov?30:24);
                Outline(scratch.data(),W,H,rx,ry,rw,rh,(hov||s)?0xFFFFD060u:0xFF6A5630u);
                DrawC(&t.surf,cx,ry+rh/2-3,opts[i],(hov||s)?255:226,(hov||s)?240:210,(hov||s)?170:180);} };

        // edges
        bool left=ms.left, leftEdge=left&&!prevLeft; prevLeft=left;
        bool ent=plat->keyDown(kVkReturn), entEdge=ent&&!prevEnter; prevEnter=ent;
        bool esc=plat->keyDown(kVkEsc), escEdge=esc&&!prevEsc; prevEsc=esc;
        bool bk=plat->keyDown(kVkBack), bkEdge=bk&&!prevBack; prevBack=bk;

        PlayerPageAction act = PlayerPageAction::kNone;
        switch (page) {
            case 0: DrawC(&t.surf,cx,py+60,lab.heroHeading,255,226,150);
                    ApplyTextEdit(firstBuf, plat->pollText(), bkEdge); drawField(lab.nameLabel,firstBuf);
                    if (entEdge && !firstBuf.empty()) act=PlayerPageAction::kAdvance; break;
            case 1: DrawC(&t.surf,cx,py+60,lab.familyHeading,255,226,150);
                    ApplyTextEdit(familyBuf, plat->pollText(), bkEdge); drawField(lab.familyLabel,familyBuf);
                    if (entEdge) act=PlayerPageAction::kAdvance; break;
            case 2: { DrawC(&t.surf,cx,py+60,lab.genderHeading,255,226,150); drawRows(lab.genderOpt,2,gender);
                    if(leftEdge){int h=hitRow(2); if(h>=0){gender=h;act=PlayerPageAction::kAdvance;}} } break;
            case 3: { DrawC(&t.surf,cx,py+60,lab.faithHeading,255,226,150); drawRows(lab.faithOpt,2,faith);
                    if(leftEdge){int h=hitRow(2); if(h>=0){faith=h;act=PlayerPageAction::kAdvance;}} } break;
            case 4: { DrawC(&t.surf,cx,py+60,lab.wappenHeading,255,226,150);
                    // 8 wappen cells (2 rows x 4), real gfx 1342+i when present else numbered.
                    const int cols=4,cw=(pw-60)/cols,chh=70,gx=px+30,gy=py+130;
                    int hovCell=-1;
                    for(int i=0;i<8;++i){int cxx=gx+(i%cols)*cw,cyy=gy+(i/cols)*(chh+10);
                        if(ms.x>=cxx&&ms.x<cxx+cw-8&&ms.y>=cyy&&ms.y<cyy+chh)hovCell=i;}
                    for(int i=0;i<8;++i){int cxx=gx+(i%cols)*cw,cyy=gy+(i/cols)*(chh+10);bool s=(i==wappen),hov=(i==hovCell);
                        gui::MenuFillRect(&t.surf,cxx,cyy,cw-8,chh, s?90:hov?64:44, s?60:hov?50:36, s?28:hov?30:24);
                        Outline(scratch.data(),W,H,cxx,cyy,cw-8,chh,(s||hov)?0xFFFFD060u:0xFF6A5630u);
                        const render::DecodedShape* sp = assets? assets->SpriteForGfxId(1342+i):nullptr;
                        if(sp) BlitDecodedSprite(&t.surf,cxx+(cw-8)/2-sp->width/2,cyy+chh/2-sp->height/2,*sp);
                        else { std::string n=std::to_string(i+1); DrawC(&t.surf,cxx+(cw-8)/2,cyy+chh/2-3,n,230,220,200);} }
                    if(leftEdge && hovCell>=0){wappen=hovCell;act=PlayerPageAction::kAdvance;} } break;
            default: break;
        }
        if (escEdge) act = PlayerPageAction::kBack;

        BlitToDevice(scratch.data(),W,H,*dev); dev->present();
        if (cfg->frameCapMs>0) plat->sleepMs((std::uint32_t)cfg->frameCapMs);
        return act;
    }
    std::string GetText(int page) override { return page==0 ? firstBuf : familyBuf; }
    int GetChoice(int page) override { return page==2 ? gender : page==3 ? faith : wappen; }
};
} // namespace

ChoosePlayerScreenResult RunChoosePlayerScreen(shim::IGraphicsDevice& device, shim::IPlatform& plat,
                                               const ChoosePlayerConfig& cfg) {
    ChoosePlayerScreenResult res;
    PlayerScreenHooks hk;
    hk.dev = &device; hk.plat = &plat; hk.cfg = &cfg;
    hk.W = cfg.fbW; hk.H = cfg.fbH; hk.scratch.assign((std::size_t)cfg.fbW*cfg.fbH, 0u);
    res.usedRealText = LoadLabels(cfg.gameDir, hk.lab);

    shim::DiskFileSystem assetFs(cfg.gameDir);
    MenuAssets assets; hk.haveAssets = !cfg.gameDir.empty() && assets.Load(assetFs); hk.assets = &assets;

    gui::ChoosePlayerRunHooks* prev = gui::Menu_SetChoosePlayerHooks(&hk);
    gui::ChoosePlayerState st; gui::ChoosePlayerRecord rec;
    int r = gui::Menu_RunChoosePlayer(st, &rec, cfg.maxFrames);
    gui::Menu_SetChoosePlayerHooks(prev);

    res.confirmed = (r == 1);
    res.back = (r == 0);
    res.firstName = st.firstName; res.familyName = st.familyName;
    res.gender = st.gender; res.faith = st.faith; res.wappenIndex = st.wappenIndex;
    res.framesPresented = hk.frames; res.pageReached = rec.maxPageReached;
    res.quitByWindow = hk.quitByWindow;
    return res;
}

} // namespace guild::play
