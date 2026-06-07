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

// gilde.exe 0x53629c — VIBE_DebugList_AppendId.
bool DebugIdList::Append(i32 id) {
    if (count_ >= kDebugListCapacity)  // dword_63CD40 >= 128
        return false;
    ids_[count_++] = id;
    return true;
}

} // namespace guild::gui
