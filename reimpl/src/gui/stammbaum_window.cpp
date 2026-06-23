#include "gui/stammbaum_window.h"

namespace guild::gui {

namespace {

// Stable object-id bases per generation (the originals get these from AddToWindow's return;
// only relative identity matters for the click wiring).
constexpr int kSelfObj     = 2500;
constexpr int kSpouseObj   = 2501;
constexpr int kParentObj0  = 2510;
constexpr int kChildObj0   = 2520;

// `anchor` is the pre-centering X; the binary stores the node x as anchor + nw/2 (the
// AddCenteredLabel x, 0x55ae32) and places the portrait sprite at anchor + 2 (v146,
// 0x55bbda) — so portraitX = node.x - nw/2 + 2, NOT node.x + 2.
TreeNode MakeNode(int entity, int anchor, int half, int objectId, int scale) {
    TreeNode n{};
    n.entity = entity;
    n.x = anchor + half;       // = node.x (centered)
    n.portraitX = anchor + 2;  // portrait sprite at anchor + 2
    n.objectId = (entity != 0) ? objectId : -1; // empty slots get the 1190 placeholder
    n.scale = scale;
    n.empty = (entity == 0);
    return n;
}

} // namespace

// gilde.exe 0x55ab84 (layout half).
//   v8 = window.w >> 16;  center v121 = v8/2;  nw v123 = node.w >> 16;  half = nw/2.
//   Every node's STORED x is `anchor + nw/2` (the v111/v25/v123/2 centering term that the
//   binary adds at AddCenteredLabel time — 0x55ae0e: ax = nw/2 + center - nw - 90):
//     self          : center - (nw + 90) + nw/2            (v97)
//     spouse        : center + 90 + nw/2                    (v101)
//     parents (2)   : father center - (nw+16) + nw/2 (v86), mother center + 16 + nw/2 (v90)
//     parent  (1)   : (center - nw/2) + nw/2 = center       (v126 + v25)
//     children odd  : anchors {center-nw/2, center-nw/2-40-nw, nw+center-nw/2+40,
//                     v104-40-nw, nw+v105+40}, each + nw/2 (read in this memory order)
//     children even : anchors {center-nw-20, center+20, v108[0]-nw-40, nw+center+20+40},
//                     each + nw/2
FamilyTreeLayout Stammbaum_BuildLayout(const Family& fam, int canvasWidth, int nodeWidth,
                                       int titleArg) {
    FamilyTreeLayout l{};
    l.form = kFormStammbaum;
    l.titleText = (titleArg != 0) ? titleArg : kStammTitleDefault;
    l.canvasWidth = canvasWidth;
    l.nodeWidth = nodeWidth;

    const int center = canvasWidth / 2; // v132
    const int nw = nodeWidth;           // v134
    const int half = nw / 2;            // v28 / v40
    l.center = center;

    // --- self / spouse row ---------------------------------------------------
    // anchors: self = center-(nw+90), spouse = center+90 (node x = anchor + nw/2).
    l.self = MakeNode(fam.focus.entity, center - (nw + kStammSelfGap), half, kSelfObj,
                      kStammSelfScale);
    const bool married = fam.spouse.entity != 0;
    if (married)
        l.spouse = MakeNode(fam.spouse.entity, center + kStammSelfGap, half, kSpouseObj,
                            kStammSelfScale);
    else
        l.spouse = MakeNode(0, center + kStammSelfGap, half, kSpouseObj, kStammSelfScale);

    // --- parents -------------------------------------------------------------
    const bool twoParents = (fam.father.entity != 0) && (fam.mother.entity != 0);
    if (twoParents) {
        // father anchor = center-(nw+16) (v86), mother anchor = center+16 (v90)
        l.parents.push_back(MakeNode(fam.father.entity, center - (nw + kStammParentGap),
                                     half, kParentObj0, kStammParentScale));
        l.parents.push_back(MakeNode(fam.mother.entity, center + kStammParentGap, half,
                                     kParentObj0 + 1, kStammParentScale));
    } else {
        // single parent anchor = center - nw/2 (v126); node x = center.
        int p = fam.father.entity ? fam.father.entity : fam.mother.entity;
        l.parents.push_back(MakeNode(p, center - half, half, kParentObj0,
                                     kStammParentScale));
    }

    // --- children ------------------------------------------------------------
    int n = (int)fam.children.size();
    // The X-anchor table, read in the binary's memory order; node x = anchor + nw/2.
    std::vector<int> xs;
    if (n > 0) {
        if (n & 1) {
            // odd anchors v103..v107 (read v103,v104,v105,v106,v107 sequentially).
            const int a103 = center - half;                              // v103
            const int a104 = center - half - kStammChildSpread - nw;     // v104
            const int a105 = nw + center - half + kStammChildSpread;     // v105
            const int a106 = a104 - kStammChildSpread - nw;              // v106
            const int a107 = nw + a105 + kStammChildSpread;              // v107
            xs.push_back(a103);
            if (n >= 2) xs.push_back(a104);
            if (n >= 3) xs.push_back(a105);
            if (n >= 4) xs.push_back(a106);
            if (n >= 5) xs.push_back(a107);
        } else {
            // even anchors v108[0..3] (read in order).
            const int e0 = center - nw - kStammChildEvenGap;            // v108[0]
            const int e1 = center + kStammChildEvenGap;                 // v108[1]
            const int e2 = e0 - nw - kStammChildSpread;                 // v108[2]
            const int e3 = nw + center + kStammChildEvenGap + kStammChildSpread; // v108[3]
            xs.push_back(e0);
            if (n >= 2) xs.push_back(e1);
            if (n >= 3) xs.push_back(e2);
            if (n >= 4) xs.push_back(e3);
        }
    }
    for (int i = 0; i < n && i < (int)xs.size(); ++i)
        l.children.push_back(MakeNode(fam.children[i].entity, xs[i], half, kChildObj0 + i,
                                      kStammChildScale));

    return l;
}

// gilde.exe 0x55ab84 (wiring half).
//   On a click the original scans the self/spouse/parent/child object tables for a match and
//   returns that node's person (then re-centres or opens the info window per mode).
int Stammbaum_DispatchClick(const FamilyTreeLayout& l, int clickedObj) {
    if (l.self.objectId == clickedObj && !l.self.empty)   return l.self.entity;
    if (l.spouse.objectId == clickedObj && !l.spouse.empty) return l.spouse.entity;
    for (const auto& p : l.parents)
        if (p.objectId == clickedObj && !p.empty) return p.entity;
    for (const auto& c : l.children)
        if (c.objectId == clickedObj && !c.empty) return c.entity;
    return -1;
}

} // namespace guild::gui
