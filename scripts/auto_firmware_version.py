#!/usr/bin/env python3
"""
Auto firmware version
Adds -D AUTO_VERSION="v2.0.0" build flag
"""

Import("env")

build_version = "v2.0.0"
print("Firmware Version: " + build_version)

env.Append(
    BUILD_FLAGS=['-D AUTO_VERSION="' + build_version + '"']
)