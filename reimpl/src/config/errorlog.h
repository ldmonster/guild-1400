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
