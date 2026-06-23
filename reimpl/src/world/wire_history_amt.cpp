// See wire_history_amt.h. Binds the AmtEconomy2 and MissionReqEvent world bridges
// to their real reconstructed cross-cluster leaves. Glue only — no module logic.
//
// All command emits stage onto the SAME shared real CommandQueue that real_hooks
// owns (sim::RealCommandQueue()); the RNG / coord-truncation / pure-table leaves
// are headless-safe reconstructions.
#include "world/wire_history_amt.h"

#include "world/amt_economy2.h"                    // AmtEconomy2Hooks / Set/Get
#include "world/mission_requirement_event_recon.h" // MissionReqEventHooks / Get

#include "world/economy_quality.h"  // EconomyComputeWeightedLawScore / EconomyLoadDemandSnapshot
#include "world/trade_player.h"     // MoneyConvertToDisplayCoord
#include "sim/building_type.h"      // BuildingType_MapToActionCode
#include "sim/command_codec.h"      // QueueRequest16 / QueueRequestCoord27
#include "sim/command_builders2.h"  // QueueRequestArgs26
#include "sim/real_hooks.h"         // RealCommandQueue()
#include "util/math_rng_float.h"    // util::RandomFloatScaled
#include "util/coord.h"             // util::ConvertX

#include "guild/common/types.h"

#include <cstdint>
#include <cstring>

namespace guild::world {

namespace {

sim::CommandQueue& Q() { return *sim::RealCommandQueue(); }

// Reinterpret a float to its raw 32-bit pattern (the original QueueRequestArgs26
// call site stages the float's bit image, e.g. 0x3D4CCCCD for +0.05f).
i32 FloatBits(float f) {
    i32 bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    return bits;
}

// =========================================================================
// AmtEconomy2Hooks adapters.
// =========================================================================

// VIBE_Math_RandomFloatScaled (0x58b910) — the rivalry roll. double -> float.
float WhaRandFloatScaled() {
    return static_cast<float>(util::RandomFloatScaled());
}

// VIBE_Coord_ConvertX (0x5c6b08) — round-toward-zero of the FPU st0 the caller
// pushed; surfaced as an explicit double -> truncated-i32.
i32 WhaTruncate(double v) {
    return static_cast<i32>(util::ConvertX(v));
}

// VIBE_Command_QueueRequest16 (0x494630) — the rivalry money payout. The native
// flag byte a4 is byte_6477A1 (the display-currency index); the recon passes it
// through `currency`.
void WhaQueueRequest16(i32 payer, i32 recipient, i32 amount, int currency) {
    sim::QueueRequest16(Q(), payer, recipient, amount, static_cast<u8>(currency));
}

// VIBE_Command_QueueRequestArgs26 (0x494848) — the rivalry reputation delta
// (field 460). The original stages the float's raw bit pattern.
void WhaQueueArgs26(i32 personId, int field, float value) {
    sim::QueueRequestArgs26(Q(), personId, field, FloatBits(value));
}

// VIBE_Command_QueueRequestCoord27 (0x494878) — highlight / marker / payout coord.
// The native builder sources its converted world-coord pair from globals
// (flt_62EB90 / dword_62EB94), not from caller args, so we pass the inert 0,0 pair
// exactly as wire_charaction does for this opcode.
void WhaQueueCoord27(i32 holderId, i32 otherId, int arg) {
    sim::QueueRequestCoord27(Q(), holderId, otherId, arg, 0, 0);
}

// =========================================================================
// MissionReqEventHooks adapters.
// =========================================================================

// VIBE_Money_ConvertToDisplayCoord (0x58f14c) — divide an amount by the active
// currency's city rate (round-bias + trunc).
int WhaMoneyConvertToDisplayCoord(int amount, u8 currencyIdx) {
    return static_cast<int>(MoneyConvertToDisplayCoord(amount, currencyIdx));
}

// VIBE_Economy_ComputeWeightedLawScore (0x57a580) — weighted gesetz sum. The
// native stores a1/a2 but the accumulation loop never reads them; the faithful
// recon takes no args, so the adapter drops them.
double WhaEconomyComputeWeightedLawScore(int /*a1*/, int /*a2*/) {
    return EconomyComputeWeightedLawScore();
}

// VIBE_Economy_LoadDemandSnapshot (0x57a5dc) — fills out[10], returns out[9]
// (after overwriting out[6] with flt_641DA8). Exact signature match.
double WhaEconomyLoadDemandSnapshot(float* out) {
    return EconomyLoadDemandSnapshot(out);
}

// VIBE_BuildingType_MapToActionCode (0x589a7c) — building type code -> action code
// (0 == none). The native GroupFromCode takes the code byte in al; the opaque
// buildingTypeRec value passed by CheckBuildingEquip IS that code byte, so we read
// its low byte and feed the byte-faithful recon.
u8 WhaBuildingTypeMapToActionCode(const void* buildingTypeRec) {
    auto code = static_cast<u8>(reinterpret_cast<std::uintptr_t>(buildingTypeRec) & 0xFF);
    return sim::BuildingType_MapToActionCode(code);
}

} // namespace

void InstallRealHistoryAmtWiring() {
    Q();  // force the shared real command queue to exist

    // --- AmtEconomy2Hooks (amt_economy2.h) -----------------------------------
    // SEED-FROM-DEFAULTS: read the live table back so unbound fields keep their
    // inert defaults, then override only the wireable ones.
    AmtEconomy2Hooks amt = AmtEconomy2GetHooks();
    amt.randFloatScaled = &WhaRandFloatScaled;
    amt.truncate        = &WhaTruncate;
    amt.queueRequest16  = &WhaQueueRequest16;
    amt.queueArgs26     = &WhaQueueArgs26;
    amt.queueCoord27    = &WhaQueueCoord27;
    // officeAddTableEntry (OfficeAddTableEntry 0x47e750 needs an OfficePersonRec*,
    // not the i32 id the recon hands it -> live person store) / personFind (live
    // person store, raw present+8/dirty+358 bytes) / queueDeltaFlag (BeginDeltaPacket
    // + AppendDeltaField + QueueState22 network commit composite) / sendEntityMessage
    // (He text/UI): coupled subsystem leaves with no signature-compatible recon ->
    // inert (documented in .h).
    AmtEconomy2SetHooks(amt);

    // --- MissionReqEventHooks (mission_requirement_event_recon.h) -------------
    // The bridge exposes a MUTABLE reference (no separate setter) and is all-null
    // inert by default; the dispatchers null-check each field. Override only the
    // wireable fields in place (seed-from-defaults == leave the rest null).
    MissionReqEventHooks& req = MissionReqEventGetHooks();
    req.moneyConvertToDisplayCoord     = &WhaMoneyConvertToDisplayCoord;
    req.economyComputeWeightedLawScore = &WhaEconomyComputeWeightedLawScore;
    req.economyLoadDemandSnapshot      = &WhaEconomyLoadDemandSnapshot;
    req.buildingTypeMapToActionCode    = &WhaBuildingTypeMapToActionCode;
    // personGetCurrencyAmount (PersonGetCurrencyAmount needs a ContainerView built
    // from person+376, not the opaque void* the hook hands it) / personGetFamilyRecord
    // (live family record layout) / personQueryOwnedObjects / personQueryMemberState /
    // personQueryAll / personIterNext (live person store cursors) / gameObjectQueryFind
    // (live object store) / objectStateByIndex (byte_12CE912 process-global table) /
    // buildingEquipWord / buildingObjId (dword_13CE294 building-equip table): coupled
    // subsystem leaves with no signature-compatible recon -> inert (documented in .h).
}

} // namespace guild::world
