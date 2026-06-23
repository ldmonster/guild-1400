#include "gui/chatconsole.h"

#include <algorithm>
#include <cstdio>

namespace guild::gui {

std::string ChatConsole_AssembleLine(int colourIndex, const std::string& text) {
    // VIBE_Crt_Sprintf_0(buf, "$%iFF ", colourIndex) then append text then "$A".
    char prefix[32];
    std::snprintf(prefix, sizeof(prefix), "$%iFF ", colourIndex);
    std::string out = prefix;
    out += text;
    out += kChatLineSuffix;  // "$A"
    return out;
}

ChatConsole::ChatConsole(int visibleRows, int scrollback)
    : visibleRows_(visibleRows), scrollback_(scrollback) {
    channels_.fill(kChatChannelEmpty);  // template dword_4AD48C: all -1
}

void ChatConsole::SetChannel(int idx, i32 value) {
    if (idx >= 0 && idx < kChatChannels)
        channels_[idx] = value;
}

i32 ChatConsole::Channel(int idx) const {
    return (idx >= 0 && idx < kChatChannels) ? channels_[idx] : kChatChannelEmpty;
}

void ChatConsole::AppendLine(const std::string& line) {
    lines_.push_back(line);
    // Cap the scrollback, dropping oldest.
    if (static_cast<int>(lines_.size()) > scrollback_)
        lines_.erase(lines_.begin(),
                     lines_.begin() + (lines_.size() - scrollback_));
    // Auto-scroll to bottom so the newest line is visible.
    scroll_ = 0;
}

std::string ChatConsole::Submit(int colourIndex) {
    std::string line = ChatConsole_AssembleLine(colourIndex, input_);
    AppendLine(line);
    input_.clear();
    return line;
}

std::vector<std::string> ChatConsole::VisibleLines() const {
    std::vector<std::string> out;
    const int n = static_cast<int>(lines_.size());
    if (n == 0)
        return out;
    // Bottom anchor: the last visible line is at index (n-1 - scroll_).
    int last = n - 1 - scroll_;
    if (last < 0)
        last = 0;
    int first = last - (visibleRows_ - 1);
    if (first < 0)
        first = 0;
    for (int i = first; i <= last; ++i)
        out.push_back(lines_[i]);
    return out;
}

void ChatConsole::ScrollUp(int rows) {
    const int n = static_cast<int>(lines_.size());
    int maxScroll = std::max(0, n - visibleRows_);
    scroll_ = std::min(scroll_ + rows, maxScroll);
}

void ChatConsole::ScrollDown(int rows) {
    scroll_ = std::max(0, scroll_ - rows);
}

// byte_631E80[8] — persisted recipient-toggle state (all 1 at boot per get_bytes).
std::array<i32, kChatPersistedChannels> g_chatRecipientState{ {1,1,1,1,1,1,1,1} };

// gilde.exe 0x4bfc48 — VIBE_ChatConsole_BuildWindow.
ChatBuildResult ChatConsole_BuildWindow(ChatConsoleHooks& hooks) {
    ChatBuildResult r;
    // v31 = 0; dword_631E88 = 0; qmemcpy(v29, dword_4AD48C) -> all -1.
    r.channels.fill(kChatChannelEmpty);
    r.toggleCount = 0;

    // The recipient build loop: walk the scene-player table (byte_12CE912, stride 268,
    // 768 entries == v2 0..205824).  Each entry whose channel tag == 7 becomes a toggle
    // (up to 8 = the v29[8] capacity), seeded from byte_631E80[v31].
    // (The form/window creation + VIBE_Object_AddToWindow are the GUI boundary; here we
    //  reproduce the STATE: which channels get populated, in order, and their restored
    //  toggle values.)
    int v31 = 0;                       // toggles built so far
    int v3 = 0;                        // byte offset into v29 (v3 += 4 per toggle)
    for (const ChatSceneSlot& slot : hooks.sceneSlots) {
        if (v31 >= kChatChannels)
            break;                     // v29[8] capacity (the 8-channel array is full)
        if (slot.channelTag == 7) {
            // *(_DWORD*)((char*)v29 + v3) = AddToWindow(...);  here we store the player id.
            r.channels[v3 / 4] = slot.playerId;
            // SetValueOrText(toggle, byte_631E80[v31]) — restore persisted toggle state.
            // (The widget restore is the boundary; the state read is reproduced.)
            (void)g_chatRecipientState[v31 < kChatPersistedChannels ? v31 : 0];
            v3 += 4;
            ++v31;
        }
    }
    r.toggleCount = v31;

    // --- the modal frame loop (RunFrameLoop 0x4c09a0) is the SDL boundary ---
    // On a submit tick (dword_75BF38 != -1 || key 28) the original gathers the 8 channel
    // data values (-1 if the channel object is empty / has no data ptr), assembles the
    // line, sets the edit field to it and forwards via RequestBuildOp75.
    if (hooks.didSubmit) {
        // Gather: for i in 0..7, if channel populated use its toggle value, else -1.
        std::array<i32, kChatChannels> recipients;
        for (int i = 0; i < kChatChannels; ++i) {
            recipients[i] = (r.channels[i] != kChatChannelEmpty)
                                ? hooks.toggleValues[i < kChatPersistedChannels ? i : 0]
                                : kChatChannelEmpty;
        }
        // Sprintf("$%iFF ", dword_12CE964[134*word_63CC5C] - 1342) + edit-text + "$A".
        std::string line = ChatConsole_AssembleLine(hooks.speakerColour, hooks.submitText);
        // VIBE_Command_RequestBuildOp75(&v25, ...) — forward (boundary sink).
        hooks.lastForwardedLine = line;
        r.forwardedLine = line;
        r.submitted = true;
        (void)recipients;  // the 25-dword command blob is assembled by the boundary sink
    }

    // --- on close: persist each built toggle's current value back to byte_631E80[i] ---
    // for (v8=v9=0; v8<8; ++v8,++v9) if (v29[v9] != -1) byte_631E80[v8] = GetDataPtr(...).
    for (int i = 0; i < kChatChannels && i < kChatPersistedChannels; ++i) {
        if (r.channels[i] != kChatChannelEmpty)
            g_chatRecipientState[i] = hooks.toggleValues[i];
    }
    // VIBE_Form_Destroy(form); byte_67225C = 0; (boundary).
    return r;
}

// gilde.exe 0x53629c — VIBE_DebugList_AppendId.
bool DebugIdList::Append(i32 id) {
    if (count_ >= kDebugListCapacity)  // dword_63CD40 >= 128
        return false;
    ids_[count_++] = id;
    return true;
}

} // namespace guild::gui
