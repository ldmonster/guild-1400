// building_upgrade_window — gilde.exe 0x594100 VIBE_Building_OpenUpgradeTreeWindow.
// 1:1 control flow; the .form/window/GPU plumbing is routed through UpgradeWindowHooks
// (the SDL/Vulkan boundary, rules 3-4) and the layered DAG layout is REUSED from
// world::RoadComputeNetworkLayout (wave-21 road_network). The genuine logic kept here
// is the dispatch + the edge-draw geometry loop. See the header for the byte map.
#include "sim/building_upgrade_window.h"

#include "world/road_network.h"      // RoadComputeNetworkLayout / RoadLayoutState (REUSE, extern)

namespace guild::sim {

static const UpgradeWindowHooks kInert{};
static const UpgradeWindowHooks* g_huw = &kInert;

void SetUpgradeWindowHooks(const UpgradeWindowHooks* hooks) { g_huw = hooks ? hooks : &kInert; }
const UpgradeWindowHooks& GetUpgradeWindowHooks() { return *g_huw; }

// gilde.exe dword_13CE288 — the upgrade-tree window handle slot.
static int g_upgradeWindowHandle = 0;
void SetUpgradeWindowHandle(int handle) { g_upgradeWindowHandle = handle; }
int  UpgradeWindowHandle() { return g_upgradeWindowHandle; }

int Building_OpenUpgradeTreeWindow(const i8* typeRecord, const i16* targetIdPtr,
                                   const char* formName) {
    const UpgradeWindowHooks* H = g_huw;

    // --- 1. open the .form window (0x594116) ---
    int handle = H->formOpen ? H->formOpen(formName) : 0;
    SetUpgradeWindowHandle(handle);                       // dword_13CE288 = ...

    // --- 2. centre / context / select / title (0x59411b..0x59419d) ---
    if (H->formCenter) H->formCenter(handle);             // Form_CenterChildWindows
    if (H->contextSnapshot) H->contextSnapshot();         // dword_13CE280 = dword_62EB38
    if (H->formSelectWindow) H->formSelectWindow(0, kUpgradeFormSelectA); // (0,1)
    if (H->renderRichTitle) H->renderRichTitle(kUpgradeRichTitleId);      // "$[%s$]",25
    if (H->formSelectWindow) H->formSelectWindow(0, kUpgradeFormSelectB); // (0,3)

    // --- 3. lay out the upgrade DAG (0x5941b4) ---
    int width = 0, height = 0;
    if (H->layoutExtent) H->layoutExtent(&width, &height);// rect.right>>16, rect.bottom>>16
    const u8* typeTable = H->typeTableBase ? H->typeTableBase() : nullptr; // dword_13CE294
    const u8* typeFlag  = H->typeFlagBase  ? H->typeFlagBase()  : nullptr; // dword_13CE27C

    world::RoadLayoutState st{};   // value-init: the unmirrored tail records read 0
    // Headless boundary: with no game-data type tables installed there is no tree to
    // lay out — take the original's empty-tree path (Form_Destroy -> -1) rather than
    // dereferencing the absent table (the original is always called with live data).
    int empty = (typeTable && typeFlag && targetIdPtr)
        ? world::RoadComputeNetworkLayout(st, typeTable, typeFlag,
                                          typeRecord, targetIdPtr, width, height)
        : 1;
    if (empty) {
        // empty / target absent: destroy the form, return -1 (eax = edx = -1).
        if (H->formDestroy) H->formDestroy(handle);       // Form_Destroy(handle,-1)
        return -1;                                        // 0x594292 (edx == -1)
    }

    // --- 4. build the per-node windows (UI boundary, 0x5941c5) ---
    if (H->buildUpgradeTree) H->buildUpgradeTree(typeRecord, targetIdPtr);

    // --- 5. draw the edges (0x5941d6..0x59425b) ---
    const int nodeCount = st.nodeCount;                   // dword_13CE28C
    for (int outer = 0; outer < nodeCount; ++outer) {     // v11 / edi node
        // Y(N) reads record +0x34 == node (N+1)'s coordY (the cross-record alias).
        // For the last node (and beyond nodeCount) the original reads the flat store's
        // zeroed +2-record tail; the public RoadLayoutState only mirrors live nodes,
        // so any next>=nodeCount slot is the zeroed tail (0).
        auto coordY = [&](int n) -> int {
            int next = n + 1;
            return (next < nodeCount) ? st.nodes[next].coordY : 0;
        };
        const i32 outerX = st.nodes[outer].cost;          // dword_12CDD98[edi] (+0x30)
        const i32 outerY = coordY(outer);                 // dword_12CDD9C[edi] (+0x34)
        const i32 outerFrom = st.nodes[outer].parentFromId;
        const i32 outerTo   = st.nodes[outer].parentToId;
        for (int inner = 0; inner < outer; ++inner) {     // v6 / esi node (< outer)
            const i32 innerId = st.nodes[inner].nodeId;   // dword_12CDD8E[esi] >> 16
            if (innerId == outerFrom || innerId == outerTo) {
                const i32 innerX = st.nodes[inner].cost;  // dword_12CDD98[esi] (+0x30)
                const i32 innerY = coordY(inner);         // dword_12CDD9C[esi] (+0x34)
                if (H->drawLine)
                    H->drawLine(innerX + kUpgradeNodeBoxHalf,   // arg1 innerX+24
                                innerY + kUpgradeNodeBoxFoot,   // arg2 innerY+48
                                outerY + kUpgradeNodeBoxHalf,   // arg3 outerY+24
                                outerX + kUpgradeNodeBoxHalf,   // arg4 outerX+24
                                kUpgradeLineColor, kUpgradeLineStyle, kUpgradeLineWidthArg);
            }
        }
    }

    return UpgradeWindowHandle();                         // dword_13CE288
}

} // namespace guild::sim
