// ===========================================================================
// guild::gui — drag-select orchestration + drag-cursor render/state, 1:1.
// See dragselect.h for the provenance manifest and per-function decompile notes.
// ===========================================================================
#include "gui/dragselect.h"

namespace guild::gui {

using guild::play::DragBox;
using guild::play::Viewport;
using guild::play::ProjectParams;
using guild::play::DragRect;

// ===========================================================================
// 0x4be154 — VIBE_DragSelect_DrawBox.
// Disasm-exact endpoint wiring of the four DrawLineLocked calls:
//   call1: eax=ax, edx=ay, ecx=edx(=ay), ebx=bx     -> (ax,ay)->(bx, with y1=ay)
//   call2: eax=bx, ecx=by+1 (inc ecx), ebx=eax(=bx) -> (bx,by+1)->(bx)
//   call3: eax=bx, edx=by, ecx=edx(=by), ebx=ax     -> (bx,by)->(ax)
//   call4: eax=ax, edx=ay, ecx=edx(=ay), ebx=ax     -> (ax,ay)->(ax)
// (DrawLineLocked's a2 arrives in ecx; a1=eax x1, a3=ebx x2, color=-1 on stack.)
// ===========================================================================
int DragSelect_BoxSegments(const DragBox& box, DragBoxSegment out[4]) {
    if (!box.active) return 0;                       // dword_11BC24C gate
    const int ax = box.ax, ay = box.ay, bx = box.bx, by = box.by;
    out[0] = { ax, ay,      bx, (i16)-1 };           // 0x4be176
    out[1] = { bx, by + 1,  bx, (i16)-1 };           // 0x4be191  (inc ecx -> by+1)
    out[2] = { bx, by,      ax, (i16)-1 };           // 0x4be1ab
    out[3] = { ax, ay,      ax, (i16)-1 };           // 0x4be1c5
    return 4;
}

void DragSelect_DrawBox(const DragBox& box, const DragDrawHooks& h) {
    DragBoxSegment seg[4];
    int n = DragSelect_BoxSegments(box, seg);
    if (h.drawLineLocked) {
        for (int i = 0; i < n; ++i)
            h.drawLineLocked(seg[i].x1, seg[i].y1, seg[i].x2, seg[i].color);
    }
}

// ===========================================================================
// 0x4bdc3c / 0x4bdecc — ApplyToUnits / ApplyToSelection.
//
// Head (both, byte-identical clamp): update the dragged corner from the live cursor,
// clamp to the scissor rect with the original idiom (max(lo) first, then cap to hi-1),
// store into box.bx/by; then normalize. The clamp idiom is play::DragClampX/Y.
//
// Then iterate units: for each live unit, clear inBox; if (selFlag & 0x800) and the
// unit has a mesh, project its 8-corner centroid (*weight) and set inBox=1 when it
// lands inside the rect. The project+contain is play::DragUnitCentroidHit.
//
// The corner-recompute here reproduces the EXACT clamp the apply functions inline
// (which differs subtly from BeginBox: they clamp the *cursor* to [lo, hi-1] but the
// ordering of the two min/max is the disasm's: first floor to lo, then if < hi-1 take
// the cursor (re-floored to lo), else hi-1). play::DragClampX implements that idiom.
// ===========================================================================
void DragSelect_Apply(DragBox& box, DragUnit* units, int count,
                      int cursorX16, int cursorY16,
                      const Viewport& vp, const ProjectParams& pp, float weight) {
    if (!box.active) return;                          // dword_11BC24C

    // Update dragged corner from cursor (>>16) clamped to scissor rect.
    int cx = cursorX16 >> 16;                         // (int)unk_67220E >> 16
    int cy = cursorY16 >> 16;                         // dword_672210 >> 16
    box.bx = guild::play::DragClampX(cx, vp);         // dword_11BC258
    box.by = guild::play::DragClampY(cy, vp);         // dword_11BC25C

    DragRect r = guild::play::DragSelectNormalize(box);  // v4/v23/v24/v22

    for (int i = 0; i < count; ++i) {
        DragUnit& u = units[i];
        if (!u.handle) continue;                      // dword_11BB6A0[i] != 0
        if (!u.active) continue;                       // *(slot+8) != 0
        u.inBox = 0;                                    // *(slot+392) = 0
        if ((u.selFlag & kDragSelectFlagBit) == 0) continue;   // (flag & 0x800)
        if (!u.hasMesh) continue;                       // mesh-chain present
        if (guild::play::DragUnitCentroidHit(u.sumX, u.sumY, u.sumZ, weight, pp, r))
            u.inBox = 1;                                // *(slot+392) = 1
    }
}

void DragSelect_ApplyToUnits(DragBox& box, DragUnit* units, int count,
                             int cursorX16, int cursorY16,
                             const Viewport& vp, const ProjectParams& pp) {
    // dbl_61E208 == 0.125
    DragSelect_Apply(box, units, count, cursorX16, cursorY16, vp, pp,
                     guild::play::picksel_const::kEighth);
}

void DragSelect_ApplyToSelection(DragBox& box, DragUnit* units, int count,
                                 int cursorX16, int cursorY16,
                                 const Viewport& vp, const ProjectParams& pp) {
    // dbl_61E210 == 0.125
    DragSelect_Apply(box, units, count, cursorX16, cursorY16, vp, pp,
                     guild::play::picksel_const::kEighth);
}

// ===========================================================================
// 0x4ba2bc — VIBE_DragSelect_UpdateUnitList.
//
// State machine over the staged cells. Locals mapped from the decompile:
//   v1  = queued-target count             (v27[] follow-list, v1>3 -> stop)
//   v2  = last target ptr (tail flush)    (we keep the cell index + ptr)
//   v22 = "a batch was opened" flag        (tail SlotReset28)
//   v32 = per-iteration "saw a 67 owner"   ('C' kind seen, drives the v31 retry path)
//   v30 = stop flag                        (set when v1 > 3)
//   v31 = pending owner ptr from the v32 path
// The original has three near-identical inner blocks (first-touch, v32-retry,
// v32==67 commit); they all (a) collapse the target's 4-slot follow list (+196..+208)
// setting the first slot == targetId to -1, and (b) on the player path queue the
// follow command. We reproduce the observable effects (queue-call sequence, the +196
// collapse, the v1 accumulation, the >4 stop, the two tail flushes) faithfully.
// ===========================================================================
namespace {
// Collapse the target's 4-slot follow list: first slot == targetId -> -1.
// (LABEL_14/25/34/40 in the decompile: walk followSlots[0..3], set first match -1.)
void CollapseFollowList(UpdateCell& c, i32 targetId) {
    for (int k = 0; k < 4; ++k) {
        if (c.followSlots[k] == targetId) { c.followSlots[k] = -1; return; }
    }
}
} // namespace

int DragSelect_UpdateUnitList(UpdateCell* cells, int count,
                              const UpdateUnitListState& st,
                              const UpdateUnitListHooks& h) {
    int v1 = 0;            // queued count
    int v2cell = -1;       // v2 = dword_12CEA8C (last target ptr; tail Entity29 gate)
    bool v22 = false;      // batch opened (tail QueueRequestSlotReset28)
    char v32 = 0;          // 0 / 67 — "pending target kind from a prior cell"
    bool v30 = false;      // stop flag
    i32 v31 = 0;           // pending owner ptr (v32 path) = dword_12CEA7C
    i32 lastTarget = 0;    // v2 = dword_12CEA8C (target ptr; retry compare + Entity29)

    for (int i = 0; i < count && !v30; ++i) {
        UpdateCell& c = cells[i];
        if (!c.pending) continue;                         // byte_12CEA98[v3]

        if (v32 == 0) {
            // ---- first-touch block (loc_4ba30a) ----
            // 0x4ba30a: gate on dword_12CEA8C (target) != 0, then dword_12CEA7C
            // (owner) != 0. 0x4ba325-32d: v32 = 67 and edi(=v2) = target are BOTH
            // set unconditionally HERE, before the *(target)==67 kind check at
            // 0x4ba334. So a non-char first-touch still arms v32 and v2 (drives the
            // retry block + the Entity29 tail) — only the queue work is gated on 67.
            if (c.target != 0 && c.owner != 0) {           // dword_12CEA8C && dword_12CEA7C
                v32 = (char)kCharKind;                       // v32 = 67 (0x4ba32d)
                lastTarget = c.target;                       // v2 = edi = target (0x4ba327)
                v2cell = i;                                  // v2 ptr -> Entity29 tail
                if (c.kind == kCharKind) {                   // *(target) == 67 (0x4ba334)
                    if (c.owner == st.playerRecord) {        // a1 == dword_11BC2F4
                        // ---- player path (commit immediately) ----
                        if (h.queryFind)
                            h.queryFind(st.playerRecord, 1, 0, 326);
                        CollapseFollowList(c, c.targetId);
                        if (h.changePlayerAction)
                            h.changePlayerAction(st.playerRecord, 0, 0, c.action);
                        if (h.queueSingle49) h.queueSingle49(c.targetId);
                        if (h.queueNamed53)
                            h.queueNamed53(c.targetId, c.ownerObjLink, 0, c.targetLink, 0, 0);
                    } else {
                        // ---- non-player: open a batch, defer ----
                        v31 = c.owner;                       // v31 = a1 = owner
                        v22 = true;                          // batch opened (v22 = 67)
                        ++v1;
                        CollapseFollowList(c, c.targetId);
                    }
                }
            }
        } else if (v32 == kCharKind) {
            // ---- v32 retry block (loc_4ba4d7) ----
            if (c.owner == v31 && c.target == lastTarget) {  // v31==owner && v2==target
                if (st.playerRecord == c.owner) {
                    if (h.queryFind)
                        h.queryFind(st.playerRecord, 1, 0, 326);
                    CollapseFollowList(c, c.targetId);
                    if (h.changePlayerAction)
                        h.changePlayerAction(st.playerRecord, 0, 0, c.action);
                    if (h.queueSingle49) h.queueSingle49(c.targetId);
                    if (h.queueNamed53)
                        h.queueNamed53(c.targetId, c.ownerObjLink, 0, c.targetLink, 0, 0);
                } else {
                    ++v1;
                    CollapseFollowList(c, c.targetId);
                    if (v1 > kUpdateMaxBatch - 1)             // v1 > 3
                        v30 = true;
                }
                // NOTE: the retry block does NOT reassign edi(=v2); the Entity29
                // tail keeps the first-touch target (so no v2cell update here).
            }
        }
        c.pending = 0;                                       // byte_12CEA98[v3] = 0
    }

    // ---- tail flushes (in original order) ----
    if (v2cell >= 0) {                                       // if (v2) QueueRequestEntity29
        UpdateCell& c = cells[v2cell];
        if (h.queueEntity29) h.queueEntity29(c.ownerLink, c.target);   // (*(v2+112), v2)
    }
    if (v22) {                                               // if (v22) QueueRequestSlotReset28
        // 0x4ba602: QueueRequestSlotReset28(&v21, a1). a1 here is a recycled
        // follow-slot register; modeled with the pending owner (v31) — the call
        // count is what is observable, not the coupled arg value.
        if (h.queueSlotReset28) h.queueSlotReset28(v31);
    }
    return v1;
}

// ===========================================================================
// 0x41fcbc — VIBE_DragCursor_SetSprite.
// a2 != 0: free old slot (if any), then register a new one from current coords.
// a2 == 0: free slot (if any) and reset mode.
// ===========================================================================
void DragCursor_SetSprite(DragCursorSprite& s, i32 spriteId,
                          const DragCursorCoords& c, const DragCursorSpriteHooks& h) {
    if (spriteId != 0) {
        if (s.slot != -1) {                                  // dword_62D30C != -1
            if (h.freeSlot) h.freeSlot(s.slot);              // ShapeAnim_GetSlot (free)
            // (the original assigns v3 then overwrites; net == free + re-register)
        }
        i32 state = h.stateUpdate ? h.stateUpdate(spriteId) : 0;   // State_Update(a2)
        s.slot = h.registerSlot ? h.registerSlot(c.x, c.y, 0, state) : -1;
    }
    if (spriteId == 0) {
        if (s.slot == -1) {                                  // already no slot
            s.mode = 0;                                       // word_62D310 = 0
        } else {
            if (h.freeSlot) h.freeSlot(s.slot);               // ShapeAnim_GetSlot (free)
            s.slot = -1;                                       // dword_62D30C = -1
            s.mode = 0;                                         // word_62D310 = 0
        }
    }
}

// ===========================================================================
// 0x41fa1c — VIBE_DragCursor_Render: count-badge layout (pure geometry).
//   v10 counts only emitted (non -1) slots; v11 strides by 3 over 18.
//   badgeY = (rowAddend + 26) * (v10/3) + cursorY
//   badgeX = 26 * (v10%3) + cursorX + xBias
// (0x41fb46 / 0x41fb4e in the disasm; the +78 bias is pre-resolved into xBias.)
// ===========================================================================
int DragCursor_BadgeLayout(const DragCursorRenderState& rs, DragCursorBadge* out) {
    if (rs.hidden) return 0;                            // !dword_62D314 gate
    int v10 = 0;
    for (int s = 0; s < kDragCursorChildSlots; ++s) {  // v11 in {0,3,...,15}
        if (rs.childShape[s] == -1) continue;          // dword_75B9F0[v11] != -1
        int badgeY = (rs.rowAddend + kDragCursorBadgeStep) * (v10 / 3) + rs.cursorY;
        int badgeX = kDragCursorBadgeStep * (v10 % 3) + rs.cursorX + rs.xBias;
        out[v10] = { badgeX, badgeY, rs.childShape[s], rs.childCount[s] };
        ++v10;
    }
    return v10;
}

} // namespace guild::gui
