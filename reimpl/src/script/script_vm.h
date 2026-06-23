#pragma once
// ===========================================================================
// Die Gilde 1400 — Script-VM front-end (the .esc parser / interpreter gate).
//
// 1:1 byte-faithful reconstruction of the script-VM front-end functions that
// gate script execution. These are translated *directly from the Hex-Rays
// pseudocode*, preserving the original's raw struct-offset addressing on the
// 2584-byte per-script "context" record. We keep the exact control flow,
// constants, field offsets and side effects.
//
//   gilde.exe (32-bit x86, imagebase 0x400000):
//     0x4431dc VIBE_Script_EnterFunction          — runtime function call: eval
//                args, push a 36-dword call frame, bind params as locals, jump
//                the cursor to the body.
//     0x442d88 VIBE_Script_ParseDeclaration_42d88  — a declaration statement:
//                read type keyword then name; on '=' / '[' / func-sig forms,
//                build the variable (DefineVariable) or function (ParseSymbolName).
//     0x442b34 VIBE_Script_ParseSymbolName         — parse a function signature's
//                parameter list (type+name pairs) into the func record.
//     0x441280 VIBE_Script_DeclareLocal            — push a local into the active
//                call frame's 8-slot local table + the +2484 local stack.
//     0x4416c4 VIBE_Script_SkipBraceBlock          — advance / rewind the cursor
//                over a balanced { } block (mode 1 forward, mode 2 backward).
//     0x445a28 VIBE_Script_DestroyContext          — recursively free a context's
//                var/func tables, line table, local stack and child contexts.
//     0x43dfb0 VIBE_Character_RegisterScriptCommands — register the per-character
//                script command table (38 ImportCommand calls).
//
// Leaf reconstructions also included (all reachable as callees):
//     0x442c90 VIBE_Script_DefineVariable    — append a var record (48-byte).
//     0x4414d4 VIBE_Script_LookupVariable    — find a var/local/event token.
//     0x4415f8 VIBE_Script_LookupFunction    — find a func record (304-byte).
//     0x441788 VIBE_Script_ParseTypeKeyword  — classify a type keyword string.
//     0x440f94 VIBE_Script_ReportError       — modeled as a hook (logging leaf).
//     0x443f38 VIBE_Script_Finish            — modeled as a hook (re-entrant).
//
// Deeply-coupled leaves kept as injectable hooks (their bodies live elsewhere /
// touch deep host state; the control flow that *uses* them is reconstructed 1:1):
//     0x441974 VIBE_Script_NextToken         — the source-text tokenizer (its
//                keyword/operator tables are built at runtime, not static).
//     0x443ff0 VIBE_Script_EvaluateExpression — operator-precedence evaluator.
//     0x43923c VIBE_Memory_FreeDebug / 0x438f10 VIBE_Memory_AllocDebug.
//     0x5d3f10 VIBE_Util_StrCmp.
//     the per-character Cmd* leaves registered by RegisterScriptCommands (stored
//     by identity only — never called through here).
//
// Namespace: guild::script (a NEW module; the prior guild::sim::script_vm is a
// higher-level re-imagining that explicitly *deferred* these exact functions —
// see its header). No symbols are shared, so no ODR clash.
// ===========================================================================
#include "guild/common/types.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace guild::script {

using guild::u8;
using guild::u32;
using guild::i32;

// ---------------------------------------------------------------------------
// ScriptContext — the 2584-byte per-script runtime record. Field offsets are
// the exact byte offsets used by the originals; we keep a raw byte buffer and
// access fields by offset so the reconstructed pointer arithmetic is identical.
//
// dword index (offset)   meaning (recovered from the decompiles):
//   [32]  +128  state flag         (-1 destroyed, 0 by Finish)
//   [34]  +136  var table base     (DefineVariable / LookupVariable)
//   [35]  +140  func table base    (ParseSymbolName / LookupFunction)
//   [36]  +144  func count
//   [37]  +148  var count
//   [38]  +152  source cursor      (NextToken reads/advances this)
//   [39]  +156  source base ptr    (line-range / SkipBraceBlock bound)
//   [40]  +160  source length      (SkipBraceBlock upper bound = +156 + +160)
//   [41]  +164  runtime flags      (bit0 runnable)
//   [42]  +168  call-frame array   (16 frames, stride 36 dwords / 144 bytes)
//   [618] +2472 active call-frame ptr (top of frame stack)
//   [619] +2476 line-table count   (ReportError)
//   [620] +2480 line-table base    (ReportError) — freed by DestroyContext
//   [621] +2484 local-stack base   — freed by DestroyContext; +1024 = limit
//   [622] +2488 local-stack alloc cursor (DeclareLocal bumps it)
//   [623..630] +2492.. child context ptrs (8 slots; DestroyContext recurses)
//
// Call frame (36 dwords / 144 bytes at +168 + 144*i):
//   frame[0]  +0   func record ptr (body source)          (EnterFunction +42*4)
//   frame[1]  +4   active source cursor for the frame
//   frame[2]  +8   local index used (DeclareLocal scans +16.. for free)
//   frame[4..11] +16.. up to 8 local-var record ptrs
//   frame[44] +176 saved cursor (EnterFunction)
//   frame[45] +180 scene slot      (= ctx[622] snapshot)
//
// Variable record (48 bytes):
//   +0   nibble  type (low nibble); high nibble flags
//   +1   name    (2-byte-stride NUL-terminated copy)
//   +36  i32     element count (array length)
//   +40  i32     scope marker  (1 = local, set by DeclareLocal)
//   +44  ptr     storage block  (DefineVariable AllocDebug result)
//
// Function record (304 bytes):
//   +0   name    (2-byte-stride NUL-terminated copy)
//   +32  ptr     body source ptr (set by ParseDeclaration func path)
//   +34  hi byte return type     (HIBYTE of dword @ +32..; param-type source)
//   +36  u8      param count
//   +37  u8[N]   param types (one byte each, N from +36)
//   +45  char[32*N] param names (32 bytes each)
// ---------------------------------------------------------------------------
constexpr int kContextDwords     = 646;    // 2584 / 4 — the record's dword count
constexpr int kCallFrameStride   = 36;     // dword indices between frames (+144 in image)
constexpr int kMaxCallFrames     = 16;
constexpr int kMaxLocalsPerFrame = 8;      // frame +16.. scan stops at +32 bytes
constexpr int kLocalStackLimit   = 1024;   // ctx[621] + 1024 overflow bound

// Variable / local record (image stride 48). The small integer fields keep
// their exact image byte offsets; the storage pointer at image +44 is widened
// to pointer width and relocated to a clean pointer-aligned slot so it can't
// overlap +36/+40 (or the next record) on a 64-bit host. The record stride is
// widened accordingly. byte +0 nibble type, +1 name, +36 elemCount, +40 scope.
constexpr int kVar_Nibble    = 0;
constexpr int kVar_Name      = 1;
constexpr int kVar_ElemCount = 36;   // i32
constexpr int kVar_Scope     = 40;   // i32
constexpr int kVar_StorageP  = 48;   // pointer-width (was image +44, relocated to a clean slot)
constexpr int kVar_InlineSt  = 48 + (int)sizeof(void*);  // local in-line storage start
constexpr int kVarRecordStride = 48 + (int)sizeof(void*); // physical record stride (widened)

// Function record (image stride 304). name@0, paramCount@36, paramTypes@37+i,
// paramNames@45+32*i. The body source pointer (image +32) is widened and moved
// past the names (45 + 32*8 = 301 < 304) to a clean pointer-aligned slot.
constexpr int kFunc_Name       = 0;
constexpr int kFunc_ParamCount = 36;   // u8
constexpr int kFunc_ParamTypes = 37;   // u8[N] (EnterFunction reads per-param type here)
constexpr int kFunc_ParamNames = 45;   // char[32*N]
constexpr int kFunc_BodyP      = 304;  // pointer-width body source (clean slot, was image +32)
constexpr int kFuncRecordStride = 304 + (int)sizeof(void*); // physical record stride (widened)

// Field DWORD INDICES (the `a1[idx]` indices used by the originals). The byte
// offset in the 32-bit image is idx*4 (shown in the trailing comment); in this
// reconstruction every cell is pointer-width, so the live byte offset is
// idx*sizeof(intptr_t) — see the Dw() accessor in the .cpp. Using dword indices
// keeps every field reference scaled uniformly.
constexpr int kIdx_State        = 32;    // +128 state flag
constexpr int kIdx_VarBase      = 34;    // +136 var table base
constexpr int kIdx_FuncBase     = 35;    // +140 func table base
constexpr int kIdx_FuncCount    = 36;    // +144 func count
constexpr int kIdx_VarCount     = 37;    // +148 var count
constexpr int kIdx_Cursor       = 38;    // +152 source cursor
constexpr int kIdx_SrcBase      = 39;    // +156 source base
constexpr int kIdx_SrcLen       = 40;    // +160 source length
constexpr int kIdx_Flags        = 41;    // +164 runtime flags
constexpr int kIdx_CallFrames   = 42;    // +168 call-frame array base
constexpr int kIdx_ActiveFrame  = 618;   // +2472 active call-frame ptr
constexpr int kIdx_LineCount    = 619;   // +2476 line-table count
constexpr int kIdx_LineBase     = 620;   // +2480 line-table base
constexpr int kIdx_LocalBase    = 621;   // +2484 local-stack base
constexpr int kIdx_LocalCursor  = 622;   // +2488 local-stack alloc cursor
constexpr int kIdx_ChildCtx0    = 623;   // +2492 first child context ptr

// Variable type codes (shared with NextToken/ParseTypeKeyword/DefineVariable).
//   1 = int (4 bytes), 2 = byte/char (1 byte), 5 = void, 6 = string (96 bytes),
//   7 = float (4 bytes), 8 = '{' , 9 = '}'. (DeclareLocal/DefineVariable sizes.)
enum VarType : u8 {
    kVarInt    = 1,
    kVarByte   = 2,
    kVarVoid   = 5,
    kVarString = 6,
    kVarFloat  = 7,
    kVarBraceOpen  = 8,
    kVarBraceClose = 9,
};

// NextToken result record (the `result` buffer NextToken writes). The first byte
// is the token class; the following dword is the value (variable/func/cmd ptr or
// literal), and for type keywords the byte at +4 is the type code.
//   class 0 unknown, 1 symbol (sub at +4), 2 variable (ptr at +1dw),
//   3 func-def, 4 cmd, 5 int literal (value at +1dw), 6 raw symbol,
//   7 string literal, 8 type keyword (type at +4), 10 keyword, 11 include,
//   12 end-of-source.
struct TokenResult {
    u8  cls = 0;      // result[0]
    u8  pad[3] = {0,0,0};
    i32 value = 0;    // *(dword*)(result+1) for class 2/3/4/5/11; +4 byte for 1/8/10
    char text[64] = {0};  // class 6/7 string lexeme copied to result+4
};

// ---------------------------------------------------------------------------
// Injectable host leaves. The reconstructed control flow calls these exactly
// where the original calls VIBE_Script_NextToken / EvaluateExpression / etc.
// Defaults (installed if no hook table is set) are inert/deterministic so the
// front-end can be exercised without the full engine.
// ---------------------------------------------------------------------------
struct ScriptVmHooks {
    // 0x441974 — tokenizer. Reads the cursor (ctx[38]) and advances it; writes
    // the next token into `out`. Returns the token class (out->cls).
    u8 (*nextToken)(u8* ctx, TokenResult* out) = nullptr;
    // 0x443ff0 — evaluate one expression from the live cursor; returns its value.
    i32 (*evaluateExpression)(u32 stopAt) = nullptr;
    // 0x440f94 — ReportError(ctx, cursor, msg). Logging leaf.
    int (*reportError)(u8* ctx, u32 cursor, const char* msg) = nullptr;
    // 0x443f38 — Finish(ctx). Returns 1 when the context is torn down.
    int (*finish)(u8* ctx) = nullptr;
    // 0x438f10 / 0x43923c — debug allocator pair (DefineVariable / DestroyContext).
    void* (*allocDebug)(std::size_t bytes, const char* tag) = nullptr;
    void  (*freeDebug)(void* p) = nullptr;
    // 0x5d3f10 — strcmp leaf (0 == equal).
    int (*strCmp)(const char* a, const char* b) = nullptr;
};
void SetScriptVmHooks(const ScriptVmHooks* hooks);
const ScriptVmHooks& GetScriptVmHooks();

// The active context global (gilde.exe dword_62E8A8). NextToken/ReportError read
// it; the front-end sets it while parsing/running a context.
u8* CurrentContext();
void SetCurrentContext(u8* ctx);

// Lexer scope-override global (gilde.exe dword_62E8C8 / dword_62E8C0 line idx) —
// LookupVariable/LookupFunction honour it. Default 0 (use the passed context).
extern u32 g_scopeOverride;   // dword_62E8C8

// ---------------------------------------------------------------------------
// Field accessors (raw offset reads/writes, matching the original arithmetic).
// ---------------------------------------------------------------------------
// Context cell accessor: the i-th pointer-width cell of the context record
// (`a1[i]` in the image, scaled to pointer width). Holds either an integer
// field or a pointer field uniformly.
inline std::intptr_t& Dw(u8* c, int dwordIndex) {
    return *reinterpret_cast<std::intptr_t*>(c + (std::size_t)dwordIndex * sizeof(std::intptr_t));
}
// Raw byte field accessor for the separately-allocated var/func records.
inline u8&   RecByte(u8* r, int off) { return *reinterpret_cast<u8*>(r + off); }
inline i32&  RecI32 (u8* r, int off) { return *reinterpret_cast<i32*>(r + off); }
inline std::intptr_t& RecPtr(u8* r, int off) { return *reinterpret_cast<std::intptr_t*>(r + off); }

// ===========================================================================
// 0x442c90 — VIBE_Script_DefineVariable(ctx, type, elemCount, sizeHint)
// Append a 48-byte var record at ctx[34] + 48*ctx[37]; alloc the storage block,
// bump the var count, return the record pointer.
// ===========================================================================
u8* DefineVariable(u8* ctx, u8 type, i32 elemCount, i32 sizeHint);

// 0x441788 — classify a type-keyword string (int=1, float=7, void=5, string=6,
// '{'=8, '}'=9, byte/char=2, else 0). Strips a trailing ' ' or ')'.
int ParseTypeKeyword(const char* lexeme);

// 0x4414d4 — find a variable by name: scan the active call-frame locals, then
// the context's var table, then the global event-token table. Returns the
// matching record ptr or 0.
u8* LookupVariable(u8* ctx, const char* name);

// 0x4415f8 — find a function record by name (304-byte stride). Returns ptr or 0.
u8* LookupFunction(u8* ctx, const char* name);

// ===========================================================================
// 0x441280 — VIBE_Script_DeclareLocal(ctx, type, name, elemCount)
// Push a local into the active frame's free local slot (frame +16..+28) and the
// +2484 local stack. Returns the new var record, or 0 if the frame is full /
// the slot scan found no room.
// ===========================================================================
u8* DeclareLocal(u8* ctx, u8 type, const char* name, i32 elemCount);

// ===========================================================================
// 0x4416c4 — VIBE_Script_SkipBraceBlock(mode)
//   mode 1: forward — advance ctx[38] to just past the matching '}'.
//   mode 2: backward — rewind ctx[38] to the matching '{'.
// Operates on the current-context cursor (CurrentContext()). Returns 1 on a
// matched brace, 0 if the source bound is hit first.
// ===========================================================================
int SkipBraceBlock(u8 mode);

// ===========================================================================
// 0x442b34 — VIBE_Script_ParseSymbolName(ctx, lexBuf)
// Parse a function signature parameter list: repeated (type-keyword, name)
// pairs terminated by sub-code 13 (')'), into the latest func record
// (304*ctx[36] + ctx[35]). Returns lexBuf on success, 0 on a syntax error.
// ===========================================================================
i32 ParseSymbolName(u8* ctx, u8* lexBuf);

// ===========================================================================
// 0x442d88 — VIBE_Script_ParseDeclaration(ctx)
// Parse a declaration statement (the type keyword was already consumed by the
// caller's NextToken). Reads the name, then dispatches on the next token's
// type: scalar (10), '=' init (2), array '[' (27), function-sig (12). Returns 1
// (always continues) or ParseSymbolName's result on the function path.
// ===========================================================================
i32 ParseDeclaration(u8* ctx);

// ===========================================================================
// 0x4431dc — VIBE_Script_EnterFunction(ctx, funcRec)
// Evaluate the call arguments, push a 36-dword call frame, bind each parameter
// as a local (DeclareLocal), copy the evaluated arg values into the locals'
// storage, and jump the source cursor to the function body. Returns the new
// active-frame ptr, or a ReportError/Finish result on overflow / OOM.
// ===========================================================================
int EnterFunction(u8* ctx, u8* funcRec);

// ===========================================================================
// 0x445a28 — VIBE_Script_DestroyContext(ctx)
// Recursively free a context: its var-table storage blocks, child contexts
// (8 slots), the var table, func table, line table and local stack; clear the
// 2584-byte record and mark +128 = -1. Returns 1 if a context was destroyed.
// ===========================================================================
int DestroyContext(u8* ctx);

// ===========================================================================
// 0x43dfb0 — VIBE_Character_RegisterScriptCommands()
// Register the 38 per-character script commands via the command table. Returns
// the result of the final ImportCommand (1 on success). The Cmd* leaves are
// stored by identity only (opaque tokens), exactly as the image does.
// ===========================================================================
i32 RegisterScriptCommands();

// One descriptor per registered character command (recovered from the 38
// ImportCommand calls @0x43dfb0). Test/introspection helper.
struct CharCommandDesc {
    const char* name;
    u8          kind;     // 1 = function (returns value), 5 = statement
    int         argc;
    const u8*   argTypes; // argc entries (1=int,6=string,7=float), or null when argc==0
    int         tokenId;  // identity index of the Cmd* leaf (opaque)
};
// The full ordered table (size 38). Stable for the lifetime of the program.
const CharCommandDesc* CharCommandTable(int* count);

} // namespace guild::script
