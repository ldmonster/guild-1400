# Hardening — GUI dialog cluster (gui01)

Files: src/gui/dialog.cpp (3), src/gui/dialog_checks.cpp (8),
src/gui/evidence_dialog.cpp (3), src/gui/feast_dialog.cpp (5).
19 provenance functions verified against gilde.exe via IDA MCP (decompile + disasm).

Counts: VERIFIED-1:1 = 18, FIXED = 1, BOUNDARY = 0.

Tests (all green, 10 suites):
gui_dialogs2_test, gui_remaining_test, gui_input_test, gui_book_reader_itest,
gui_message_box_itest, gui_book_reader_e2e_test, gui_dialogs2_e2e_test,
gui_input_e2e_test, gui_remaining_e2e_test, sim_he_entity_query_e2e_test.

---

## dialog.cpp

### Dialog_FormForFlags — 0x4ad6f0 / 0x569a30 — VERIFIED-1:1
Form-name selection ladder confirmed against both ShowMessageBox (0x4ad6f0) and
RunMessageBox (0x569a30): a1&2 -> Messagebox_Autosize; &0x10 -> _BIG; &0x20 ->
_VERY_BIG; &0x100 -> _NONE_PERGA; else -> Messagebox. Strings confirmed via decompile
refs. Flag constants in dialog.h (0x02/0x08/0x10/0x20/0x40/0x100/0x80-sign) match.

### Dialog_BuildButtonGroup — 0x4ad6f0 — VERIFIED-1:1
Loop `while (i < *(dword_62D298+26) >> 16)`, reads list[i] from `*(dword_62D298+24)`,
adds id to radio group iff `*(dword_69FFB4 + 740*id + 24) != 64` ('@'/kTypeWindow).
Then Selection_Update(group, 0). Stride 740, offset +24, skip-value 0x40 all confirmed.

### Dialog_ResolveResult — 0x4ad6f0 — VERIFIED-1:1
OK (dword_75BF38==1210) -> dword_676588[group]+1 (selectedIndex+1); Cancel (1155) or
right-click (byte_67225C==1) -> 0; else loop continues (-1). Matches modal-loop body.

---

## dialog_checks.cpp

### Dialog_CheckSkillRequirement — 0x4ad594 — VERIFIED-1:1
`if (required <= skill || !required) return 1;` else RenderFormattedMessage(buf,116,
required); ShowMessageBox(0, kind); return 0. (skill = *(a1+404).)

### Dialog_CheckActiveCharFlag — 0x4ad5d4 — VERIFIED-1:1
`if (!c || (c[218]&1)==0) return 0;` else msg 120 (arg *c), ShowMessageBox(256, kind),
return 1. `busy` models the `c && (c[218]&1)` predicate.

### Dialog_CheckResourceAmount — 0x4ad62c — VERIFIED-1:1
`if (CountAtLocation(*(a2+376)) >= needed || !needed) return 1;` else msg 117,
ShowMessageBox(0, needed-as-kind), return 0.

### Dialog_CheckResourceByItem — 0x4ad678 — VERIFIED-1:1
owned>=needed||!needed -> 1; else item==322 -> msg118, ==277 -> msg119, else msg117;
ShowMessageBox(4, kind), return 0. Original null-checks (a1 && *a1==322); the deref'd
itemId is passed in (null check is the caller boundary).

### Dialog_SimpleFormForFlags — 0x4adea4 — VERIFIED-1:1
&0x10 -> _BIG; &0x20 -> _VERY_BIG; else -> Messagebox. (No Autosize/NonePerga arms in
the simple variant — confirmed.)

### Book_HandlePageButton — 0x4be1d0 — VERIFIED-1:1
dword_75BF38==1753 -> TurnPage(book,2); ==1754 -> TurnPage(book,-2); else book.
Source returns the delta (2/-2/0). Button ids 1753/1754 confirmed.

### Book_SetPageText — 0x4be588 — VERIFIED-1:1
`if (firstPage > book[625]) return 0;` loop v4 in [0,book[625]); visible iff
`v4 >= firstPage && v4 <= firstPage+1`; return 1. Per-page visible decision matches
the (v4>=a2 && v4<=a2+1) test; the Form_RefreshIfVisible pane edge is the documented
io boundary.

### Window_CreateScrollButtons — 0x419ad8 — FIXED
Before: wrote widget[down][+476]/[+444] and widget[up][+476]/[+444] only inside
`if (down != -1)` / `if (up != -1)` guards.
After: writes unconditionally.
Evidence (disasm 0x419b42-0x419b8c down, 0x419ba8-0x419bf0 up): the binary stores
`[esi+3ACh]=v8` then immediately `mov [ebx+eax+1DCh],ebp` (+476) and
`mov byte ptr [ebx+eax+1BCh],3` (+444) indexing `dword_69FFB4 + 740*v8` with no
compare/branch on v8 == -1. The -1 guards were invented defensive code not present in
the original. Object_AddToWindow returns a valid slot in all live paths, so behavior is
unchanged in practice; the guards are removed for 1:1 fidelity. Also confirmed: the
default-geometry branch reads `(v7[+6]>>16)-32` (x) and `(v7[+8]>>16)-32` (y) via SAR
(signed >>16) — remapped to the Window record's pixel w()/h() fields (accepted window-
record abstraction). down=gfx, up=gfx+1, returns 896*group. Slot offsets +3ACh(940)
down / +3A8h(936) up confirmed.

---

## evidence_dialog.cpp

### EvidenceDialog_BuildBrowse — 0x548168 (layout) — VERIFIED-1:1
FindRecordById fail -> skip (no card, no index bump). Card: x=50, cardY=105*i+10
(0x5482e0/BuildPersonCard), labelY=55+105*i (v32 init 55, +=105). Empty -> RichString
0x1354 (4948). Form "privillegien\Beweise_Sichten". Stride 105, Y0 10, labelY0 55 all
match header constants.

### EvidenceDialog_DispatchBrowse — 0x548168 (wiring) — VERIFIED-1:1
Match clicked obj against card list (56-byte stride card records, +1 obj field);
return entity (v24[14*idx]) on hit, else -1. Models the `dword_62D22C == card.obj`
scan + ShowDetails(card.person) dispatch.

### EvidenceDialog_BuildDetails — 0x547e88 (layout) — VERIFIED-1:1
count<1 -> early return, no form. Row: nameY=10+40*i (v21 init 10, +=40),
AddLeftAlignedLabel(...,10,315,nameY,68). Seals: x=340+16*k (v13 init 340, +=16),
y=row baseline v25=40*i+10. Form "privillegien\Beweise_Details". Stride 40, Y0 10,
sealX0 340, sealStep 16 all match.

---

## feast_dialog.cpp  (all from 0x549a54)

### FeastDialog_BuildCourseMenu — VERIFIED-1:1
Header RichString 4997; entries "$L%ia[%s]$A" 4998+i (i 0..4) + final "$L%in[%s]"
4998+5 -> 6 child objects. Base 4998, count 6 confirmed (0x549bc7-0x549c40).

### FeastDialog_BuildDrinkMenu — VERIFIED-1:1
Only when wine cellar (v115 = QueryFind(...,24)!=0). Header 5004; "$L%ia[%s]$A" 5005+i
+ final 5005-based -> 6 entries (0x549d4e-0x549de5). No-cellar -> empty layout.

### FeastDialog_BuildTable — VERIFIED-1:1
Header RichString 4988. For i in [0,4): guest present (v103[i]) -> removeObj=childId,
++count; else removeObj=-1 (0x549fb8-0x54a01d). add (4990): SetEnabled(add,0) iff
count==4. confirm/cancel BuildButtonRow (4991/4992); SetEnabled(confirm,0) iff
count==0. Form "fest". addDisabled/confirmDisabled predicates match.

### FeastDialog_DispatchMenu — VERIFIED-1:1
Linear scan of the 6 child ids vs dword_62D22C; returns selected index or -1
(0x549cae loop, bound <6). Matches both course (+26 array) and drink (+50 array) scans.

### FeastDialog_DispatchTable — VERIFIED-1:1
add (v117) -> office overview (-2; ignored if disabled); confirm (v108) -> dispatch
feast (-3); cancel (v116) -> -4; else remove matching removeObj[i] -> i. Confirm body
(0x54a2c4): kind-55 slot reset (v82=55,v85=2) then per guest append delta + kind-54
with per-guest message kind v85 = (rank==6||rank==7) ? 9 : 1, rank = *(byte*)guest+2.
HoldFeast(building,course,drink,count) + InviteGuest(entity, 9|1) model this exactly.
Guest-msg constants 9/1 confirmed (0x54a3f3/0x54a4b0).
