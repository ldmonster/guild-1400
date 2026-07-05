#pragma once
// gilde.exe — guild::world  (MODULE: world / scene object & building WRITE serializers)
//
// 1:1 reconstruction of the WorldIo write-side object/building serializers that
// stream a live scene-graph node (and its building sub-state) to a savegame /
// scene chunk through the binary-IO layer (VIBE_Bio_*), itself a raw wrapper over
// the VFS stream verbs. Field order, offsets, bit extractions and the object-type
// switch are reproduced verbatim from the Hex-Rays decompilation, cross-checked
// against the disassembly.
//
// Reconstructed here:
//   VIBE_WorldIo_WriteObject         @0x5e5ab4  — serialize one scene object node
//                                                  (recursive over child/sibling)
//   VIBE_WorldIo_WriteBuildingData   @0x5e5f74  — serialize a building's sub-state
//   VIBE_WorldIo_WriteObjectCallback @0x5e61ec  — write-one-object helper that
//                                                  temporarily severs the sibling
//                                                  link (+496) so only this node
//                                                  is emitted.
//
// Boundary handling (rules 1/4/8): file I/O is the rule-4 boundary. The original
// streams through VIBE_Vfs_WriteStream via the VIBE_Bio_* primitives; here the pure
// serialization LOGIC is reconstructed against a byte-buffer cursor (WorldIoSink).
// Records are addressed by their raw byte base exactly as the binary does; pointer
// slots stored inside a record (node+492/+488/+468/+508/+496, building room[0]) are
// followed as host pointers (the records are reconstructed by value elsewhere). No
// engine behaviour is approximated — only the serialization order is reconstructed.
#include "guild/common/types.h"

namespace guild::world {

// ---------------------------------------------------------------------------
// Byte-buffer sink (the rule-4 file-I/O boundary, modelled as a cursor). The same
// bytes flow through VIBE_Vfs_WriteStream in a real build.
// ---------------------------------------------------------------------------
struct WorldIoSink {
    guild::u8* buf;  // destination buffer
    guild::u32 cap;  // capacity (a short write -> ok=false, mirroring VfsWriteStream)
    guild::u32 pos;  // bytes written so far
    bool       ok;   // false once a write overran the buffer
};

// Open a sink over `buffer` (capacity `capacity`), positioned at 0.
WorldIoSink WorldIoSinkOpen(guild::u8* buffer, guild::u32 capacity);

// Intra-record pointer-slot resolver (32-bit-faithful link handling). Every pointer
// slot the serializer follows (child/sibling/sub/part/event-list/building room ptrs)
// is a 4-byte LINK ID in this reconstruction; id 0 == null. Install a resolver to map
// a non-zero id to the live record pointer (default: every non-zero id -> null, i.e.
// a single node with no graph serializes only its own fields). The serialized BYTES
// are byte-exact; only the in-memory slot encoding (id vs raw 32-bit pointer) differs
// from the original (types.h: serialized paths store explicit 32-bit ids).
using LinkResolver = guild::u8* (*)(guild::u32 id);
void WorldIoSetLinkResolver(LinkResolver resolver);

// VIBE_Event_WriteEventNames @0x5f4b60 is owned by the event module (out of this
// slice). WriteObject's trailing call is routed through this hook; the default
// (no hook) emits the binary's null/empty-list terminator byte-exactly (a 4-byte
// 0 count dword via Bio_WriteDwordPair — NOT a single byte; see 0x5f4b60). A list
// with entries needs VIBE_EventTable_LookupIdToName, so without a hook the sink
// fails instead of emitting unfaithful bytes. A hook receives the sink's
// (buf,cap,&pos,&ok) and the event-name list head (node+468).
using EventNamesHook = guild::u32 (*)(guild::u8* buf, guild::u32 cap, guild::u32* pos,
                                      bool* ok, guild::u8* listHead);
void WorldIoSetEventNamesHook(EventNamesHook hook);

// gilde.exe 0x5e5ab4 — VIBE_WorldIo_WriteObject. Serialize one scene-object node and
// (recursively) its first qualifying child and sibling. `node` is the object-record
// base. Returns false if the sink overran. Updates dst->pos / dst->ok.
bool WorldIoWriteObjectS(WorldIoSink* dst, guild::u8* node);

// gilde.exe 0x5e5f74 — VIBE_WorldIo_WriteBuildingData. Serialize a building's
// sub-state record. `bld` is the building-record base.
bool WorldIoWriteBuildingDataS(WorldIoSink* dst, const guild::u8* bld);

// gilde.exe 0x5e61ec — VIBE_WorldIo_WriteObjectCallback. Emit a 1 byte (present
// marker), temporarily clear node+496 (sibling), serialize the node, restore.
bool WorldIoWriteObjectCallbackS(WorldIoSink* dst, guild::u8* node);

} // namespace guild::world
