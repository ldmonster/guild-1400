# Protection layer — StarForce unpacker

`gilde.dll` is StarForce-protected. The on-disk image is a thin loader: a real PE entry
that jumps into a 3-function unpack/relocation stub which reconstructs the encrypted
`.sforce` payload in memory. This document covers those functions in detail.

## Segment layout

| Segment | Range | Size | Perm | Notes |
|---|---|---|---|---|
| `.text` | `0x69001000–0x69006000` | `0x5000` | RWX | **All zeros on disk** — runtime-filled by unpacker |
| `.sforce` | `0x69006000–0x69354000` | `0x34E000` | RWX | StarForce payload (compressed/encrypted) + the 3 stub funcs at its head |
| `.idata` | `0x69354000–0x69354204` | `0x204` | RW | Loader import table (StarForce APIs only) |
| `.rdata` | `0x69354204–0x69355000` | `0xDFC` | RW | Contains `DllEntryPoint` thunk + loader strings |
| `.data` | `0x69355000–0x69356000` | `0x1000` | RW | Loader data |

RWX on the code segments is itself a packer tell — the stub needs to write decompressed
code into executable memory.

## Function 1 — `DllEntryPoint` @ `0x69354d0c` (.rdata)

```
DllEntryPoint:  call protect_1        ; E8 EF 12 CB FF -> 0x69006000
```

Single instruction. The real PE AddressOfEntryPoint; hands control straight to the
unpacker. (Lives in `.rdata`, not a normal code section — another packer artifact.)

## Function 2 — `VIBE_StarForce_UnpackEntryStub` @ `0x69006000`

Self-modifying protection entry stub.

- Reads its own return address, computes `v1 = retaddr - 5`, then `++*(BYTE*)v1` and
  `*(DWORD*)(v1+1) -= 11439` — i.e. it **rewrites its own code bytes** before continuing
  (anti-static / anti-tamper).
- Calls `VIBE_StarForce_RelocAndUnpackLoop`.
- Uses `retf` (far return) and a `JUMPOUT(0x69006031)` to obfuscate flow — the decompiler
  cannot follow it cleanly, which is intended.

## Function 3 — `VIBE_StarForce_RelocAndUnpackLoop` @ `0x6900602a`

Two phases:

1. **Block unpack loop.** Walks a descriptor list (`v13`) terminated by `-1`. For each
   block it advances `v13` by the block's encoded size and calls
   `VIBE_StarForce_LZ_Decompress(*v2)` to decompress that block into memory. This rebuilds
   the section bytes (including `.text`).

2. **Base-relocation loop.** Computes the load delta
   `v5 = a2 - DllEntryPoint_preferred` (loaded base minus link-time base). Then over a
   packed reloc bitstream it walks targets and does `*v9 += v5`, fixing up absolute
   addresses in the freshly-decompressed image. Standard PE relocation, hand-rolled.

After this returns, the real `gilde.dll` (game + multiplayer + networking) exists in
memory at its loaded base.

## Function 4 — `VIBE_StarForce_LZ_Decompress` @ `0x69006061`

LZ-style decompressor over a 16-bit-word bitstream.

- Maintains a control word `v4` refilled 16 bits at a time from the source (`*(_WORD*)v5`,
  `v5 += 2`), consumed via shifts and `rcl ebx, 1`.
- Decision tables at `STACK[0x1AC]` / `STACK[0x1BC]` map the extracted control bits to a
  match length (`v8`) and a back-reference distance (`_EBX`).
- Emits **literals** (`*v3++ = *v5++`) and **matches**
  (`qmemcpy(v3, &v3[-distance], length); v3 += length`).
- A length byte of `0xFF` terminates the current block (`return &v3[-a2]`).

This is the engine that turns the encrypted `.sforce` blob into runnable code. Its output
is **not** captured in the static IDB.

## Why no further static progress is possible here

The decompressed image is produced at runtime in process memory. Nothing downstream of
these 4 functions (the actual game) is present on disk. To analyze the game you must
**unpack first** (see [../network/networking.md](../network/networking.md#path-forward)).
