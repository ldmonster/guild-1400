#pragma once
// Script-VM "long tail": per-command import/opcode bodies and the binary `.esc`
// loader's token-reader family that the core VM (script_vm.cpp / script_run.cpp /
// script_compiler.cpp) leaves untranslated.
//
// Namespace: guild::sim (same cluster as script_vm.{h,cpp}).
//
// Two coherent subsystems are recovered here, faithfully (1:1) from the
// gilde.exe pseudocode:
//
//  A) The .esc binary token parser (0x5e3bd0..0x5e3f54).  The compiled `.esc`
//     file is a stream of single-byte "token" codes; ReadToken pulls one,
//     FindTokenHandler maps it through a 12-byte-stride handler table, and
//     ParseBlock walks nested blocks (token 0x28 '(' opens, 0x2F '/' and 0x2B
//     '+' close, 0x27 "'" is end-of-stream/error).  The leaf readers
//     (ReadNameField/ReadMeshField/ReadTextureField/ReadByteFieldA/B/
//     ReadFlagBitsField/ReadFlagBitsField2) deserialize fields of a 224-byte
//     record at  recordBase + 224*index  (index in ctx+8, base in ctx+16).
//
//  B) The script context-table scan/lookup helpers and trivial command bodies
//     (0x442174 FindByHandle, 0x4421b8 FindByName, 0x445b70 FindCommandByName,
//     0x445d40 CountActive, 0x445370 FreeFinished, 0x441660 SkipToSemicolon,
//     0x44191c FindLogicalOperator, plus the constant-returning command stubs
//     and thunks).  These index the 2584-byte/128-slot context table
//     (dword_62E8A4) and the 52-byte/256-slot command table (dword_62E8AC).
//
// Host-engine leaves (Vfs read, Bio read, Util str*, DestroyContext, RunMain,
// RunFrameLoop) are forward-declared and provided by other modules; the tests
// supply tiny stubs.
#include "guild/common/types.h"
#include "sim/script_vm.h"
#include <cstddef>
#include <cstdint>

namespace guild::sim {

// ===========================================================================
// A) .esc binary token parser
// ===========================================================================

// Token control codes recovered from ReadToken / ParseBlock comparisons.
//   ReadToken returns 43 (0x2B '+') on stream EOF, 39 (0x27 "'") if the raw
//   byte exceeds 0x3A.  ParseBlock treats 0x27 as error/end, 0x2F '/' and 0x2B
//   as block-close, 0x28 '(' as the "open nested block" handler entry.
constexpr u8 kEscTokEof      = 43;   // 0x2B  ('+')  also a block terminator
constexpr u8 kEscTokError    = 39;   // 0x27  (''')  not-found / out-of-range
constexpr u8 kEscTokOpen     = 40;   // 0x28  ('(')  open nested block
constexpr u8 kEscTokClose    = 47;   // 0x2F  ('/')  close block
constexpr u8 kEscMaxToken    = 0x3A; // ReadToken rejects raw bytes above this
constexpr int kEscHandlerStride = 12;   // handler-table entry stride (bytes)
constexpr int kEscHandlerMax    = 64;   // FindTokenHandler scans <64 entries

// One entry of the token-handler table (12 bytes, matched against the IDA
// pseudocode's `&a2[12*i]` layout):
//   +0  (byte) token code this entry handles (0x28 is the sentinel/open code)
//   +4  (ptr)  child handler table (used when this is a block-open entry)
//   +8  (ptr)  leaf handler fn  -> "has a body"; when null & no-child, recurse
// Note: the pseudocode reads v6[1] as the fn ptr for the open ('(') entry, and
// v10[1]/v10[2] (offsets +4/+8) for ordinary entries; we keep both meanings.
struct EscHandler {
    u8           token;        // +0  matched token byte
    u8           _pad[3];      // +1
    EscHandler*  childTable;   // +4  nested block handler table
    int (*leafFn)();           // +8  leaf reader (null => recurse into block)
};

// The parser walks a stream while reading record fields into a 224-byte record
// array.  We model the original's ctx pointer (ecx in the readers) explicitly:
//   ctx+8  -> record index (incremented by IncrementCounter)
//   ctx+16 -> base address of the record array (224-byte stride)
struct EscParseCtx {
    int          stream;       // +0/+4 region: VFS stream handle (passed as a1)
    i32          recordIndex;  // +8   current record index
    i32          _pad;         // +12
    u8*          recordBase;   // +16  base of the 224-byte record array
};

// ---------------------------------------------------------------------------
// Stream-reader hooks (the .esc parser's only cross-module leaves).
//
// The original calls the VFS/Bio byte-stream readers by an opaque `int stream`
// handle (the parser threads it through ParseBlock/ReadToken/the field readers).
// In this reconstruction the byte/string readers are routed through an
// installable hooks struct rather than bare extern symbols, exactly like
// CutsceneMiscHooks:
//   * The library provides INERT DEFAULTS (read nothing -> immediate EOF), so
//     every TU that pulls in script_import links cleanly with no test stubs.
//   * Both the unit and e2e tests install their own readers backed by an
//     in-memory stream they own. The stream object is reached through the same
//     `int stream` handle the readers receive, so no library symbol (no bare
//     `g_stream` global) is ever defined by a test.
//
// The string utilities (StrChr/StrCmp/StrCmpNoCase) are NOT hooks: they
// delegate directly to the real reconstructed guild::util primitives, which
// already live in the library.
//
//   ReadStream(buf, size, stream, count) -> bytes read (0 on EOF/short read).
//     gilde.exe 0x4514ac VIBE_Vfs_ReadStream (delegated to via this hook).
//   ReadByte(stream, out)   -> nonzero on success. gilde.exe 0x5dc850.
//   ReadString(stream, out) -> read a NUL-terminated string. gilde.exe 0x5dc86c.
struct ScriptImportHooks {
    u32  (*readStream)(u8* buf, u32 size, int stream, int count) = nullptr;
    u32  (*readByte)  (int stream, u8* out) = nullptr;
    u32  (*readString)(int stream, u8* out) = nullptr;
};

// Install the stream-reader hooks (nullptr restores the inert defaults). The
// installed pointer is borrowed; the caller owns the struct's lifetime.
void SetScriptImportHooks(const ScriptImportHooks* hooks);
const ScriptImportHooks& GetScriptImportHooks();

// gilde.exe 0x5e3bd0 — VIBE_Script_ReadToken(stream@<eax>, ctx@<ecx>)
// Reads one byte from the stream. Returns 43 (0x2B) on EOF, 39 (0x27) if the
// byte is > 0x3A, else the byte itself. (ctx is captured but unused.)
u8 EscReadToken(int stream);

// gilde.exe 0x5e3c0c — VIBE_Script_FindTokenHandler(token@<al>, table@<edx>)
// Linear scan of the handler table for `token`; on a 0x28 ('(') entry it stops
// early (treated as a wildcard/sentinel). Returns the index, or 39 (0x27) if
// not found within 64 entries.
int EscFindTokenHandler(u8 token, const EscHandler* table);

// gilde.exe 0x5e3d28 — VIBE_Script_IncrementCounter(ctx) : ++ctx->recordIndex.
void EscIncrementCounter(EscParseCtx* ctx);

// The 224-byte record field readers (gilde.exe 0x5e3d40..0x5e3f54). Each writes
// into  ctx->recordBase + 224*ctx->recordIndex + <field offset>.
//   ReadNameField     +0   (string, truncated at '.')
//   ReadMeshField     +64  (string, truncated at '.')
//   ReadTextureField  +128 (string, truncated at '.')
//   ReadByteFieldA    +192 (raw byte)
//   ReadByteFieldB    +193 (raw byte)
//   ReadFlagBitsField byte -> +194 bit0, +197 bit1, +198 bits2..5(&0xF), +195 bit6
//   ReadFlagBitsField2 byte -> +196 bit0, +199 = 2*(byte>>1)
constexpr int kEscRecordStride = 224;
void EscReadNameField   (int stream, EscParseCtx* ctx);
void EscReadMeshField   (int stream, EscParseCtx* ctx);
void EscReadTextureField(int stream, EscParseCtx* ctx);
void EscReadByteFieldA  (int stream, EscParseCtx* ctx);
void EscReadByteFieldB  (int stream, EscParseCtx* ctx);
void EscReadFlagBitsField (int stream, EscParseCtx* ctx);
void EscReadFlagBitsField2(int stream, EscParseCtx* ctx);

// Nesting depth counter (gilde.exe dword_64A35C). Bumped on block recursion.
extern int g_escBlockDepth;

// gilde.exe 0x5e3c44 — VIBE_Script_ParseBlock(stream@<eax>, table@<edx>, ctx@<ecx>)
// Reads tokens until a close (0x2F/0x2B) or error (0x27); dispatches each token
// through the handler table, invoking leaf fns or recursing into child blocks.
// Returns the last token byte read.
u8 EscParseBlock(int stream, EscHandler* table, EscParseCtx* ctx);

// ===========================================================================
// B) Context / command table scan + lookup helpers, and command stubs.
// ===========================================================================
//
// The originals use raw process globals (dword_62E8A4 context table base,
// dword_62E8AC command table base, dword_62E8A8 "current" context). Here those
// bases are injected as pointers so the byte-exact pointer arithmetic is
// reproduced without a fixed image layout.

// Context table: 128 slots * 2584 bytes (matches script_vm.h constants).
//   +128 handle (-1 == free) ; +132 owner ; +164 run flags (bit0 runnable)
//   +0   in-use byte
//   +152 source cursor ; +156 src len ; +160 src base  (SkipToSemicolon)

// gilde.exe 0x442174 — VIBE_Script_FindByHandle(handle@<eax>)
// Linear scan of the context table for slot with +128 == handle. Returns the
// slot base pointer, or nullptr if handle==-1 or not found.
u8* FindByHandle(u8* ctxTableBase, i32 handle);

// gilde.exe 0x4421b8 — VIBE_Script_FindByName(name@<eax>)
// Case-insensitive name scan over the context table (StrCmpNoCase against the
// slot start). Returns slot base or nullptr.
u8* FindByName(u8* ctxTableBase, const char* name);

// gilde.exe 0x445b70 — VIBE_Script_FindCommandByName(name@<eax>)
// Scan the 256-slot * 52-byte command table by name (StrCmp). Returns the
// command record base or nullptr.
u8* FindCommandByName(u8* cmdTableBase, const char* name);

// (VIBE_Script_CountActive 0x445d40, VIBE_Script_FreeFinished 0x445370 and
//  VIBE_Script_SkipToSemicolon 0x441660 are already translated in
//  script_vm.cpp / script_compiler.cpp — reused, not re-defined here.)

// gilde.exe 0x44191c — VIBE_Script_FindLogicalOperator(p@<eax>, end@<ebx>)
// Scan [p,end) for a top-level "&&" or "||"; bails immediately if the first one
// or two bytes already form one. Stops at ')' ';' '(' '{'. Returns the pointer
// to the operator, or nullptr.
const char* FindLogicalOperator(const char* p, const char* end);

// Constant-returning command bodies / stubs.
i32 CmdReturnTrue();   // gilde.exe 0x43c650 — returns 1
i32 CmdReturnFalse();  // gilde.exe 0x43c658 — returns 0
i32 NullStub();        // gilde.exe 0x442598 — returns 0

// Current-handle reset (gilde.exe 0x445d7c, dword_62E8D4 = -1). Modeled as a
// caller-provided i32 to avoid a duplicate global.
void ResetCurrentHandle(i32* currentHandle);

} // namespace guild::sim
