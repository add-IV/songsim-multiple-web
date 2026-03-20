# Package

version       = "0.1.0"
author        = "add-IV"
description   = "Comparing different implementations of the songnim algorithm"
license       = "MIT"
srcDir        = "src"
bin           = @["songsim_multiple_web"]


# Dependencies

requires "nim >= 2.2.8"
requires "simplepng", "jester"
requires "httpbeast >= 0.4.2"

before build:
  echo "Building Rust static library..."
  exec "cargo build --release --lib"
  echo "Rust build complete."