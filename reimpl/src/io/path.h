#pragma once
// gilde.exe — guild::io  (MODULE: VFS path-string helpers)
//
// In-place slash converters used pervasively by the VFS path resolver. The
// engine stores tree paths with forward slashes internally and converts to
// backslashes only when handing a path to the host OS.
//
//   VIBE_Path_ConvertBackslashToSlash @0x44eb54  ('\\' -> '/')
//   VIBE_Path_ConvertSlashToBackslash @0x44eb90  ('/'  -> '\\')

namespace guild::io {

// Replace every '\\' with '/' in place. Returns `s` (the original returns the
// strlen-derived end pointer in eax, but every caller uses it only for the
// side effect; exposing the buffer pointer keeps the C++ signature natural).
char* ConvertBackslashToSlash(char* s);

// Replace every '/' with '\\' in place. Returns `s`.
char* ConvertSlashToBackslash(char* s);

} // namespace guild::io
