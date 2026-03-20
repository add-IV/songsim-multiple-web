# Package

version       = "0.1.0"
author        = "add-IV"
description   = "Comparing different implementations of the songnim algorithm"
license       = "MIT"
srcDir        = "src"
bin           = @["songnim_multiple_web"]


# Dependencies

requires "nim >= 2.2.8"
requires "simplepng", "jester"