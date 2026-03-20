import os

# Rust static library
const rustLib = currentSourcePath.parentDir() / ".." / "target" / "release" / "libsongsim_multiple_web.a"
{.passL: rustLib.}
{.passL: "-lpthread -ldl -lm".}

proc songsimRust*(text: cstring, outLen: ptr csize_t): ptr UncheckedArray[uint8]
  {.importc: "songsim_rust", cdecl.}
proc songsimRustFree*(p: ptr UncheckedArray[uint8], len: csize_t)
  {.importc: "songsim_rust_free", cdecl.}

# C implementation compiled directly
{.compile: "songsim.c".}

proc songsimC*(text: cstring, outLen: ptr csize_t): ptr UncheckedArray[uint8]
  {.importc: "songsim_c", cdecl.}
proc songsimCFree*(p: ptr UncheckedArray[uint8], len: csize_t)
  {.importc: "songsim_c_free", cdecl.}
