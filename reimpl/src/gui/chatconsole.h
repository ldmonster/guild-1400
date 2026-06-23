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
// VIBE_ChatConsole_BuildWindow @0x4bfc48 — the actual window driver (state logic).
//
// The wave-20 helpers above (ChatConsole / ChatConsole_AssembleLine) model the
// channel template + line-assembly SUB-logic.  This is the real 0x4bfc48 routine,
// reconstructed 1:1 as a state machine:
//   1. v31 = 0; dword_631E88 = 0;
//      qmemcpy(channels[8], dword_4AD48C)  -> all -1.
//   2. Walk the scene-player table (byte_12CE912, stride 268, 768 entries): for each
//      entry whose channel-tag byte == 7, add a recipient toggle (up to 8), store its
//      object id into channels[next], and restore its toggle value from the persisted
//      byte_631E80[v31] (1 = on).  v31 counts the toggles built (0..7).
//   3. Build the input edit field + submit button (callback 96), run the modal frame
//      loop (RunFrameLoop 0x4c09a0).
//   4. On submit (key 28 / submit button): gather the 8 channels' data (-1 if empty)
//      into the recipient vector, assemble "$<colour>FF " + edit-text + "$A" where
//      colour = ColourForActiveSpeaker() (dword_12CE964[134*word_63CC5C] - 1342), set
//      the edit field to the assembled text and forward it via RequestBuildOp75.
//   5. On close: persist each built toggle's current value back to byte_631E80[i],
//      then destroy the form.
//
// The form-load / widget-alloc / scene-table walk / modal loop are the SDL/Vulkan
// boundary (rules 3-4) and are routed through ChatConsoleHooks.  The persisted
// recipient-toggle state (byte_631E80[8], all 1 at init) is owned here.
// ---------------------------------------------------------------------------

// Persisted recipient-toggle state byte_631E80[8] (all 1 at boot = every channel on).
inline constexpr int kChatPersistedChannels = 8;
extern std::array<i32, kChatPersistedChannels> g_chatRecipientState;  // byte_631E80

// One scene-player slot the build loop inspects (byte_12CE912 entry, stride 268).
struct ChatSceneSlot {
    int channelTag = 0;  // byte_12CE912[entry]; == 7 selects this player as a recipient
    i32 playerId   = 0;  // word_12CE910[entry]: the recipient's player id (toggle payload)
};

// The boundary hooks (form / widget / scene-table / modal loop / colour / command).
struct ChatConsoleHooks {
    // The scene-player table the build loop walks (byte_12CE912, 768 entries).
    std::vector<ChatSceneSlot> sceneSlots;
    // Per-toggle live value, queried on close (VIBE_Object_GetDataPtr -> byte).
    // Indexed by the order toggles were built (0..count-1).
    std::array<i32, kChatPersistedChannels> toggleValues{ {1,1,1,1,1,1,1,1} };
    // The active speaker's biased colour index (dword_12CE964[134*word_63CC5C]-1342).
    int speakerColour = 0;
    // The submitted edit-field text (drained by the submit path).
    std::string submitText;
    // Whether the modal loop ended in a submit (key 28 / submit button) vs cancel.
    bool didSubmit = false;
    // Sink for the assembled line (VIBE_Command_RequestBuildOp75); records last line.
    std::string lastForwardedLine;
};

// The 8-entry recipient channel result of one build session (channels[i] = player id
// or -1).  Returned for inspection/testing; the original keeps it on the stack (v29).
struct ChatBuildResult {
    std::array<i32, kChatChannels> channels{};   // v29[8]
    int toggleCount = 0;                          // v31 (recipient toggles built)
    bool submitted = false;                       // whether a line was forwarded
    std::string forwardedLine;                    // the assembled "$cFF …$A" line
};

// gilde.exe 0x4bfc48 — VIBE_ChatConsole_BuildWindow.  Reconstructed state machine;
// `hooks` provides the scene table + boundary I/O.  Mutates g_chatRecipientState.
ChatBuildResult ChatConsole_BuildWindow(ChatConsoleHooks& hooks);

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
