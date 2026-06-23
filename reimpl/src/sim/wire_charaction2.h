#pragma once
// wire_charaction2 — wires the four CharAction step/state-machine LEAF bridges
// (CharActionStep5Hooks .. CharActionStep8Hooks; sim/charaction_steps5.h ..
// steps8.h) into their real reconstructed siblings. Before this, NOTHING in the
// live tree installed any of the four tables (only the unit/e2e/integration tests
// did), so every CharAction step coroutine ran against the fully INERT default
// table at runtime — including the RNG draw, which the inert default pins to 0.
//
// What is bound to a REAL reconstruction:
//   * the `randomModulo` field, present in ALL FOUR bridges, is bound to
//     util::RandomModulo (VIBE_Math_RandomModulo @0x58b89c — the exact function the
//     bridge comments name). This de-inerts every RNG draw the four step batches
//     make: the spy scan cursor / coprime stride (RunSpionage), the highwayman
//     ambush + arrival roll (RunTransport), the bribe/escape check (RunArrestPerson),
//     the wage/severance amounts and reschedule jitter (RunMeister*), the 50/50
//     pose-free roll (RestorePosFinishAlt), the herd wake jitter (RunHerdAnimals),
//     etc. — all now draw from the real LCG instead of the constant 0.
//
// What stays INERT (and why): every other field on the four tables is a
// CROSS-CLUSTER ENGINE-LEAF side effect whose hook contract is over the engine
// handler-entry record (HeRecord*) — the person/object/building resolves
// (findPersonById/objectQueryFind/buildingFindById/personQueryBegin/...), the
// cmd-queue emits, the formatted-message renders + quickjump/entity sends, the
// scene-graph walk, the handler-pool scan and the city recipient/category tables.
// The reconstructed native record modules (entity.h: PersonFindRecordById /
// BuildingFindById / GameObjectResolveEntityById / PersonQueryBegin) return the
// NATIVE record types (Person* / ObjectRec* / SceneNode*), which are a DIFFERENT
// record model from the engine handler-entry HeRecord* the hooks hand back; there
// is no byte-faithful adapter between them, so binding one would be a cheap
// analogue (rule 8). They therefore keep their faithful inert defaults (every
// resolve reports absent, every emit/render is a no-op, every query returns 0) and
// are reported per-bridge as deferred. NpcClock()/GameTime* are already reused by
// the step bodies and need no wiring.
//
// Idempotent. Builds each real table on top of the library's COMPLETE inert
// default (the step .cpp bodies call hook members WITHOUT null-checks, so every
// field must stay valid — we copy the inert table and override only randomModulo).
namespace guild::sim {

// Install the real CharAction step-leaf wiring into the four global hook tables.
void InstallRealCharAction2Wiring();

} // namespace guild::sim
