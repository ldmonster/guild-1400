# gilde.dll — Reverse Engineering Report

**Target:** `gilde.dll` (Die Gilde / Europa 1400 — "The Guild", 2002)
**Module base:** `0x69000000`  •  **Image size:** `0x35C000` (~3.4 MB)  •  **Arch:** x86 (32-bit)
**MD5:** `773313992065912ece8aebfbaf26f06e`
**SHA256:** `7c9cffcfc5b32236ddbd64fca6b6ef8031193b7ca2ecd57b71c03488dafb0060`
**Last updated:** 2026-06-07

---

## TL;DR / Headline finding

This DLL is **packed with StarForce protection**. The file on disk contains **only the
StarForce unpacking/relocation stub (3 functions)**. The entire game — including all
multiplayer, networking, packet, lobby, and session code — is **compressed and encrypted
inside the `.sforce` segment** and is reconstructed in memory **only at runtime**.

**Consequence:** the multiplayer/networking flow cannot be mapped from this static image,
because that code does not exist in analyzable form in the file. There are **no WinSock
imports** (no `ws2_32`/`wsock32`, no `socket`/`connect`/`send`/`recv`) and no game strings.
What IDA sees is the loader, not the game.

All 3 statically-present functions have been named (`VIBE_` prefix) and commented. See
[protection/starforce_unpacker.md](protection/starforce_unpacker.md) and
[network/networking.md](network/networking.md).

---

## Evidence for the StarForce verdict

| Signal | Observation |
|---|---|
| Section name | `.sforce` segment `0x69006000–0x69354000` (0x34E000, ~99% of the image), permissions **RWX** |
| String | `"protect.dll"` @ `0x69354d00`, **3 xrefs** — StarForce protection module |
| Function count | Only **3** functions in the whole 3.4 MB image; 1 leaf, 2 wrappers |
| `.text` is empty | `.text` (`0x69001000–0x69006000`) reads as **all zeros on disk** — runtime-filled |
| Packed entry | `0x69002f72` (listed as an entry point) is **all zeros** — encrypted/packed data |
| Self-modifying stub | `VIBE_StarForce_UnpackEntryStub` patches its own bytes, uses `retf`/`JUMPOUT` |
| Loader imports only | `DeviceIoControl`, `OpenSCManager`/`StartService`/`ControlService`, `OpenProcessToken`/`AdjustTokenPrivileges`, `VirtualAlloc`/`VirtualFree`, registry + version APIs — the StarForce driver/loader toolkit, **not** game APIs |
| No networking | Entire import table searched: **zero** socket APIs; only `SendMessageA` (USER32 window message, unrelated to sockets) |

---

## Statically-present functions (the complete list)

| Address | Name (VIBE) | Role |
|---|---|---|
| `0x69354d0c` | `DllEntryPoint` (commented) | Real PE entry; single `call protect_1` → launches unpacker. In `.rdata`. |
| `0x69006000` | `VIBE_StarForce_UnpackEntryStub` | Self-modifying protection entry stub. Calls the reloc/unpack loop. |
| `0x6900602a` | `VIBE_StarForce_RelocAndUnpackLoop` | Iterates packed block list → calls decompressor per block; applies base relocations (delta = loaded_base − preferred). |
| `0x69006061` | `VIBE_StarForce_LZ_Decompress` | LZ-style bitstream decompressor (`rcl`-driven bit reads, back-ref `qmemcpy(dst, dst-dist, len)`, `0xFF` terminator). |

**This table is exhaustive — these are all the functions that exist in the binary.**
The primary goal ("name all functions") is therefore complete for the static image.

---

## Runtime control flow (the unpacker)

```
PE loader
  └─> DllEntryPoint (0x69354d0c)          ; call protect_1
        └─> VIBE_StarForce_UnpackEntryStub (0x69006000)
              ; self-patches code, far-return obfuscation
              └─> VIBE_StarForce_RelocAndUnpackLoop (0x6900602a)
                    ├─ loop: per block -> VIBE_StarForce_LZ_Decompress (0x69006061)
                    │        ; decompress section bytes into memory
                    └─ loop: apply base relocations (*target += delta)
        ==> real gilde.dll image (game + multiplayer + network) now in memory
```

---

## What this means for the multiplayer goal

The requested deliverables (packet structures, opcodes, handshake/auth state machine,
send/recv paths, lobby/session/matchmaking) **require the unpacked image**. They are not
recoverable from this file. See [network/networking.md](network/networking.md) for the
detailed null-result and the recommended dynamic approach (dump after OEP / unpack first).

---

## Index

- [protection/starforce_unpacker.md](protection/starforce_unpacker.md) — the unpacker, function-by-function.
- [network/networking.md](network/networking.md) — networking analysis (null result + path forward).

## Open questions / next targets

*(Everything statically resolvable is resolved. The remaining items all require a runtime-unpacked image — they cannot be progressed against this file.)*

1. **Obtain an unpacked image** — run under a debugger, break at the original entry point
   (OEP) after `VIBE_StarForce_RelocAndUnpackLoop` finishes, and dump the reconstructed
   module. Only then do the multiplayer targets below become addressable.
2. Locate the real import table post-unpack — confirm whether `ws2_32`/`wsock32` (or
   DirectPlay `dplayx`) is used for multiplayer.
3. Map network init / socket setup / connect / send / recv wrappers (post-unpack).
4. Reconstruct packet headers, opcodes, length/seq/ack, session tokens, payload schema.
5. Map the protocol state machine (handshake, auth, keepalive, lobby, gameplay, teardown).
