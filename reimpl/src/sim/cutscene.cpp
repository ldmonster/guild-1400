#include "sim/cutscene.h"

namespace guild::sim {

// ===========================================================================
// Slot-table management. Each function mirrors its original linear scan over
// the 96-slot table; the "alive gate" is the participant-count byte at +48
// (byte_11AE6E0), and a slot is free when its id (+0) == -1.
// ===========================================================================

// gilde.exe 0x4ac750 — VIBE_Cutscene_FindSlotById
CutsceneSlot* CutsceneTable::FindById(i32 id) {
    for (auto& s : slots_) {
        if (s.partCount && s.id == id)   // (+48 nonzero) && (+0 == id)
            return &s;
    }
    return nullptr;
}

// gilde.exe 0x4ac708 — VIBE_Cutscene_FindSlotByTypeAndId
CutsceneSlot* CutsceneTable::FindByTypeAndId(u8 type, u32 secondId) {
    for (auto& s : slots_) {
        if (s.partCount && s.type == type &&
            static_cast<u32>(s.secondId) == secondId)
            return &s;
    }
    return nullptr;
}

// gilde.exe 0x4ac7e4 — VIBE_Cutscene_AllocSlot
CutsceneSlot* CutsceneTable::AllocSlot(const CutsceneSlot& tmpl, i32 newId) {
    for (auto& s : slots_) {
        if (s.id == -1) {                    // first free slot
            std::memcpy(&s, &tmpl, sizeof(CutsceneSlot));  // qmemcpy 0x114
            s.id = newId;                    // *v3 = dword_649894[0]
            if (s.priority == 0)             // byte+49 default
                s.priority = 8;
            return &s;
        }
    }
    return nullptr;                          // table full
}

// gilde.exe 0x4ac860 — VIBE_Cutscene_RemoveById
bool CutsceneTable::RemoveById(i32 id) {
    CutsceneSlot* s = FindById(id);
    if (!s) return false;
    std::memset(s, 0, sizeof(CutsceneSlot)); // VIBE_Light_SetGrayColorThunk clear
    s->id = -1;                              // *v2 = -1
    return true;
}

// gilde.exe 0x4ac888 — VIBE_Cutscene_RemoveAll
void CutsceneTable::RemoveAll() {
    for (auto& s : slots_) {
        if (s.id != -1) {
            // The original re-finds by id (FindById) before clearing; that find
            // requires the alive gate (+48) — only participant-bearing slots are
            // actually torn down. Match that gate here.
            if (s.partCount) {
                std::memset(&s, 0, sizeof(CutsceneSlot));
                s.id = -1;
            }
        }
    }
}

// gilde.exe 0x4ac630 — VIBE_Cutscene_FindLowestPriority
CutsceneSlot* CutsceneTable::FindLowestPriority() {
    u32 best = 0xFFFFFFFFu;                  // v1 = -1
    for (auto& s : slots_) {
        if (s.partCount) {                   // alive gate (+48)
            u32 sid = static_cast<u32>(s.id);
            if (sid != 0xFFFFFFFFu &&
                (s.stateFlags & kCsFlagActive) != 0 &&  // (+38 & 1)
                best >= sid)
                best = sid;
        }
    }
    if (best == 0xFFFFFFFFu) return nullptr;
    return FindById(static_cast<i32>(best));
}

// gilde.exe 0x4ac8d0 — VIBE_Cutscene_AddParticipant
int CutsceneTable::AddParticipant(i32 id, i32 person) {
    CutsceneSlot* s = FindById(id);
    if (!s) return 0;
    int count = s->partCount;
    for (int i = 0; i < count; ++i) {
        if (s->partIds[i] == person)         // duplicate
            return 0;
    }
    if (count >= kMaxParticipants)           // (>= 0x10) full
        return 0;
    s->partIds[count] = person;
    s->partCount = static_cast<u8>(count + 1);
    return 1;
}

// gilde.exe 0x4ac928 — VIBE_Cutscene_RemoveParticipant (slot-targeted form)
int CutsceneTable::RemoveParticipant(i32 id, i32 person) {
    CutsceneSlot* s = FindById(id);
    if (!s) return 0;
    for (int i = 0; i < s->partCount; ++i) {
        if (s->partIds[i] == person) {
            s->partIds[i] = -1;              // SlotById[13+i] = -1
            break;
        }
    }
    return 1;
}

} // namespace guild::sim
