#pragma once
// guild::gui — chat console: per-channel line buffer + input + message assembly.
//
// VIBE_ChatConsole_BuildWindow @0x4bfc48 loads misc\chatconsole, builds up to 8
// recipient-toggle objects (one per active scene player, slot stride 268 in the scene
// table, channel byte tag == 7), restores the per-channel selection from the persisted
// byte_631E80[8] state, runs a modal input loop on an edit field, and on submit
// assembles an outgoing chat line of the form "$%iFF <text>$A" (the "$<colour>FF "
// prefix selects the speaker colour; "$A" terminates) and forwards it through the
// build-op-75 command.  On close it persists the 8 toggle states back to byte_631E80.
//
// This module recovers the LINE-BUFFER + INPUT + message-assembly DATA logic:
//   - the 8-channel recipient model (template initialised to all -1, dword_4AD48C),
//   - the visible line ring/scroll buffer (append + scroll-to-visible window),
//   - the input edit buffer + submit -> assembled "$%iFF …$A" line.
// The form load, widget creation and modal frame loop are routed through a command
// hook so the buffer/input logic is testable in isolation.

#include "guild/common/types.h"
#include <array>
#include <string>
#include <vector>

namespace guild::gui {

using guild::i32;

// Number of recipient channels (template dword_4AD48C is 8 dwords, all 0xFFFFFFFF).
inline constexpr int kChatChannels = 8;
inline constexpr i32 kChatChannelEmpty = -1;   // unused channel slot
inline constexpr int kChatChannelPitchX = 48;  // recipient toggle x = 48*i + 100
inline constexpr int kChatChannelBaseX = 100;

// The chat-line prefix/suffix the submit path builds ("$%iFF " + text + "$A").
// The colour index is `dword_12CE964[134*speaker] - 1342` (a per-player palette base).
inline constexpr int kChatColourBias = 1342;   // subtracted from the player palette id
inline constexpr const char* kChatLineSuffix = "$A";  // aA @0x61e49c

// Assemble the outgoing chat line: "$<colourIndex>FF " + text + "$A".
// `colourIndex` is the already-biased palette index (palette - 1342).
std::string ChatConsole_AssembleLine(int colourIndex, const std::string& text);

// ---------------------------------------------------------------------------
// Visible-line ring buffer.  The console keeps a scrollback of lines and shows the
// last `visibleRows` of them; appending past capacity drops the oldest, and the view
// auto-scrolls so the newest line is always visible.
// ---------------------------------------------------------------------------
class ChatConsole {
public:
    explicit ChatConsole(int visibleRows = 8, int scrollback = 256);

    // Recipient channels (per-player toggles); index 0..kChatChannels-1.
    void SetChannel(int idx, i32 value);
    i32  Channel(int idx) const;
    // The persisted 8-byte selection state (byte_631E80): bit/byte per channel.
    std::array<i32, kChatChannels> ChannelState() const { return channels_; }

    // Append a finished line to the scrollback (auto-scrolls to show it).
    void AppendLine(const std::string& line);

    // The current input edit buffer.
    void SetInput(const std::string& s) { input_ = s; }
    const std::string& Input() const { return input_; }

    // Submit the input as a chat line via the colour prefix; returns the assembled
    // line, appends it to the scrollback, and clears the input.
    std::string Submit(int colourIndex);

    // Scrollback access.
    int LineCount() const { return static_cast<int>(lines_.size()); }
    const std::string& Line(int i) const { return lines_[i]; }

    // The visible window (the last `visibleRows` lines, oldest-first) given the
    // current scroll offset.  Scroll offset 0 == anchored to the bottom (newest).
    std::vector<std::string> VisibleLines() const;

    // Scroll the view up (toward older lines) / down (toward newer); clamped.
    void ScrollUp(int rows = 1);
    void ScrollDown(int rows = 1);
    int  ScrollOffset() const { return scroll_; }
    int  VisibleRows() const { return visibleRows_; }

private:
    int visibleRows_;
    int scrollback_;
    int scroll_ = 0;  // lines above the bottom anchor (0 == newest visible)
    std::array<i32, kChatChannels> channels_{};
    std::string input_;
    std::vector<std::string> lines_;
};

// ---------------------------------------------------------------------------
// VIBE_DebugList_AppendId @0x53629c — append an id to the debug id list
// (dword_122FCC0, capped at 128 entries; returns false when full).
// ---------------------------------------------------------------------------
inline constexpr int kDebugListCapacity = 128;

class DebugIdList {
public:
    bool Append(i32 id);          // gilde.exe 0x53629c
    int  Count() const { return count_; }
    i32  At(int i) const { return ids_[i]; }
    void Clear() { count_ = 0; }
private:
    std::array<i32, kDebugListCapacity> ids_{};
    int count_ = 0;
};

} // namespace guild::gui
