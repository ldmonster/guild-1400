#include "crt/exception.h"

namespace guild::crt {

// ---------------------------------------------------------------------------
// SEH scope walk.
// ---------------------------------------------------------------------------

EhSavedState& EhSaved() {
    static EhSavedState s;
    return s;
}

// VIBE_Eh_SaveRegistration @0x1426932:
//   unk_14550A0 = *(frame+8);  unk_145509C = code;  unk_14550A4 = frame;
int EhSaveRegistration(int code, ExceptionFrame* frame, int /*disposition*/) {
    EhSaved().scopeTable = frame ? frame->scopeTable : nullptr;
    EhSaved().code = code;
    EhSaved().frame = frame;
    return code;
}

// VIBE_Eh_UnwindNestedHandlers @0x142689e:
//   while (1) {
//     scope = frame->scopeTable; lvl = frame->tryLevel;
//     if (lvl == -1 || lvl == target) break;
//     frame->tryLevel = scope[lvl].enclosingLevel;     // pop one level
//     if (scope[lvl].filter == 0) {                    // __finally
//       SaveRegistration(scope[lvl].handler, frame, 257);
//       scope[lvl].handler(ExceptionList, &cookie);    // run __finally body
//     }
//   }
//   return frame;
ExceptionFrame* EhUnwindNestedHandlers(ExceptionFrame* frame, int targetLevel) {
    while (true) {
        ScopeTableEntry* scope = frame->scopeTable;
        int lvl = frame->tryLevel;
        if (lvl == -1 || lvl == targetLevel)
            break;
        ScopeTableEntry& e = scope[lvl];
        frame->tryLevel = e.enclosingLevel; // pop to enclosing level
        if (e.filter == nullptr) {
            // A filter of 0 marks a __finally; run its body during unwind.
            EhSaveRegistration(reinterpret_cast<std::intptr_t>(e.handler), frame, 257);
            if (e.handler)
                e.handler();
        }
    }
    return frame;
}

// VIBE_Eh_RunFrameHandlers @0x1426954.
//   if (excFlags & 6) { UnwindNestedHandlers(frame, -1); return 1; }
//   // link this dispatcher onto the frame, then:
//   lvl = frame->tryLevel; scope = frame->scopeTable;
//   while (lvl != -1) {
//     if (scope[lvl].filter) {
//       r = scope[lvl].filter();
//       if (r) {
//         if (r < 0) return 0;                 // EXCEPTION_CONTINUE_EXECUTION
//         DoExitThunk(frame);                  // EXCEPTION_EXECUTE_HANDLER:
//         UnwindNestedHandlers(frame, lvl);    //   unwind inner levels,
//         SaveRegistration(scope[lvl].handler, frame, 1);
//         frame->tryLevel = scope[lvl].enclosingLevel;
//         scope[lvl].handler();                //   run the __except body
//       }
//     }
//     scope = frame->scopeTable;
//     lvl = scope[lvl].enclosingLevel;
//   }
//   return 1;
//
// The original's handler invocation does not return (it longjmps into the
// __except body); here we return 0 to signal "claimed here" and stop the walk.
int EhRunFrameHandlers(int excFlags, ExceptionFrame* frame, void* /*dispatcher*/) {
    if ((excFlags & 6) != 0) {
        EhUnwindNestedHandlers(frame, -1);
        return 1;
    }
    int lvl = frame->tryLevel;
    while (lvl != -1) {
        ScopeTableEntry* scope = frame->scopeTable;
        ScopeTableEntry& e = scope[lvl];
        if (e.filter) {
            // Run the filter. The real filter returns an EXCEPTION_* code; here
            // the filter encodes its result in its (void) call, so we treat a
            // present filter+handler pair as "execute handler" once invoked.
            e.filter();
            // EXCEPTION_EXECUTE_HANDLER path: unwind inner levels then run body.
            EhUnwindNestedHandlers(frame, lvl);
            EhSaveRegistration(reinterpret_cast<std::intptr_t>(e.handler), frame, 1);
            frame->tryLevel = e.enclosingLevel;
            if (e.handler)
                e.handler();
            return 0; // claimed here
        }
        lvl = scope[lvl].enclosingLevel;
    }
    return 1; // not claimed — pass to the next outer frame
}

// ---------------------------------------------------------------------------
// FP exception classification.
// ---------------------------------------------------------------------------

// VIBE_FpException_ClassToType @0x1424114.
int FpClassToType(u8 fpclass) {
    if (fpclass & 0x20)
        return 5;
    if (fpclass & 0x08)
        return 1;
    if (fpclass & 0x04)
        return 2;
    if (fpclass & 0x01)
        return 3;
    return 2 * (fpclass & 2); // residual: 0 for most, 4 when bit 1 is set
}

int& MathErrno() {
    static int slot = 0; // dword_145A1DC
    return slot;
}

// VIBE_FpException_SetErrno @0x14240c9:
//   if (a1 == 1) dword_145A1DC = 33;            // EDOM
//   else if (a1 > 1 && a1 <= 3) dword_145A1DC = 34;  // ERANGE
//   return a1;
int FpSetErrno(int type) {
    if (type == 1)
        MathErrno() = kEDOM;
    else if (type > 1 && type <= 3)
        MathErrno() = kERANGE_FP;
    return type;
}

// ---------------------------------------------------------------------------
// OS-leaf shims.
// ---------------------------------------------------------------------------

namespace {
TopLevelExceptionFilter g_topFilter = nullptr;
} // namespace

TopLevelExceptionFilter InstallExceptionHandler(TopLevelExceptionFilter filter) {
    TopLevelExceptionFilter old = g_topFilter;
    g_topFilter = filter;
    return old;
}

void RemoveExceptionHandler() { g_topFilter = nullptr; }

TopLevelExceptionFilter CurrentExceptionFilter() { return g_topFilter; }

} // namespace guild::crt
