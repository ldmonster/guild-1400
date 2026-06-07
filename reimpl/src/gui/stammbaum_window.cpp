#include "gui/stammbaum_window.h"

namespace guild::gui {

namespace {

// Stable object-id bases per generation (the originals get these from AddToWindow's return;
// only relative identity matters for the click wiring).
constexpr int kSelfObj     = 2500;
constexpr int kSpouseObj   = 2501;
constexpr int kParentObj0  = 2510;
constexpr int kChildObj0   = 2520;

TreeNode MakeNode(int entity, int x, int objectId, int scale) {
    TreeNode n{};
    n.entity = entity;
    n.x = x;
    n.portraitX = x + 2;       // every node's portrait is placed at frame_x + 2
    n.objectId = (entity != 0) ? objectId : -1; // empty slots get the 1190 placeholder
    n.scale = scale;
    n.empty = (entity == 0);
    return n;
}

} // namespace

// gilde.exe 0x55ab84 (layout half).
//   v8 = window.w >> 16;  v152 = v8/2;  v132 = v152;  v134 = node.w >> 16;  v28 = v134/2.
//   self : v23 = v132 - (v134 + 90); spouse : v132 + 90.
//   parents (both): left v132-(v134+16), right v132+16; (one): v132 - v134/2.
//   children odd : center v132 - v134/2, then outward by (v134+40);
//   children even: v132 - v134 - 20, v132 + 20, then outward by (v134+40).
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
    l.self = MakeNode(fam.focus.entity, center - (nw + kStammSelfGap), kSelfObj,
                      kStammSelfScale);
    const bool married = fam.spouse.entity != 0;
    if (married)
        l.spouse = MakeNode(fam.spouse.entity, center + kStammSelfGap, kSpouseObj,
                            kStammSelfScale);
    else
        l.spouse = MakeNode(0, center + kStammSelfGap, kSpouseObj, kStammSelfScale);

    // --- parents -------------------------------------------------------------
    const bool twoParents = (fam.father.entity != 0) && (fam.mother.entity != 0);
    if (twoParents) {
        // left = center-(nw+16) (father), right = center+16 (mother)
        l.parents.push_back(MakeNode(fam.father.entity, center - (nw + kStammParentGap),
                                     kParentObj0, kStammParentScale));
        l.parents.push_back(MakeNode(fam.mother.entity, center + kStammParentGap,
                                     kParentObj0 + 1, kStammParentScale));
    } else {
        // single parent (whichever is present) centred at center - nw/2
        int p = fam.father.entity ? fam.father.entity : fam.mother.entity;
        l.parents.push_back(MakeNode(p, center - half, kParentObj0, kStammParentScale));
    }

    // --- children ------------------------------------------------------------
    int n = (int)fam.children.size();
    // The original arranges children symmetrically; we reproduce the X-anchor table.
    std::vector<int> xs;
    if (n > 0) {
        if (n & 1) {
            // odd: a centred child then outward pairs.
            //   slot0 = center - nw/2; slotL = slot0 - 40 - nw; slotR = nw+center-nw/2+40; ...
            xs.push_back(center - half);                       // v113
            if (n >= 2) xs.push_back(nw + center - half + kStammChildSpread);          // v115
            if (n >= 3) xs.push_back(center - half - kStammChildSpread - nw);          // v114
            if (n >= 4) xs.push_back(nw + (nw + center - half + kStammChildSpread)
                                        + kStammChildSpread);                          // v117
            if (n >= 5) xs.push_back((center - half - kStammChildSpread - nw)
                                        - kStammChildSpread - nw);                     // v116
        } else {
            // even: two inner children then outward pairs.
            xs.push_back(center - nw - kStammChildEvenGap);    // v118[0]
            if (n >= 2) xs.push_back(center + kStammChildEvenGap);                     // v118[1]
            if (n >= 3) xs.push_back((center - nw - kStammChildEvenGap)
                                        - nw - kStammChildSpread);                     // v118[2]
            if (n >= 4) xs.push_back(nw + center + kStammChildEvenGap
                                        + kStammChildSpread);                          // v119
        }
    }
    for (int i = 0; i < n && i < (int)xs.size(); ++i)
        l.children.push_back(MakeNode(fam.children[i].entity, xs[i], kChildObj0 + i,
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
