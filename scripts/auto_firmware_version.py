#!/usr/bin/env python3
"""
Auto firmware version from git tags
Adds -D AUTO_VERSION="vX.X.X" build flag
"""

import subprocess

Import("env")

def get_firmware_specifier_build_flag():
    try:
        ret = subprocess.run(["git", "describe", "--tags", "--dirty"],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if ret.returncode == 0:
            build_version = ret.stdout.strip()
        else:
            build_version = "dev"
    except:
        build_version = "dev"

    build_flag = '-D AUTO_VERSION=\\"' + build_version + '\\"'
    print("Firmware Version: " + build_version)
    return build_flag

env.Append(
    BUILD_FLAGS=[get_firmware_specifier_build_flag()]
)
