#include "crt/runtime.h"

namespace guild::crt {

// ---------------------------------------------------------------------------
// (1) Priority-tagged init/term registry.
// ---------------------------------------------------------------------------

void PriorityInitTable::RegisterInit(u8 priority, CrtInitFunc fn) {
    init_.push_back(PrioritySlot{0, priority, fn});
}

void PriorityInitTable::RegisterExit(u8 priority, CrtInitFunc fn) {
    exit_.push_back(PrioritySlot{0, priority, fn});
}

// VIBE_Crt_InvokeExitFunc @0x5fe210:
//   if ( *result ) return (*result)();  else return result;
void InvokeExitFunc(PrioritySlot& slot) {
    if (slot.func)
        slot.func();
}

// VIBE_Crt_RunInitFuncs @0x5fe21c:
//   while (1) {
//     v3 = end; i = floor;
//     for (p = start; p < end; p += 6)
//       if (p->state != 2 && i >= p->priority) { v3 = p; i = p->priority; }
//     if (v3 == end) break;            // nothing selectable -> done
//     InvokeExitFunc(v3->func); v3->state = 2;
//   }
// The floor `i` starts at the caller's value and ratchets DOWN to the lowest
// pending priority, so each pass runs the current minimum and the next pass
// re-floors from the caller value again — i.e. ascending priority order.
void PriorityInitTable::RunInitFuncs(u8 floor) {
    while (true) {
        PrioritySlot* sel = nullptr;
        u8 i = floor;
        for (PrioritySlot& s : init_) {
            if (s.state != 2 && i >= s.priority) {
                sel = &s;
                i = s.priority;
            }
        }
        if (!sel)
            break;
        InvokeExitFunc(*sel);
        sel->state = 2;
    }
}

// VIBE_Crt_RunExitFuncs @0x5fe26c:
//   while (1) {
//     v5 = end; i = floor;
//     for (p = start; p < end; p += 6)
//       if (p->state != 2 && i <= p->priority) { v5 = p; i = p->priority; }
//     if (v5 == end) break;
//     if (gate >= v5->priority) InvokeExitFunc(v5->func);
//     v5->state = 2;
//   }
// `i` starts at the caller's floor and ratchets UP to the highest pending
// priority >= floor, so entries fire in descending priority order; the `gate`
// (dh) suppresses the call for entries whose priority exceeds it (used by
// DoExit to drop the high band first while still marking it done).
void PriorityInitTable::RunExitFuncs(u8 floor, u8 gate) {
    while (true) {
        PrioritySlot* sel = nullptr;
        u8 i = floor;
        for (PrioritySlot& s : exit_) {
            if (s.state != 2 && i <= s.priority) {
                sel = &s;
                i = s.priority;
            }
        }
        if (!sel)
            break;
        if (gate >= sel->priority)
            InvokeExitFunc(*sel);
        sel->state = 2;
    }
}

// ---------------------------------------------------------------------------
// (2) atexit / exit machinery.
// ---------------------------------------------------------------------------

// VIBE_Crt_RunInitializerTable @0x142131a:
//   while (a1 < a2) { if (*a1) (*a1)(); ++a1; }
void RunInitializerTable(CrtFunc* begin, CrtFunc* end) {
    for (CrtFunc* p = begin; p < end; ++p) {
        if (*p)
            (*p)();
    }
}

// __onexit-style registrar (the original pushes onto the grow-down array whose
// top is dword_1466070; the registrar itself is outside this owned set).
CrtFunc ExitRegistry::Atexit(CrtFunc fn) {
    handlers_.push_back(fn);
    return fn;
}

// VIBE_Crt_DoExit_21281 @0x1421281:
//   dword_145A220 = 1; byte_145A21C = skipFinal;
//   if (!quick) {
//     if (onexit_top) {                       // dword_1466074 (base) != 0
//       v4 = onexit_top - 1;                  // (top-4)/4 last entry
//       if (v4 >= base) do { if (*v4) (*v4)(); --v4; } while (v4 >= base);
//     }
//     RunInitializerTable(preTerm.begin, preTerm.end);  // 142C114..142C11C
//   }
//   RunInitializerTable(term.begin, term.end);          // 142C120..142C124
//   if (!skipFinal) { dword_145A224 = 1; /* final OS exit hook */ }
void ExitRegistry::DoExit(int code, int quick, int skipFinal) {
    exitCode_ = code;
    terminating_ = true;
    skipFinal_ = skipFinal;
    if (!quick) {
        // LIFO walk of the onexit array: top-1 down to base.
        for (std::size_t n = handlers_.size(); n-- > 0;) {
            if (handlers_[n]) {
                handlers_[n]();
                fireOrder_.push_back(handlers_[n]);
            }
        }
        for (CrtFunc fn : preTerm_) {
            if (fn) {
                fn();
                fireOrder_.push_back(fn);
            }
        }
    }
    for (CrtFunc fn : term_) {
        if (fn) {
            fn();
            fireOrder_.push_back(fn);
        }
    }
    // (!skipFinal): the original calls the final OS terminate hook here; that
    // OS leaf is shimmed away in this hosted reimpl.
}

// VIBE_Crt_Exit @0x142125f.
void ExitRegistry::Exit(int code) { DoExit(code, 0, 0); }

// VIBE_Crt_QuickExit @0x1421270.
void ExitRegistry::QuickExit(int code) { DoExit(code, 1, 0); }

// VIBE_Crt_RunExitHandlers @0x1421232:
//   if (off_145298C) off_145298C();
//   RunInitializerTable(preTerm); RunInitializerTable(term);
void ExitRegistry::RunExitHandlers() {
    if (preExitHook_) {
        preExitHook_();
        fireOrder_.push_back(preExitHook_);
    }
    for (CrtFunc fn : preTerm_) {
        if (fn) {
            fn();
            fireOrder_.push_back(fn);
        }
    }
    for (CrtFunc fn : term_) {
        if (fn) {
            fn();
            fireOrder_.push_back(fn);
        }
    }
}

// ---------------------------------------------------------------------------
// Stack probe.
// ---------------------------------------------------------------------------

// VIBE_Crt_StackProbe @0x5fe350 — reproduce the integer residual of the
// page-touch loop without touching the stack:
//   result = size; do { ... ; result -= 4096; } while (result > 4096 at entry);
// The loop body runs while the pre-decrement value was > 4096, so the returned
// residual is size - 4096*k where k is the smallest count making it <= 0
// (i.e. the value after the iteration whose pre-value was in (0,4096]).
int StackProbe(int size) {
    int result = size;
    bool stop;
    do {
        stop = (result <= 4096);
        result -= 4096;
    } while (!stop);
    return result;
}

// ---------------------------------------------------------------------------
// errno.
// ---------------------------------------------------------------------------

// VIBE_Runtime_SetErrnoEinval @0x5fb1c0 — *(off_64A90C()+4) = EINVAL.
void SetErrnoEinval() { Errno() = kEINVAL; }

} // namespace guild::crt
