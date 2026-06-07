#include "ai/intrigue.h"

#include <cstring>

// Deferred (string/format/query coupled — only the rule cores ported here):
//   VIBE_AiScore_ComputeRelationDistance 0x479a4c — adds a He_SumPlayerHandler
//     distance term + a four-way sign split; needs the He cluster.
//   VIBE_AiScore_ScaleByActionType 0x479d84 — BuildingValue_ComputeRoomWorth +
//     Coord_ConvertX coupling.
//   VIBE_AiIntrigue_FormatActionLabel 0x47c900 / _FormatSlanderLabel 0x47cfa8 —
//     Crt_Sprintf + He handler scan + DebugCmd dispatch (string formatting).
//   VIBE_AiPlayer_ExecPamphlet/ExecThreaten/ExecSlander 0x47d4cc.. — command
//     emitters over the resolved target (Command_QueueRequest* + history).

namespace guild::ai {

namespace {
PamphletCmdFn g_pamphletCmd = nullptr;

// Read the dword at `p+off` and return its high byte (>>24), matching the
// `*(int *)(x) >> 24` idiom the scorers use for packed class ids.
i32 dword_at(const u8* p, int off) {
    return static_cast<i32>(p[off] | (p[off + 1] << 8) | (p[off + 2] << 16)
         | (static_cast<u32>(p[off + 3]) << 24));
}
float float_at(const u8* p, int off) {
    float f;
    std::memcpy(&f, p + off, sizeof(f));
    return f;
}
} // namespace

// gilde.exe 0x4796b0 — AiScore_ComputeRelationWeighted.
RelationScore ComputeRelationWeighted(const u8* entry, const PersonScoreScratch& p) {
    RelationScore out;
    int personClass = p.method_class();

    float divNear, divFar; // v23 / v24
    bool eligible;
    if ((dword_at(entry, 142) >> 16) & p.visited_bits()) {
        eligible = true;
        divNear = 40.0f;
        divFar  = 50.0f;
    } else {
        divNear = 50.0f;
        divFar  = 100.0f;
        eligible = false;
    }

    // Two pre-scan loops set `eligible` if any matching positive sub-entry exists.
    if (!eligible) {
        for (int k = 0; k < 4 && !eligible; ++k) {
            int off = 8 * k;
            if ((dword_at(entry, 45 + off) >> 24) == personClass
                && float_at(entry, 52 + off) > 0.0f)
                eligible = true;
        }
    }
    if (!eligible) {
        for (int k = 0; k < 4 && !eligible; ++k) {
            int off = 8 * k;
            if ((dword_at(entry, 109 + off) >> 24) == personClass
                && float_at(entry, 116 + off) > 0.0f)
                eligible = true;
        }
    }

    // Main accumulation: 4 sub-entries, stride 8, walking from `entry`.
    for (int k = 0; k < 4; ++k) {
        int off = 8 * k;
        if (static_cast<i8>(entry[48 + off]) >= 0) {
            int cls = dword_at(entry, 45 + off) >> 24;
            float w = float_at(entry, 52 + off);
            float rel = p.relation_weight(cls);
            float term = (cls == personClass) ? (w * rel / divNear) : (w * rel / divFar);
            out.a += term;
        }
        if (static_cast<i8>(entry[112 + off]) >= 0) {
            int cls = dword_at(entry, 109 + off) >> 24;
            float w = float_at(entry, 116 + off);
            float rel = p.relation_weight(cls);
            float term = (cls == personClass) ? (w * rel / divNear) : (w * rel / divFar);
            out.b += term;
        }
    }
    out.eligible = eligible;
    return out;
}

// gilde.exe 0x479898 — AiScore_ComputeRelationOwn.
RelationScore ComputeRelationOwn(const u8* entry, const PersonScoreScratch& p) {
    RelationScore out;
    int personClass = p.method_class();

    float div; // v19
    bool eligible;
    if ((dword_at(entry, 142) >> 16) & p.visited_bits()) {
        eligible = true;
        div = 40.0f;
    } else {
        div = 50.0f;
        eligible = false;
    }
    if (!eligible) {
        for (int k = 0; k < 4 && !eligible; ++k) {
            int off = 8 * k;
            if ((dword_at(entry, 45 + off) >> 24) == personClass
                && float_at(entry, 52 + off) > 0.0f)
                eligible = true;
        }
    }
    if (!eligible) {
        for (int k = 0; k < 4 && !eligible; ++k) {
            int off = 8 * k;
            if ((dword_at(entry, 109 + off) >> 24) == personClass
                && float_at(entry, 116 + off) > 0.0f)
                eligible = true;
        }
    }

    for (int k = 0; k < 4; ++k) {
        int off = 8 * k;
        if (static_cast<i8>(entry[48 + off]) >= 0) {
            int cls = dword_at(entry, 45 + off) >> 24;
            float term = 0.0f;
            if (cls == personClass)
                term = float_at(entry, 52 + off) * p.relation_weight(cls) / div;
            out.a += term;
        }
        if (static_cast<i8>(entry[112 + off]) >= 0) {
            int cls = dword_at(entry, 109 + off) >> 24;
            float term = 0.0f;
            if (cls == personClass)
                term = float_at(entry, 116 + off) * p.relation_weight(cls) / div;
            out.b += term;
        }
    }
    out.eligible = eligible;
    return out;
}

// gilde.exe 0x4715d8 — AiIntrigue_EvalActionLabel.
u8 EvalActionLabel(bool formatted, u8 rejectCode) {
    return formatted ? 39 : rejectCode;
}

// gilde.exe 0x471fa0 — AiIntrigue_EvalSlanderLabel. Mirror of EvalActionLabel:
// returns the slander label code 45 when the slander formatter accepted, else the
// reject code from VIBE_Interaction_EvalRejectStub (passed in as `rejectCode`).
u8 EvalSlanderLabel(bool formatted, u8 rejectCode) {
    return formatted ? 45 : rejectCode;
}

void SetPamphletCmdHook(PamphletCmdFn fn) { g_pamphletCmd = fn; }

// gilde.exe 0x471ae0 — AiPlayer_EvalPamphlet (return-code + command-emit core).
u8 EvalPamphlet(bool execOk, i32 targetId) {
    if (!execOk)
        return 0;
    if (g_pamphletCmd)
        g_pamphletCmd(targetId);
    return 41;
}

} // namespace guild::ai
