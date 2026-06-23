#include "config/errorlog.h"

#include <cstdio>
#include <cstring>

namespace guild::config {

// gilde.exe 0x437ab0 — VIBE_ErrorLog_WriteRecord
// The original writes the body then a separate "\n" to each selected sink; we
// hand each sink the body with the newline appended (same bytes on the wire).
void WriteRecord(ILogSink& sink, const std::string& text, u8 flags) {
    const std::string line = text + "\n";
    if (flags & kLogFile)
        sink.file(line);
    if (flags & kLogDebug)
        sink.debug(line);
    if (flags & kLogConsole)
        sink.console(line);
}

// gilde.exe 0x438da8 — VIBE_ErrorLog_ReportMessage
//   if any of (file|debug|console) set:  sprintf("MESSAGE: %s\n", msg);
//                                        WriteRecord(buf, flags, 1);
//   if msgbox bit set:                   MessageBox(msg).
std::string FormatMessage(ILogSink& sink, u8 flags, const std::string& msg) {
    std::string body = "MESSAGE: " + msg + "\n";
    // The original tests (flags & 0xD) i.e. file|debug|console.
    if (flags & (kLogFile | kLogDebug | kLogConsole)) {
        WriteRecord(sink, body, flags);
    }
    if (flags & kLogMsgBox)
        sink.msgBox(msg);
    return body;
}

// gilde.exe 0x438e3c — VIBE_ErrorLog_ReportModuleLine
std::string FormatModuleLine(const std::string& module, unsigned long line,
                             const std::string& msg, bool withTime, unsigned long timeMs) {
    char buf[512];
    if (withTime) {
        std::snprintf(buf, sizeof(buf), "Time:%8lu\tModule '%s':\tLine:%lu:\t%s",
                      timeMs, module.c_str(), line, msg.c_str());
    } else {
        // Verbatim from the original: the no-time format string begins "tModule"
        // (the leading 't' is a stray tab-escape lost in the original literal).
        std::snprintf(buf, sizeof(buf), "tModule '%s':\tLine:%lu:\t%s",
                      module.c_str(), line, msg.c_str());
    }
    return std::string(buf);
}

// --- ErrorLog_Init (subsystem startup) -------------------------------------

// gilde.exe aErrorLog @0x62D85C — 12 names, 32-byte stride (recovered with
// get_bytes). The original loops ecx from 0x62D85C, +0x20 each step, until
// ecx == 0x62D85C+0x180 (12 iterations).
const char* const kErrorLogFileNames[12] = {
    "_error.log",   "_ai.log",       "_sim.log",      "_dbg.log",
    "_script.log",  "_cutscene.log", "_meister.log",  "_d3.log",
    "_stats.log",   "_msx.log",      "___tracker.log", "_meister2.log",
};

// gilde.exe dword_62D82C @0x62D82C — 12 dwords (recovered with get_bytes).
const u32 kErrorLogChannelMasks[12] = {
    0x001, 0x002, 0x004, 0x008, 0x010, 0x020,
    0x040, 0x080, 0x100, 0x200, 0x400, 0x800,
};

// gilde.exe 0x438a98 — VIBE_ErrorLog_Init.
std::string ErrorLog_Init(u8 flags, ErrorLogInitHooks& hooks) {
    // mov byte_62D828, cl / setnz byte_62D829 (flags & 0x10).
    hooks.titleBit10 = (flags & 0x10) != 0;

    // SetUnhandledExceptionFilter(TopLevelExceptionFilter).
    hooks.installedExceptionFilter = true;

    // VIBE_Crt_Ftime(0) — snapshot captured (var_8); unused below, kept for
    // fidelity of side effects.

    // Header: sprintf(byte_762730, "* ----...---- *\n[%s], Date: %s",
    //                 windowTitle, ctime(now)).
    std::string header =
        "* ------------------------------------------------------------ *\n[" +
        hooks.windowTitle + "], Date: " + hooks.ctimeDate;
    hooks.header = header;

    // if (flags & 0x01): file-rotation loop over the 12-name table (stride 0x20,
    // 12 iterations). path = sprintf("%s\\%s", exeDir, name).
    if (flags & kLogFile) {
        for (int i = 0; i < 12; ++i) {
            ErrorLogRotationStep step;
            step.path = hooks.exeDir + "\\" + kErrorLogFileNames[i];
            step.opened = false;
            step.rotated = false;
            // v7 = File_OpenStream(path, "at"); if (v7) { ... }
            int size = hooks.fileSize ? hooks.fileSize(step.path, hooks.user) : -1;
            if (size >= 0) {                  // open succeeded (v7 != 0)
                step.opened = true;
                // if (Memory_FreeBlock(stream) > 0x10000): close, reopen "wt",
                // fputs("LogFile has been deleted!\n"); then close.
                if (size > 0x10000) {
                    step.rotated = true;
                }
                // Vfs_CloseAndFreeEntry(stream) — close in both branches.
            }
            hooks.rotation.push_back(step);
        }
    }

    // if (flags & 0x08): AllocConsole(); hConsoleOutput = GetStdHandle(-11).
    if (flags & kLogConsole) {
        hooks.allocatedConsole = true;
    }

    // Startup-record loop: ecx = 0,4,...,0x2C (12 records). The original passes
    // (header@eax, flags@dl, dword_62D82C[i]@ebx) to WriteRecord.
    for (int i = 0; i < 12; ++i) {                 // ecx: 0,4,...,0x2C; cmp 0x30
        ErrorLogInitRecord rec;
        rec.text = header;
        rec.flags = flags;
        rec.channel = kErrorLogChannelMasks[i];
        hooks.records.push_back(rec);
    }

    return header;
}

// gilde.exe 0x437e04 — VIBE_Trace_DecodeCallInstruction
// a1 = return address (here `code` points one past the call instruction),
// a2 = avail bytes before it, a4 = image base added to E8 rel32 targets.
int DecodeCallInstruction(const u8* code, int avail, u32 imageBase, u32* target) {
    *target = 0;
    // Direct near call: E8 rel32 (5 bytes).
    if (avail >= 5 && code[-5] == 0xE8) {
        u32 rel;
        std::memcpy(&rel, code - 4, 4);
        *target = rel + imageBase;
        return 1;
    }
    // Indirect call forms (FF /2, FF /3 with various ModRM/SIB encodings):
    //   FF 14 ..            call [reg*scale]            (SIB, 3 bytes back)
    //   FF 15 disp32        call [disp32]               (6 bytes back)
    //   FF /2 reg (mod=00)  call [reg]                  (2 bytes back, modrm<0x40)
    //   FF D0..D7           call reg                    (2 bytes back, modrm&0xF8==0xD0)
    //   FF 50+reg disp8     call [reg+disp8]            (3 bytes back, modrm&0xF8==0x50)
    //   FF 90+reg disp32    call [reg+disp32]           (7 bytes back, modrm&0xF8==0x90)
    if ((avail >= 3 && code[-3] == 0xFF && code[-2] == 0x14) ||
        (avail >= 6 && code[-6] == 0xFF && code[-5] == 0x15) ||
        (avail >= 2 && code[-2] == 0xFF && code[-1] < 0x40) ||
        (avail >= 2 && code[-2] == 0xFF && (code[-1] & 0xF8) == 0xD0) ||
        (avail >= 3 && code[-3] == 0xFF && (code[-2] & 0xF8) == 0x50) ||
        (avail >= 7 && code[-7] == 0xFF && (code[-6] & 0xF8) == 0x90)) {
        return 2;
    }
    return 0;
}

} // namespace guild::config
