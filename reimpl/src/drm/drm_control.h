#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::drm — DRM control-flow / anti-debug exception dispatch (1:1 recon)
// =============================================================================
//
// SCOPE
//   This unit reconstructs the *pure control-flow / state-machine / dispatch*
//   logic of gilde.exe's SecuROM-class protection cluster (the `0x140xxxx`
//   overlay). It is the orchestration spine that sits on top of the disc-I/O
//   helpers stubbed in drm_stub.{h,cpp}: the vectored-exception "opcode-VM"
//   exception dispatchers, the user-message / cleanup state dispatcher, the
//   key-action dispatcher and the timing gate.
//
//   The reconstruction reproduces the EXACT branch conditions, exception-code
//   comparisons, CONTEXT/EXCEPTION_RECORD field offsets, state values and
//   transitions, dispatch-table indices and integer wraparound of the original.
//
// OS / ANTI-DEBUG COUPLINGS (PLAN §3-§6, §8)
//   The original wires these into Win32: AddVectoredExceptionHandler, the
//   SEH chain (NtCurrentTeb()->NtTib.ExceptionList), RaiseException / int3 /
//   ud0 traps, QueryPerformanceCounter timing and a CONTEXT record mutated in
//   place to redirect Eip. None of that exists headlessly. Per PLAN we DO NOT
//   fake the protection logic — instead we model the dispatch as a PURE state
//   machine over an injected "exception event" (code + a plain CONTEXT struct),
//   and route the few OS/timing/exit primitives through an INERT-DEFAULT
//   `DrmOs` hooks struct. The default hooks are no-ops (timer returns a caller
//   supplied value, Exit records the code instead of terminating), so the whole
//   machine runs and is asserted deterministically in tests.
//
// PROVENANCE
//   Every routine keeps its `// gilde.exe 0xADDR — VIBE_Name` provenance.
//   CONTEXT offsets are the standard x86 CONTEXT layout (Dr0=4 .. Dr7=0x18,
//   ExtendedRegisters/FloatSave region, SegGs..SegSs, Edi..Eax, Ebp,
//   Eip=0xB8=184, SegCs, EFlags=0xC0=192, Esp, SegSs). EXCEPTION_RECORD:
//   ExceptionCode=+0, ExceptionFlags=+4, ExceptionRecord=+8,
//   ExceptionAddress=+12, NumberParameters=+16, ExceptionInformation=+20.
// =============================================================================

namespace guild::drm {

// -----------------------------------------------------------------------------
// NT exception status codes the dispatchers compare against (the only constants
// that drive the exception state machine). Values are the literal STATUS_*
// dwords the original switch()es on.
// -----------------------------------------------------------------------------
constexpr u32 kExcBreakpoint     = 0x80000003u; // STATUS_BREAKPOINT (int3)
constexpr u32 kExcSingleStep     = 0x80000004u; // STATUS_SINGLE_STEP (TF)
constexpr u32 kExcAccessViol     = 0xC0000005u; // STATUS_ACCESS_VIOLATION
constexpr u32 kExcIntDivByZero   = 0xC0000094u; // STATUS_INTEGER_DIVIDE_BY_ZERO
constexpr u32 kExcFltInvalidOp   = 0xC0000090u; // STATUS_FLOAT_INVALID_OPERATION

// VEH return codes (EXCEPTION_CONTINUE_EXECUTION / _CONTINUE_SEARCH).
constexpr int kContinueExecution = 0; // handled — resume at (possibly) new Eip
constexpr int kContinueSearch    = 1; // not ours — pass to next handler

// VIBE_Drm_CheckExceptionCode sentinels.
constexpr int kExcCodeMatch   = -1; // first-chance code == STATUS_FLOAT_INVALID_OPERATION
constexpr int kExcCodeNoMatch = 0;

// -----------------------------------------------------------------------------
// Minimal CONTEXT mirror. Only the dword slots the dispatchers touch are named;
// the byte offsets match the original x86 CONTEXT exactly so the field math
// (a3 + 4/8/.../184/192/...) is reproduced 1:1. Indexed in dwords for clarity:
//   off 4   = Dr0   (idx 1)    off 172 = Edx (0xAC, idx 43)
//   off 8   = Dr1   (idx 2)    off 176 = Ecx (0xB0, idx 44)
//   off 12  = Dr2   (idx 3)    off 184 = Eip (0xB8, idx 46)
//   off 16  = Dr3   (idx 4)    off 192 = EFlags (0xC0, idx 48)
//   off 20  = Dr6   (idx 5)
//   off 24  = Dr7   (idx 6)    off 164 = Eax-region ptr slot (0xA4, idx 41)
//   off 196 = Esp (0xC4, idx 49)
// We keep a raw dword[64] image so byte offsets stay exact and so the original
// pointer reads (e.g. *(Eip) to fetch the trapped opcode byte) can be emulated
// by an injected "opcode at Eip" value when needed.
// -----------------------------------------------------------------------------
struct DrmContext {
    u32 dr0 = 0;   // +4
    u32 dr1 = 0;   // +8
    u32 dr2 = 0;   // +12
    u32 dr3 = 0;   // +16
    u32 dr6 = 0;   // +20
    u32 dr7 = 0;   // +24
    u32 edx = 0;   // +172
    u32 ecx = 0;   // +176
    u32 eip = 0;   // +184
    u32 eflags = 0;// +192
    u32 esp = 0;   // +196

    // Emulation aids (stand in for the original's raw memory reads):
    // byteAtEip = *(BYTE*)Eip  (the trapped opcode), used by the SINGLE_STEP arm.
    u8  byteAtEip = 0;
    // dwordAtEsp = **(DWORD**)(Eip)/(Esp+196 deref). The original reads the
    // return address off the stack (`**(_DWORD**)(a3+196)`); we model it directly.
    u32 returnTarget = 0;
};

// Injected exception event fed to the dispatchers.
struct DrmExceptionEvent {
    u32 code = 0;        // EXCEPTION_RECORD.ExceptionCode
    DrmContext ctx{};    // the CONTEXT to be mutated in place
};

// -----------------------------------------------------------------------------
// Mutable global protection state shared by the dispatchers (the `dword_*`
// flags the original keeps in its data segment). Centralised here so the state
// machine is self-contained and testable. Field names map to the original
// globals; comments give the address.
// -----------------------------------------------------------------------------
struct DrmState {
    int  guardArmed       = 0; // dword_145A11C — "instrumentation arm" flag
    int  stepPhase        = 0; // dword_1459F9C — single-step phase (0/1/2)
    int  resetCounters    = 0; // dword_1451468 — one-shot counter reset request
    u32  rangeLo          = 0; // off_145A804 — VM code range low  (StateDispatch ptr)
    u32  rangeHi          = 0; // off_145A54C — VM code range high (loc_141557D)
    u8   sumByteA         = 0; // byte_145A124 — rolling opcode-byte counters
    u32  sumDword         = 0; // dword_145A128
    u8   sumByteB         = 0; // byte_145A12C
    u32  xorAccLo         = 0; // dword_142DD60 — checksum-feedback accumulators
    u32  xorAccHi         = 0; // dword_142DD64

    // Timing gate state (VerifyTimerOrExit / opcode-VM heuristics):
    u32  timerCalibration = 0; // dword_142DDBC — calibrated tick budget (>50 enables)
    u32  timerSampleCount = 0; // dword_1459FA0 — observed VM-instruction count
    u16  vmTableSize      = 0; // word_14501E0 — VM dispatch-table modulus
};

// -----------------------------------------------------------------------------
// INERT-DEFAULT OS hooks. The default implementation never touches the OS:
//   - RandomMod returns a deterministic value (seed % modulus by default),
//   - PerfCounter returns a monotonically supplied value,
//   - Exit records the exit code (and sets `exited`) instead of terminating,
//   - RaiseUndefined records that ud0/illegal-instruction was hit.
// Tests inject their own subclass to assert/observe couplings.
// -----------------------------------------------------------------------------
struct DrmOs {
    bool exited = false;
    int  lastExitCode = 0;
    bool undefinedTrapped = false;

    virtual ~DrmOs() = default;

    // VIBE_Util_RandomMod @0x140b530 — returns value in [0, modulus).
    virtual u32 RandomMod(u32 seed, u32 modulus) {
        return modulus ? (seed % modulus) : 0u;
    }
    // QueryPerformanceCounter / dword_145E2FC timing source. Default: 0.
    virtual u32 PerfCounter() { return 0u; }
    // VIBE_Crt_Exit @0x142125f — terminates the process. Inert: record only.
    virtual void Exit(int code) { exited = true; lastExitCode = code; }
    // ud0 illegal-instruction trap (VIBE_Drm_TrapUndefined). Inert: record only.
    virtual void TrapUndefined() { undefinedTrapped = true; }
};

// =============================================================================
// Reconstructed entry points (pure logic; OS coupling via DrmOs).
// =============================================================================

// gilde.exe 0x140b0b0 — VIBE_Drm_TrapUndefined  (__noreturn)
// Original: executes `ud0` (UD0), raising STATUS_ILLEGAL_INSTRUCTION; never
// returns. The protection's exception dispatcher catches the resulting trap.
// Modelled by routing to DrmOs::TrapUndefined (inert default records the hit).
[[noreturn]] void TrapUndefined(DrmOs& os);

// gilde.exe 0x140b570 — VIBE_Drm_CheckExceptionCode  (__stdcall)
// First-chance code filter: returns -1 iff the EXCEPTION_RECORD code is
// STATUS_FLOAT_INVALID_OPERATION (0xC0000090), else 0. (The Hex-Rays
// "-1073741680" literal is 0xC0000090.)
int CheckExceptionCode(u32 exceptionCode);

// gilde.exe 0x140b0c0 — VIBE_Drm_ExceptionDispatch  (__cdecl)
// The primary opcode-VM exception handler. Pure state machine over the injected
// event + DrmState; mutates ev.ctx in place and returns 0 (continue execution)
// for the four handled codes, 1 (continue search) otherwise.
int ExceptionDispatch(DrmExceptionEvent& ev, DrmState& st, DrmOs& os);

// gilde.exe 0x140f230 — VIBE_Drm_VectoredExceptionHandler  (__cdecl)
// The installed AddVectoredExceptionHandler callback. Same shape as
// ExceptionDispatch but with the access-violation / single-step variant used at
// runtime (handles BREAKPOINT/ACCESS_VIOLATION/SINGLE_STEP; the int3-cc opcode
// rewrite path). Pure state machine; mutates ev.ctx; returns 0/1.
int VectoredExceptionHandler(DrmExceptionEvent& ev, DrmState& st, DrmOs& os);

// gilde.exe 0x1411e30 — VIBE_Drm_VerifyTimerOrExit  (__stdcall)
// Timing gate: if the calibration is unset, or 0.9 * calibration < sampleCount
// (i.e. the VM ran "too slow", indicating a debugger/single-step), it exits with
// code -1; otherwise it decrements and returns the VM table size word.
// Returns the post-decrement vmTableSize (16-bit).
i16 VerifyTimerOrExit(DrmState& st, DrmOs& os);

// -----------------------------------------------------------------------------
// State / message dispatcher.
// -----------------------------------------------------------------------------

// Outcome of VIBE_Drm_StateDispatch, exposing the pure control-flow decisions
// (which decode arm ran, whether the network/handshake branch was taken, and
// whether the resource-cleanup teardown ran) so tests can assert them without
// the original's global side effects.
struct StateDispatchResult {
    bool decodedSmallTable = false; // state<=13: XorDecode127(table[state]) ran
    bool stringArm         = false; // states 0..13 -> VIBE_String_CopyThunk arm
    bool keepAliveArm      = false; // state 44 (0x2C) -> no-op (keep resources)
    bool largeDecodeArm    = false; // default -> XorDecode123 + CopyThunk arm
    bool handshakeEligible = false; // (state<6 || state==13): handshake attempted
    bool teardown          = false; // resource free / sprintf reset path ran
};

// gilde.exe 0x140b590 — VIBE_Drm_StateDispatch  (__stdcall)
// Decodes and dispatches a "protection message" state code. We reconstruct the
// pure control-flow skeleton: the decode-arm selection, the handshake-eligible
// predicate and the teardown decision. `handshakeSucceeded` injects the result
// of the original's network/registry handshake (dword_145A040 != 0 means "stay
// resident, skip teardown"). The many global-pointer frees are represented by
// the `teardown` flag (state != 44 path).
StateDispatchResult StateDispatch(u32 state, bool handshakeSucceeded);

// -----------------------------------------------------------------------------
// Key-action dispatcher (the per-VM-instruction action table).
// -----------------------------------------------------------------------------

// One row of the action table the VM consults (dword_14514A4[10*idx] = opcode,
// dword_1451498[10*idx] = immediate for opcode 2). Modelled as injected rows.
struct DrmKeyActionRow {
    int opcode = 0;     // dword_14514A4[10*a1]
    int immediate = 0;  // dword_1451498[10*a1] (used by opcode 2)
};

// Result of VIBE_Drm_DispatchKeyAction: the value written to *a16 (the "key
// out" slot), whether the VM redirect slot (a7+4) was rewritten and to what,
// and the always-1 return (handled).
struct DrmKeyActionResult {
    int  keyOut       = -1;   // *a16 (unset == -1 here; original writes 0/1/imm)
    bool keyOutWritten = false;
    bool redirectWritten = false; // *(a7+4) updated?
    u32  redirectValue   = 0;     // value written to *(a7+4)
    int  ret = 1;                 // return value (1 unless default -> 1 too)
};

// gilde.exe 0x1411030 — VIBE_Drm_DispatchKeyAction  (__stdcall)
// Dispatches the VM "key action" opcode. Pure for the opcodes that only set the
// key-out / redirect slots (0,1,2,64); the OS-coupled opcodes (16 resource
// query, 32/33 event signalling, 48/49 thread ops) are represented structurally
// (no OS effect modelled — they all `return 1`). `a13`/`a15` are the redirect
// operands the original threads through (a7+4 = a13 for 0/1/2, = a15 for 64).
DrmKeyActionResult DispatchKeyAction(const DrmKeyActionRow& row, u32 a13, u32 a15);

// -----------------------------------------------------------------------------
// InitProtection step dispatcher (pure outer skeleton).
// -----------------------------------------------------------------------------

// gilde.exe 0x140fa60 — VIBE_Drm_InitProtection  (__stdcall) — PURE PART
// The original is a large per-step verifier driven by the step-type table
// dword_14514A0[10*a1]; each arm does disc I/O / registry / driver work that
// belongs to the disc cluster. The PURE, OS-free piece reconstructed here is
// the LBA/MSF sector-address derivation used by step type 2 (the 0x4B/0x1194
// "75 sectors / 4500 frames" CD timecode math at 0x1410041..0x141020d).
// Returned exactly as computed (BCD-packed MM:SS:FF in the original's layout).
struct DrmSectorAddrs {
    u32 startMsf = 0;  // dword_145A564
    u32 startBcd = 0;  // dword_145DB40
    u32 endMsf   = 0;  // dword_145CB50  (start + 150 frames == +2s lead-in)
    u32 endBcd   = 0;  // dword_145AC48
};
DrmSectorAddrs ComputeSectorAddrs(u32 sectorBase /* dword_145B934 */);

// gilde.exe 0x1411e90 — VIBE_Drm_NopStub3
// gilde.exe 0x1414b00 — VIBE_Drm_NopStub4
// gilde.exe 0x1415590 — VIBE_Drm_NopStub5
//   These are NOT inert no-ops. Each is a 21-byte address-marker sled
//   (push ebp/regs; `mov edi,<lo>; mov esi,<hi>`; pop regs; retn) whose two
//   immediates delimit a low-game-code byte range the overlay decryptor
//   consumes (see drm_stub.h OVERLAY-DECRYPTION FINDING). They have no
//   observable runtime effect (registers are saved/restored and the function
//   returns void), so we reconstruct each as a no-op that *returns its
//   documented [lo,hi) range bounds* for provenance/verification.
struct DrmRangeMarker { u32 lo; u32 hi; };
DrmRangeMarker NopStub3();  // 0x442103 .. 0x442204
DrmRangeMarker NopStub4();  // 0x401001 .. 0x4020AB
DrmRangeMarker NopStub5();  // 0x403001 .. 0x404121

} // namespace guild::drm
