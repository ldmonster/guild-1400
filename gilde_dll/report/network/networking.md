# Networking analysis — null result (packed image)

## Summary

**No networking code is present in the static image.** This is a direct consequence of
StarForce packing (see [../protection/starforce_unpacker.md](../protection/starforce_unpacker.md)).
Every multiplayer-focused requirement (socket setup, connect/send/recv, encryption,
serialization, packet structures, opcodes, handshake/auth state machine, lobby, session
tokens, keepalive, teardown) maps to code that only exists **after runtime unpacking**.

## What was checked (and the result)

| Check | Method | Result |
|---|---|---|
| WinSock imports | full import table scan | **None.** No `ws2_32`, `wsock32`, `WSAStartup`, `socket`, `connect`, `send`, `recv`, `bind`, `listen`, `select`. |
| DirectPlay | import table scan | **None.** No `dplayx`/`dpnet`. |
| Network strings | regex over all 130 strings: `socket\|connect\|send\|recv\|server\|client\|host\|player\|packet\|net\|...` | **1 hit only:** `"SendMessageA"` (USER32 — window message, **not** a socket). |
| Game strings | string survey | **None.** Only loader strings: `protect.dll`, `KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `ADVAPI32.dll`, `VERSION.dll`. |
| Send/recv code paths | function survey | **None.** Only the 3 unpacker functions exist. |

The only import categorized as "network" by the survey is `SendMessageA`, which is the
USER32 window-message API, unrelated to network sockets.

## The imports that *are* present (all loader, not game)

The static `.idata` is the **StarForce loader's** import set, e.g.:
- `DeviceIoControl` — talk to the StarForce kernel driver.
- `OpenSCManagerA`/`CreateServiceA`/`StartServiceA`/`ControlService`/`OpenServiceA`/
  `CloseServiceHandle` — install/start the protection driver service.
- `OpenProcessToken`/`AdjustTokenPrivileges`/`LookupPrivilegeValueA` — acquire privileges
  for the driver.
- `VirtualAlloc`/`VirtualFree`/`GetProcessHeap`/`HeapAlloc` — allocate space to unpack into.
- `RegCreateKeyExA`/`RegQueryValueExA`/… — protection/license registry checks.
- `GetFileVersionInfoA`/`VerQueryValueA` — version/integrity checks.

None of these belong to the game's multiplayer subsystem; the game's real imports are
resolved at runtime after unpacking.

## Path forward (requires unpacking — out of scope for the static file) {#path-forward}

To deliver the multiplayer map, an **unpacked image is mandatory**:

1. Load the host process under a debugger (x64dbg/IDA debugger). Set the StarForce-aware
   options or hardware breakpoints; let `VIBE_StarForce_RelocAndUnpackLoop` complete.
2. Break at OEP (first instruction of the reconstructed game code), then **dump the module**
   from memory and rebuild a usable import table (Scylla/ImpRec).
3. Re-import the dumped image into IDA. Then, and only then, pursue:
   - real import table — confirm `ws2_32`/`wsock32` (or DirectPlay) usage;
   - socket setup / connect / send / recv wrappers;
   - packet build/queue/transmit and receive/validate/parse/dispatch paths;
   - packet structure: magic/opcode/version/flags, length, seq/ack, session token,
     payload schema, checksum/encryption metadata;
   - protocol state machine: handshake → auth → keepalive → lobby → gameplay → teardown;
   - server-directed vs peer/client-directed traffic.

Until an unpacked image exists, there is nothing in this binary to map for these items.

## Status

- All static analysis complete. **No open static items.**
- All remaining networking work is **blocked on obtaining an unpacked image** and tracked
  in the parent [../README.md](../README.md#open-questions--next-targets).
