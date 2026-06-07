#pragma once
// The .esc compiled symbol table for the Guild scripting engine (gilde.exe).
//
// Namespace: guild::sim  (companion to script_vm.{h,cpp} / script_lexer /
//   script_compiler — the VIBE_Script_* front-end at 0x441140..0x4459bc).
//
// This recovers, byte-for-byte, the on-disk symbol-record layouts the lexer,
// compiler and variable-addressing code touch, and re-implements the three
// table builders/lookups:
//   * VIBE_Script_DefineVariable     0x442c90  — append a variable record
//   * VIBE_Script_LookupVariable     0x4414d4  — resolve a name -> var record
//   * VIBE_Script_LookupFunction     0x4415f8  — resolve a name -> func record
//   * VIBE_Script_AddEventToken      0x441140  — append a *global* var record
//   * VIBE_Script_GetVariableAddress 0x4459bc  — runtime storage end address
//
// Record formats (recovered from DefineVariable / AddEventToken / ParseDecl /
// AssignVariable / EnterFunction):
//
//   Variable record — stride 48 (0x30):
//     +0  (byte)  type nibble: low nibble = type code, high nibble = flags.
//                 The runtime reads it sign-extended: (char)(16*b)>>4 yields the
//                 low nibble as a signed value. Type codes:
//                   1 = int/dword   (elem 4 bytes)
//                   2 = byte/char   (elem 1 byte)
//                   6 = string      (elem 96 bytes)
//                   7 = float       (elem 4 bytes)
//                   5 = void
//     +1  (char[35]) NUL-terminated symbol name (StrCmp key region)
//     +36 (dword) element count (array length; 1 for scalars)
//     +40 (dword) "is-local"/scope marker (1 in DeclareLocal)
//     +44 (dword) storage pointer (heap, elemSize*count) — for our reimpl an
//                 index/handle into the script's flat variable-storage arena.
//
//   Function record — stride 304 (0x130):
//     +0   (char[32]) NUL-terminated function name (StrCmp key region)
//     +32  (dword) source pointer to the function body (the '{' after the sig)
//     +36  (byte)  parameter count
//     +37  (byte[?]) per-parameter type codes (one per param, written by
//                   ParseSymbolName at +37+i)
//     +45  (char[32*N]) per-parameter names (32 bytes each, from ParseSymbolName)
//
//   Global variable table (VIBE_Script_AddEventToken): same 48-byte record,
//     dynamically grown; base dword_767944, count dword_767948, cap dword_76794C.
#include "guild/common/types.h"
#include <string>
#include <vector>
#include <cstring>

namespace guild::sim {

// ---- recovered strides / type codes -------------------------------------
constexpr int kVarRecordStride  = 48;    // 0x30  (DefineVariable)
constexpr int kFuncRecordStride = 304;   // 0x130 (CompileBlock 304*count)
constexpr int kVarNameOffset    = 1;     // +1  name region
constexpr int kVarCountOffset   = 36;    // +36 element count
constexpr int kVarLocalOffset   = 40;    // +40 is-local
constexpr int kVarStorageOffset = 44;    // +44 storage ptr
constexpr int kFuncNameOffset   = 0;     // +0  name region
constexpr int kFuncBodyOffset   = 32;    // +32 body source ptr
constexpr int kFuncParamCntOff  = 36;    // +36 param count
constexpr int kFuncParamTypeOff = 37;    // +37 param type codes
constexpr int kFuncParamNameOff = 45;    // +45 param names (32 B stride)

// Variable type codes (VIBE_Script_DefineVariable / DeclareLocal / ParseTypeKeyword).
enum ScriptVarType : u8 {
    kVarVoid   = 5,   // "void"
    kVarInt    = 1,   // "int"
    kVarByte   = 2,   // "byte"/"char"
    kVarString = 6,   // "string"
    kVarFloat  = 7,   // "float"
};

// Element size in bytes for a type code (DefineVariable's a4 switch).
inline int VarElemSize(u8 type) {
    switch (type) {
        case kVarInt:    return 4;
        case kVarByte:   return 1;
        case kVarString: return 96;
        case kVarFloat:  return 4;
        case kVarVoid:   return 4;   // case 5 falls into the 4-byte path
        default:         return 4;
    }
}

// A compiled variable symbol (one 48-byte record, kept as a value here).
struct ScriptVar {
    u8          typeNibble = 0;   // +0  (low nibble = type, high = flags)
    std::string name;             // +1
    i32         count = 1;        // +36 element count
    i32         isLocal = 0;      // +40
    i32         storage = 0;      // +44 storage handle (index into arena)

    u8 type() const { return typeNibble & 0x0F; }
};

// A compiled function symbol (one 304-byte record).
struct ScriptFunc {
    std::string         name;                 // +0
    int                 bodyCursor = -1;      // +32 body source offset
    std::vector<u8>     paramTypes;           // +37
    std::vector<std::string> paramNames;      // +45
    int paramCount() const { return (int)paramTypes.size(); }
};

// The compiled symbol table for one script (the per-context +136/+140/+144/+148
// fields). Variables and functions are appended in declaration order, exactly as
// DefineVariable / ParseSymbolName build the original flat arrays.
class ScriptSymbols {
public:
    // gilde.exe 0x442c90 — VIBE_Script_DefineVariable(type@<edx>, count@<ebx>).
    // Appends a variable record; sets its type nibble, element count and a
    // freshly-allocated storage handle. Returns the new record's index.
    int DefineVariable(const std::string& name, u8 type, i32 count);

    // gilde.exe 0x4414d4 — VIBE_Script_LookupVariable: linear StrCmp scan over
    // the local var array, then (deferred block-scope) the global table. Returns
    // the variable index, or -1 if not found.
    int LookupVariable(const std::string& name) const;

    // gilde.exe 0x4415f8 — VIBE_Script_LookupFunction: linear StrCmp scan over
    // the function array. Returns the function index, or -1.
    int LookupFunction(const std::string& name) const;

    // Append a function record (CompileBlock + ParseSymbolName path). Returns idx.
    int DefineFunction(const std::string& name, int bodyCursor);

    // gilde.exe 0x4459bc — VIBE_Script_GetVariableAddress: the end offset of the
    // flat variable-storage arena = sum of 4*count over every variable (the
    // original adds the per-element *4 over all vars to size the runtime block).
    // Returns the arena size in dwords-as-bytes used by the originals.
    int VariableStorageEnd() const;

    std::vector<ScriptVar>&        vars()        { return vars_; }
    const std::vector<ScriptVar>&  vars() const  { return vars_; }
    std::vector<ScriptFunc>&       funcs()       { return funcs_; }
    const std::vector<ScriptFunc>& funcs() const { return funcs_; }

    // Flat per-script variable storage arena (mirrors the heap region the
    // original allocs at +44 and addresses via GetVariableAddress). Indexed by
    // the var's storage handle + element index.
    std::vector<i32>& storage() { return storage_; }
    const std::vector<i32>& storage() const { return storage_; }

private:
    std::vector<ScriptVar>  vars_;
    std::vector<ScriptFunc> funcs_;
    std::vector<i32>        storage_;
};

} // namespace guild::sim
