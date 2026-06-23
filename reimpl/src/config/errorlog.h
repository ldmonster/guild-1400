#pragma once
// gilde.exe error/trace logging (VIBE_ErrorLog_* 0x437ab0-0x438ec0,
// VIBE_Trace_* 0x437c00-0x437ea4).
//
// The original ErrorLog routes formatted records to up to four sinks selected
// by a flags byte (byte_62D828): a per-module .log file, a Win32 MessageBox,
// OutputDebugString, and an allocated console (WriteConsole). This module keeps
// the FLAG decode and the record FORMATTING 1:1; the actual sink output is sent
// through an ILogSink (the Win32 substitute) so no OS call lives in src/.
#include "guild/common/types.h"
#include <string>
#include <vector>

namespace guild::config {

// gilde.exe byte_62D828 — ErrorLog sink/format flags.
enum LogFlags : u8 {
    kLogFile    = 0x01, // write to "<exe-dir>\\<module>_error.log"
    kLogMsgBox  = 0x02, // pop a MessageBox
    kLogDebug   = 0x04, // OutputDebugString
    kLogConsole = 0x08, // WriteConsole to an allocated console
    kLogTime    = 0x20, // prefix module-line records with "Time:%8lu\t"
};

// Win32 substitute for the four output sinks. A record is delivered as the
// already-formatted text plus the original's trailing "\n" (the original makes
// two writes: the body, then "\n").
class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void file(const std::string& text) = 0;    // append to the log file
    virtual void msgBox(const std::string& text) = 0;  // MessageBox body
    virtual void debug(const std::string& text) = 0;   // OutputDebugString
    virtual void console(const std::string& text) = 0; // WriteConsole
};

// gilde.exe 0x437ab0 — VIBE_ErrorLog_WriteRecord (eax=text, dl=flags, ebx=channel)
// Dispatches `text` (followed by the original's "\n") to whichever sinks the
// flags select. The original's `ebx` channel id selects among 12 log files; the
// channel name is passed through so the file sink can route it.
void WriteRecord(ILogSink& sink, const std::string& text, u8 flags);

// gilde.exe 0x438da8 — VIBE_ErrorLog_ReportMessage (eax=msg)
// Formats "MESSAGE: %s\n" and, per flags, writes it to the log/debug/console
// sinks and/or pops a MessageBox. Returns the formatted body (for testing).
std::string FormatMessage(ILogSink& sink, u8 flags, const std::string& msg);

// gilde.exe 0x438e3c — VIBE_ErrorLog_ReportModuleLine (eax=module, edx=line, ebx=msg)
// Formats a module/line record. With kLogTime set the prefix is
//   "Time:%8lu\tModule '%s':\tLine:%lu:\t%s"
// otherwise
//   "tModule '%s':\tLine:%lu:\t%s"
// (the leading 't' without time is reproduced verbatim from the original).
// `timeMs` substitutes timeGetTime(). Returns the formatted body.
std::string FormatModuleLine(const std::string& module, unsigned long line,
                             const std::string& msg, bool withTime, unsigned long timeMs);

// --- ErrorLog_Init (subsystem startup) -------------------------------------

// gilde.exe 0x438a98 — VIBE_ErrorLog_Init (__usercall: a1@eax=mainWnd, a2@edx,
// a3@cl=flags, a4@ebx=titleWnd).
//
// Startup of the error-log subsystem. The original:
//   * stores mainWnd@0x62D820, a2@0x62D824, titleWnd@0x764834, flags@0x62D828;
//   * stores the derived bit (flags & 0x10)!=0 at 0x62D829;
//   * SetUnhandledExceptionFilter(TopLevelExceptionFilter);
//   * snapshots the wall clock (ftime) — the value is captured but unused here;
//   * GetWindowTextA(titleWnd) -> title; GetModuleFileNameA(0) -> exe path,
//     then truncates at the last '\\' so it becomes the exe DIRECTORY;
//   * formats the header into byte_762730:
//       "* ------------------------------------------------------------ *\n"
//       "[%s], Date: %s"   (window-title, ctime(now));
//   * if (flags & 0x01): the FILE-ROTATION loop over the 12-entry, 32-byte-stride
//     file-name table at 0x62D85C (ecx from 0x62D85C; esi=ecx+0x180; add ecx,0x20;
//     stop when ecx==esi -> 12 iterations): for each name, sprintf "%s\\%s"
//     (dir,name) -> path; open append; if open succeeds and size > 0x10000:
//     close, reopen truncate ("wt"), fputs "LogFile has been deleted!\n"; then
//     close;
//   * if (flags & 0x08): AllocConsole(); hConsoleOutput = GetStdHandle(-11);
//   * the STARTUP-RECORD loop: index ecx = 0,4,8,...,0x2C (add 4, stop at 0x30) ->
//     12 records: WriteRecord(header@eax, flags@dl, channelMask@ebx) where the
//     channel mask is dword_62D82C[i] = { 1,2,4,8,0x10,0x20,0x40,0x80,
//     0x100,0x200,0x400,0x800 }.
//
// All Win32/file/CRT leaves are routed through ErrorLogInitHooks so no real OS
// call lives in src/ (rules 4/6). The PURE control flow (rotation decision,
// header format, the 12-name loop, the 12-record loop) is reconstructed 1:1.

// The 12-entry file-name table (gilde.exe aErrorLog @0x62D85C, 32-byte stride).
extern const char* const kErrorLogFileNames[12];

// The 12-entry channel bitmask table (gilde.exe dword_62D82C @0x62D82C).
extern const u32 kErrorLogChannelMasks[12];

// One delivered startup record (what the original passes to WriteRecord).
struct ErrorLogInitRecord {
    std::string text;     // a1 (the formatted header buffer)
    u8          flags;    // dl (byte_62D828)
    u32         channel;  // ebx (dword_62D82C[i])
};

// One log-file rotation step (what the original does per file-name slot).
struct ErrorLogRotationStep {
    std::string path;     // sprintf("%s\\%s", exeDir, name)
    bool        opened;   // append-open succeeded
    bool        rotated;  // size > 0x10000 -> truncate + "LogFile has been deleted!\n"
};

// Inert-default boundary hooks for ErrorLog_Init's OS/file/CRT leaves. The
// defaults perform NO real OS work; tests inject behavior (e.g. file sizes).
struct ErrorLogInitHooks {
    // SetUnhandledExceptionFilter(TopLevelExceptionFilter) — record the install.
    bool installedExceptionFilter = false;

    // GetWindowTextA(titleWnd) — the window title used in the header.
    std::string windowTitle;

    // GetModuleFileNameA(0) then truncate at last '\\' — the exe directory.
    std::string exeDir = "C:\\GILDE";

    // ctime(now) — the date string spliced into the header.
    std::string ctimeDate = "Thu Jan  1 00:00:00 1970\n";

    // File_OpenStream(path, "at")/("wt") size probe. Return the byte size of the
    // append-opened file (Memory_FreeBlock(stream) in the original); <0 means the
    // open failed (the original's `if (v7)` gate). Default: file absent (-1).
    int (*fileSize)(const std::string& path, void* user) = nullptr;

    // AllocConsole()+GetStdHandle(-11) — record the alloc.
    bool allocatedConsole = false;

    void* user = nullptr;

    // --- observed side effects (filled by ErrorLog_Init) -------------------
    std::string                       header;     // byte_762730 after formatting
    bool                              titleBit10 = false; // (flags & 0x10) bit
    std::vector<ErrorLogRotationStep> rotation;   // per file-name slot (if flag 1)
    std::vector<ErrorLogInitRecord>   records;    // the 12 startup records
};

// gilde.exe 0x438a98 — VIBE_ErrorLog_Init. `flags` is the original cl byte. Runs
// the full 1:1 control flow over the inert hooks and returns the formatted
// header (also stored in hooks.header).
std::string ErrorLog_Init(u8 flags, ErrorLogInitHooks& hooks);

// --- Trace (stack-walk) helpers --------------------------------------------

// gilde.exe 0x437e04 — VIBE_Trace_DecodeCallInstruction
// Given the bytes preceding a return address (`code` = pointer to the return
// address, `avail` = bytes available before it, `imageBase` added to E8 rel32
// targets), classify the call that produced it:
//   returns 1 and sets *target for a direct  `E8 rel32` call;
//   returns 2 for an indirect call (FF /2, FF /3 forms) without a known target;
//   returns 0 if the preceding bytes are not a recognizable call.
int DecodeCallInstruction(const u8* code, int avail, u32 imageBase, u32* target);

} // namespace guild::config
