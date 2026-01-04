#!/usr/bin/env python3
"""
Post-build script for SparkMiner firmware merging
Detects ESP32 type and creates factory + update firmware files
"""

import os
import subprocess
from pathlib import Path

Import("env")

def detect_esp32_type(bootloader_path):
    """Detect ESP32 type by analyzing bootloader signature"""
    try:
        with open(bootloader_path, 'rb') as f:
            bootloader_data = f.read()

        if len(bootloader_data) < 13:
            return 'ESP32'

        chip_id = bootloader_data[12]
        size = len(bootloader_data)

        if chip_id == 0x09 and size >= 15000:
            return 'ESP32-S3'
        elif chip_id == 0x05 and size >= 13000 and size < 14000:
            return 'ESP32-C3'
        elif chip_id == 0x02 and size >= 13000 and size < 15000:
            return 'ESP32-S2'
        elif chip_id == 0x00 and size >= 17000:
            return 'ESP32'
        else:
            if size >= 17000:
                return 'ESP32'
            elif size >= 15000:
                return 'ESP32-S3'
            elif size >= 13600:
                return 'ESP32-S2'
            elif size >= 13000:
                return 'ESP32-C3'
            else:
                return 'ESP32'

    except Exception as e:
        print(f"Warning: Could not analyze bootloader ({e})")
        return 'ESP32'

def get_memory_layout(esp_type):
    """Get memory addresses for each ESP32 variant"""
    if esp_type == 'ESP32-C3':
        return {'bootloader': 0x0000, 'partitions': 0x8000, 'firmware': 0x10000}
    elif esp_type == 'ESP32-S2':
        return {'bootloader': 0x1000, 'partitions': 0x8000, 'boot_app0': 0xE000, 'firmware': 0x10000}
    elif esp_type == 'ESP32-S3':
        return {'bootloader': 0x0000, 'partitions': 0x8000, 'firmware': 0x10000}
    else:
        return {'bootloader': 0x1000, 'partitions': 0x8000, 'boot_app0': 0xE000, 'firmware': 0x10000}

def get_firmware_version():
    """Get firmware version from git"""
    try:
        result = subprocess.run(["git", "describe", "--tags", "--dirty"],
                              stdout=subprocess.PIPE, text=True,
                              cwd=env.subst("$PROJECT_DIR"))
        if result.returncode == 0:
            return result.stdout.strip()
    except:
        pass
    return "dev"

def create_merged_firmware(source, target, env):
    """Create merged factory firmware after build"""

    project_dir = Path(env.subst("$PROJECT_DIR"))
    build_dir = Path(env.subst("$BUILD_DIR"))
    env_name = env.subst("$PIOENV")
    version = get_firmware_version()

    print(f"\n Building firmware files for {env_name}...")

    bootloader_file = build_dir / "bootloader.bin"
    partitions_file = build_dir / "partitions.bin"
    boot_app0_file = build_dir / "boot_app0.bin"
    firmware_file = build_dir / "firmware.bin"

    if not firmware_file.exists():
        print(f"Firmware file not found: {firmware_file}")
        return

    esp_type = detect_esp32_type(bootloader_file) if bootloader_file.exists() else 'ESP32'
    addresses = get_memory_layout(esp_type)

    print(f"Detected: {esp_type} (bootloader at 0x{addresses['bootloader']:04X})")

    # Output directory
    version_dir = project_dir / "firmware" / version
    version_dir.mkdir(parents=True, exist_ok=True)

    factory_file = version_dir / f"{env_name}_factory.bin"
    update_file = version_dir / f"{env_name}_firmware.bin"

    # Copy firmware.bin as update file
    try:
        import shutil
        shutil.copy2(firmware_file, update_file)
        print(f"Firmware: {update_file.name}")
    except Exception as e:
        print(f"Error creating firmware file: {e}")
        return

    # Create factory file (merged)
    try:
        merged_size = 0x400000  # 4MB
        merged_data = bytearray([0xFF] * merged_size)
        max_address = 0

        files_to_merge = {
            'bootloader': bootloader_file,
            'partitions': partitions_file,
            'firmware': firmware_file
        }

        if 'boot_app0' in addresses:
            files_to_merge['boot_app0'] = boot_app0_file

        for file_type, file_path in files_to_merge.items():
            if file_path.exists():
                address = addresses[file_type]
                with open(file_path, 'rb') as f:
                    data = f.read()

                print(f"   {file_type} at 0x{address:06X}: {len(data)} bytes")

                if address + len(data) <= merged_size:
                    merged_data[address:address+len(data)] = data
                    max_address = max(max_address, address + len(data))

        actual_end = ((max_address + 4095) // 4096) * 4096

        with open(factory_file, 'wb') as f:
            f.write(merged_data[:actual_end])

        print(f"Factory: {factory_file.name} ({actual_end} bytes)")

    except Exception as e:
        print(f"Error creating factory file: {e}")

env.AddPostAction("$BUILD_DIR/firmware.bin", create_merged_firmware)
