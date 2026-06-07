#pragma once
// Guild retained-mode GUI core — Form / Window / Object(Widget) data model.
//
// gilde.exe (32-bit x86, imagebase 0x400000) draws its in-game UI to an in-memory
// software surface using a three-level retained-mode hierarchy:
//
//     Form  (a loaded .form screen/dialog; owns a list of Windows)
//       └─ Window  (geometry, surfaces, an object-id child list, text buffer)
//            └─ Object/Widget  (the leaf controls: window-backing, 3D/anim, edit, ...)
//
// This header recovers the three record layouts and the original global array bases.
// Only the fields actually touched by the core alloc/free/select/lookup/add-child/
// accessor functions are named; everything else is preserved as raw storage so the
// total record size matches the original stride exactly. Unknown/unused regions are
// marked with TODO.
//
// IMPORTANT — record sizes are byte-exact to the originals:
//   Form:   171 dwords = 684 bytes   (base dword_676A60)
//   Window: 238 dwords = 952 bytes   (base dword_67EB80, capacity 96)
//   Widget: 740 bytes                (base dword_69FFB4, capacity 511)
//
// The original code addresses every field by raw byte/dword/word offset off the
// array base. To stay 1:1 we model each record as a fixed-size byte blob and provide
// typed accessors keyed by the exact original offset. This makes the "740 * idx" /
// "238 * idx" / "171 * id" pointer arithmetic in the decompiles translate directly.

#include "guild/common/types.h"
#include <cstring>

namespace guild::gui {

using guild::i16;
using guild::i32;
using guild::u8;
using guild::u16;
using guild::u32;

// ---------------------------------------------------------------------------
// Capacities / strides (recovered from the array sizes and loop bounds).
// ---------------------------------------------------------------------------
inline constexpr int kFormStrideDwords  = 171;            // 684 bytes
inline constexpr int kFormStrideBytes   = 171 * 4;        // 684
inline constexpr int kWindowStrideDwords = 238;           // 952 bytes
inline constexpr int kWindowStrideBytes  = 238 * 4;       // 952
inline constexpr int kWidgetStrideBytes  = 740;

inline constexpr int kMaxWindows  = 96;   // 22848 / 238
inline constexpr int kMaxWidgets  = 511;  // 378140 / 740
inline constexpr int kMaxChildren = 384;  // 0x600-byte id buffer / 4

inline constexpr int kObjIdBufBytes = 0x600;   // d2:win_obj  (<=384 int32 ids)
inline constexpr int kTextBufBytes  = 0x17D0;  // d2_t:w->txt (when window flag 0x10)

// Forms are indexed by a small id; the original BSS reserved enough space for the
// handful of concurrently-loaded forms. We size generously but document the base.
inline constexpr int kMaxForms = 64;

// ---------------------------------------------------------------------------
// Widget (Object) record — 740-byte stride, base dword_69FFB4.
//   +0   (dword)  slot marker / recycled window-slot id (set on free in Destroy)
//   +4   (dword)  inUse flag (0 = free; VIBE_Widget_AllocSlot scans this)
//   +8   (dword)  id          (window-backing widget id = winSlot + 1024)
//   +12  (dword)  dataPtr     -> Window* (type '@') or Object3D*/anim (type 'A')
//   +16..+22 (word) x,y,w,h   copied from owning window
//   +24  (byte)   type tag:  0x40 '@' window-backing, 0x41 'A' 3D/anim,
//                            0x43 'C' text-label, 0x45 'E' edit field
//   +26  (word)   subtype/order (=2 default)
//   +28..+34 (word) clip/screen bounds; +28 also incremented as child count
//   +36  (dword)  value (mirrored at +40)
//   +44  (dword)  group/parent link
//   +52  (dword)  inherited render ptr (from owning window-backing widget)
//   +60  (dword)  inherited clip ptr
//   +68  (dword)  button/clickable flag A (nonzero => togglable button)
//   +72  (dword)  button/clickable flag B
//   +116 (dword)  owning window slot
//   +120 (dword)  edit-field text ptr   (type 'E')
//   +124 (dword)  edit-field value
//   +128 (dword)  edit-field range
//   +140 (dword)  edit-field step
//   +620 (dword)  back-pointer used to fetch parent's +52/+60
// Type tag byte values (the original compares against these decimal constants):
inline constexpr u8 kTypeWindow = 0x40;  // '@'  window-backing widget
inline constexpr u8 kTypeAnim   = 0x41;  // 'A'  3D / animated object
inline constexpr u8 kTypeLabel  = 0x43;  // 'C'  text label (AddTextLabel)
inline constexpr u8 kTypeEdit   = 0x45;  // 'E'  edit / text field

struct Widget {
    u8 raw[kWidgetStrideBytes];

    Widget() { std::memset(raw, 0, sizeof(raw)); }

    // Typed accessors at the exact original byte offsets.
    template <typename T> T&       at(int off)       { return *reinterpret_cast<T*>(raw + off); }
    template <typename T> const T& at(int off) const { return *reinterpret_cast<const T*>(raw + off); }

    i32&  marker()      { return at<i32>(0); }     // +0
    i32&  inUse()       { return at<i32>(4); }     // +4
    i32&  id()          { return at<i32>(8); }     // +8
    i32&  dataPtr()     { return at<i32>(12); }    // +12  (Window*/Object3D* as 32-bit handle)
    i16&  x()           { return at<i16>(16); }    // +16
    i16&  y()           { return at<i16>(18); }    // +18
    i16&  w()           { return at<i16>(20); }    // +20
    i16&  h()           { return at<i16>(22); }    // +22
    u8&   type()        { return at<u8>(24); }     // +24
    i16&  order()       { return at<i16>(26); }    // +26
    i16&  clipX0()      { return at<i16>(28); }    // +28 (also child-count bump target)
    i16&  clipX1()      { return at<i16>(30); }    // +30
    i16&  clipY0()      { return at<i16>(32); }    // +32
    i16&  clipY1()      { return at<i16>(34); }    // +34
    i32&  value()       { return at<i32>(36); }    // +36
    i32&  valueMirror() { return at<i32>(40); }    // +40
    i32&  groupLink()   { return at<i32>(44); }    // +44
    i32&  disabledA()   { return at<i32>(56); }    // +56  (=1 when disabled; SetEnabled / RadioGroup_SetEnabled)
    i32&  renderPtr()   { return at<i32>(52); }    // +52
    i32&  parentClip()  { return at<i32>(60); }    // +60
    i32&  btnFlagA()    { return at<i32>(68); }    // +68
    i32&  btnFlagB()    { return at<i32>(72); }    // +72
    i32&  disabledB()   { return at<i32>(76); }    // +76  (=1 when disabled; second mirror in SetEnabled)
    i32&  dirty()       { return at<i32>(96); }    // +96  (=1 when slider value changed by mouse)
    i32&  ownerWindow() { return at<i32>(116); }   // +116
    i32&  editText()    { return at<i32>(120); }   // +120
    i32&  editVal()     { return at<i32>(120); }   // +120  (slider current value; alias of editText slot)
    i32&  editMin()     { return at<i32>(124); }   // +124  (slider lower bound)
    i32&  editValue()   { return at<i32>(124); }   // +124
    i32&  editMax()     { return at<i32>(128); }   // +128  (slider upper bound)
    i32&  editRange()   { return at<i32>(128); }   // +128
    i16&  editFlags()   { return at<i16>(132); }   // +132  (slider/edit mode bits: 0x10 clamp-to-step, 0x40 ...)
    i32&  editSpan()    { return at<i32>(136); }   // +136  (slider pixel span for thumb math)
    i32&  editStep()    { return at<i32>(140); }   // +140
    u8&   radioFlag()   { return at<u8>(444); }    // +444  (bit 0x02 = this radio button is selected)
    i32&  backWidget()  { return at<i32>(620); }   // +620
    u8&   decButtonHeld(){ return at<u8>(732); }   // +732  (slider "<" decrement button held this frame)
    u8&   incButtonHeld(){ return at<u8>(733); }   // +733  (slider ">" increment button held this frame)
};
static_assert(sizeof(Widget) == kWidgetStrideBytes, "Widget must be 740 bytes");

// ---------------------------------------------------------------------------
// Object3D / animation flag byte (lives at obj+38 of the type-'A' data record).
// VIBE_Object_GetDataPtr branches on these bits to decide what payload to return.
//   0x01 => return record+40   (inline value)
//   0x02 => return *(record+296)
//   0x10 => return *(record+328)
// The data record is owned by the renderer/scene cluster; the GUI only reads +38
// and the three payload slots. We model a minimal stand-in here so the accessor
// branch logic is exercisable; the real record is larger.
inline constexpr u8 kAnimFlagInline = 0x01;  // payload at +40
inline constexpr u8 kAnimFlag296    = 0x02;  // payload at *(+296)
inline constexpr u8 kAnimFlag328    = 0x10;  // payload at *(+328)

struct Object3D {
    u8 raw[336];  // >= +328 + 4; renderer cluster owns the full 536-byte scene record

    Object3D() { std::memset(raw, 0, sizeof(raw)); }
    template <typename T> T& at(int off) { return *reinterpret_cast<T*>(raw + off); }

    u8&  flags()    { return at<u8>(38); }    // +38  branch selector
    i32& inline40() { return at<i32>(40); }   // +40  payload for flag 0x01 (returns &record[40])
    i32& ptr296()   { return at<i32>(296); }  // +296 payload for flag 0x02 (returns *(record+296))
    i32& ptr328()   { return at<i32>(328); }  // +328 payload for flag 0x10 (returns *(record+328))
};

// ---------------------------------------------------------------------------
// Window record — 238-dword (952-byte) stride, base dword_67EB80, capacity 96.
//   word[2..5] = +4,+6,+8,+10 : x, y, w, h
//   word[8..11] = +16..+22    : margins (=4,4,4,4 on create)
//   dword[3]  = +12  : window flags
//   dword[6]  = +24  : pointer to the 0x600 object-id child list (int32 ids)
//   word[13]  >>... : object count lives as word at +26 (see objCount())
//   dword[8..10] = +32,+36,+40 : surfaces (main / alpha / colorkey)
//   dword[11] = +44 : text buffer (when flag 0x10)
//   dword[145] = +580 : content height
//   dword[146] = +584 : scroll current ; dword[147] = +588 : scroll prev
//   dword[155] = +620 : backing widget-slot index
//   dword[156] = +624 : state flag (=1 on create)
//   dword[160] = +640 : enabled / in-use flag (this is dword_67EE00[238*idx];
//                       0 == free slot; checked by Create/Destroy/AddToWindow)
// Object count: the original reads it two equivalent ways — as the word at +26
// (`*((WORD*)w+13)`) and as the dword `(char*)w+26 >> 16`. Both name the high word
// of the dword at +24? No: +24 is the id-list pointer. The count is the *word* at
// +26, and `*(int*)(w+26) >> 16` is that same word sign-extended via the dword at
// +26 whose high half is the count word at +28? The decompiles consistently use
// `*((__int16*)w + 14)` (word at +28) for the >=384 capacity test and
// `*(int*)((char*)w + 26) >> 16` (high word of dword@+26, i.e. word@+28) for the
// lookup bound. So the live count is the WORD at +28. We expose objCount() at +28.
struct Window {
    u8 raw[kWindowStrideBytes];

    Window() { std::memset(raw, 0, sizeof(raw)); }
    template <typename T> T&       at(int off)       { return *reinterpret_cast<T*>(raw + off); }
    template <typename T> const T& at(int off) const { return *reinterpret_cast<const T*>(raw + off); }

    i16& x()       { return at<i16>(4); }      // word[2]
    i16& y()       { return at<i16>(6); }      // word[3]
    i16& w()       { return at<i16>(8); }      // word[4]
    i16& h()       { return at<i16>(10); }     // word[5]
    i16& margin0() { return at<i16>(16); }     // word[8]
    i16& margin1() { return at<i16>(18); }     // word[9]
    i16& margin2() { return at<i16>(20); }     // word[10]
    i16& margin3() { return at<i16>(22); }     // word[11]
    i32& flags()       { return at<i32>(12); } // dword[3]
    i32& objListPtr()  { return at<i32>(24); } // dword[6] -> child id list (set to a real ptr; 0 = none)
    i16& objCount()    { return at<i16>(28); } // word[14]  (>=384 capacity test target)
    i32& surfaceMain() { return at<i32>(32); } // dword[8]
    i32& surfaceAlpha(){ return at<i32>(36); } // dword[9]
    i32& surfaceKey()  { return at<i32>(40); } // dword[10]
    i32& textBuffer()  { return at<i32>(44); } // dword[11]
    i32& contentHeight(){ return at<i32>(580); } // dword[145]
    i32& scrollCur()   { return at<i32>(584); } // dword[146]  (== dword_67EDC8[238*idx])
    i32& scrollPrev()  { return at<i32>(588); } // dword[147]  (== dword_67EDCC[238*idx])
    i32& scrollOffset(){ return at<i32>(592); } // dword[148]  (== dword_67EDD0[238*idx]; Window_Scroll target)
    i32& scrollExtraX(){ return at<i32>(596); } // dword[149]  (== dword_67EDD4[238*idx]; Window_Scroll accum)
    i32& reset234()    { return at<i32>(936); } // dword[234]  (-1 on Create; cleared by RemoveChildren)
    i32& reset235()    { return at<i32>(940); } // dword[235]  (-1 on Create; cleared by RemoveChildren)
    i32& reset236()    { return at<i32>(944); } // dword[236]  (-1 on Create; cleared by RemoveChildren)
    i32& backWidget()  { return at<i32>(620); } // dword[155]  (== dword_67EDEC[238*idx])
    i32& stateFlag()   { return at<i32>(624); } // dword[156]
    i32& enabled()     { return at<i32>(640); } // dword[160]  (== dword_67EE00[238*idx]); 0 = free
};
static_assert(sizeof(Window) == kWindowStrideBytes, "Window must be 952 bytes");

// ---------------------------------------------------------------------------
// Form record — 171-dword (684-byte) stride, base dword_676A60.
// The original aliases three labels onto the same array:
//   dword_676A60[171*id + k]       — generic dword view (Form_Destroy uses v3[100]/[105])
//   dword_676A64 == 676A60 + 1 dw  — window-id table: 676A64[171*id + slot]
//                                                    == 676A60[171*id + 1 + slot]
//   dword_676BE4 == 676A60 + 97 dw — window count:    676BE4[171*id]
//                                                    == 676A60[171*id + 97]
// Layout (dword slots, base 676A60):
//   [0]      marker (unused by core paths)
//   [1..96]  windowId[0..95]   (676A64 view, slot index = logical window slot)
//   [97]     windowCount       (676BE4 view)
//   [100]    in-use / valid flag (Form_Destroy guard)
//   [105,106] surfaces
struct Form {
    i32 dw[kFormStrideDwords];

    Form() { std::memset(dw, 0, sizeof(dw)); }

    i32& windowId(int slot) { return dw[1 + slot]; } // 676A64[slot] == 676A60[1+slot]
    i32& windowCount()      { return dw[97]; }       // 676BE4
    i32& valid()            { return dw[100]; }      // Form_Destroy guard (in-use flag)
    i32& shownFlag()        { return dw[102]; }      // Form_RefreshIfVisible visibility gate
    i32& childrenVisible()  { return dw[103]; }      // Form_SetChildrenVisible cached arg
    i32& objectsVisible()   { return dw[104]; }      // Form_SetObjectsVisible cached arg
    i32& surfaceA()         { return dw[105]; }
    i32& surfaceB()         { return dw[106]; }
};
static_assert(sizeof(Form) == kFormStrideBytes, "Form must be 684 bytes");

} // namespace guild::gui
