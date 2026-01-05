#!/usr/bin/env python3
"""
SparkMiner - Interactive Build & Flash Tool for Freenove ESP32-S3 Display
==========================================================================

Supports the Freenove FNK0104 ESP32-S3 with 2.8" ILI9341 display.

Usage:
    python flash-s3-display.py           # Interactive mode
    python flash-s3-display.py --build   # Build only
    python flash-s3-display.py --flash   # Flash only
    python flash-s3-display.py --monitor # Monitor only
    python flash-s3-display.py --all     # Build, flash, and monitor
"""

import os
import sys
import subprocess
import argparse
import time
from pathlib import Path

# ============================================================
# Configuration
# ============================================================
ENV_NAME = "esp32-s3-2432s028"
CHIP = "esp32s3"
BAUD_RATE = 921600
MONITOR_BAUD = 115200

# ESP32-S3 flash addresses (bootloader at 0x0)
FLASH_ADDRESSES = {
    "bootloader": 0x0000,
    "partitions": 0x8000,
    "firmware": 0x10000
}

# Colors for terminal output
class Colors:
    HEADER = '\033[95m'
    BLUE = '\033[94m'
    CYAN = '\033[96m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'

def color(text, c):
    """Wrap text in color codes (works on Windows 10+)"""
    return f"{c}{text}{Colors.ENDC}"

def print_banner():
    """Print the SparkMiner banner"""
    banner = """
+===============================================================+
|                                                               |
|    SparkMiner - Freenove ESP32-S3 Display Flash Tool          |
|                                                               |
|    Board: Freenove FNK0104 ESP32-S3 + 2.8" ILI9341 Display    |
|    Environment: esp32-s3-2432s028                             |
|                                                               |
+===============================================================+
"""
    print(color(banner, Colors.CYAN))

def print_bootloader_instructions():
    """Print instructions for entering bootloader/download mode"""
    print(f"\n{color('=' * 60, Colors.YELLOW)}")
    print(f"{color(' ESP32-S3 DOWNLOAD MODE INSTRUCTIONS', Colors.BOLD)}")
    print(f"{color('=' * 60, Colors.YELLOW)}")
    print("""
  To put the ESP32-S3 into download/bootloader mode:

    1. HOLD the BOOT button (do not release)
    2. While holding BOOT, press and release the RESET button
    3. Release the BOOT button

  The board is now in download mode and ready to flash.

  NOTE: The display will be blank/off in download mode - this is normal!
""")
    print(f"{color('=' * 60, Colors.YELLOW)}")

def print_reset_instructions():
    """Print instructions for resetting after flash"""
    print(f"\n{color('=' * 60, Colors.GREEN)}")
    print(f"{color(' FLASH COMPLETE - RESET REQUIRED', Colors.BOLD)}")
    print(f"{color('=' * 60, Colors.GREEN)}")
    print("""
  The firmware has been flashed successfully!

  To start the new firmware:

    >>> Press the RESET button on the board <<<

  The display should turn on and SparkMiner will start.
""")
    print(f"{color('=' * 60, Colors.GREEN)}")

def wait_for_user(prompt="Press ENTER when ready..."):
    """Wait for user to press Enter"""
    input(f"\n{color('[?]', Colors.CYAN)} {prompt}")

def get_script_dir():
    """Get the directory where this script is located"""
    return Path(__file__).parent.resolve()

def get_python_path():
    """Get the path to Python executable in venv"""
    script_dir = get_script_dir()
    venv_python = script_dir / ".venv" / "Scripts" / "python.exe"
    if venv_python.exists():
        return str(venv_python)
    return sys.executable

def get_pio_cmd():
    """Get the PlatformIO command as a list (uses python -m platformio to avoid launcher issues)"""
    python_path = get_python_path()
    return [python_path, "-m", "platformio"]

def check_pio_installed():
    """Check if PlatformIO is installed"""
    try:
        result = subprocess.run(get_pio_cmd() + ["--version"], capture_output=True, text=True)
        return result.returncode == 0
    except Exception:
        return False

def run_command(cmd, description=None, capture=False):
    """Run a command and handle output"""
    if description:
        print(f"\n{color('[INFO]', Colors.BLUE)} {description}")

    try:
        if capture:
            result = subprocess.run(cmd, capture_output=True, text=True, cwd=get_script_dir())
            return result
        else:
            result = subprocess.run(cmd, cwd=get_script_dir())
            return result
    except FileNotFoundError as e:
        print(f"{color('[ERROR]', Colors.RED)} Command not found: {cmd[0]}")
        return None
    except KeyboardInterrupt:
        print(f"\n{color('[CANCELLED]', Colors.YELLOW)} Operation cancelled by user")
        return None

def list_serial_ports():
    """List available serial ports"""
    if not check_pio_installed():
        print(f"{color('[ERROR]', Colors.RED)} PlatformIO not found!")
        return []

    result = run_command(get_pio_cmd() + ["device", "list", "--serial"], capture=True)
    if not result or result.returncode != 0:
        return []

    ports = []
    lines = result.stdout.split('\n')
    current_port = None
    current_desc = ""

    for line in lines:
        line = line.strip()
        if line.startswith("COM") or line.startswith("/dev/"):
            if current_port:
                ports.append((current_port, current_desc))
            current_port = line
            current_desc = ""
        elif current_port and "Description:" in line:
            current_desc = line.replace("Description:", "").strip()

    if current_port:
        ports.append((current_port, current_desc))

    return ports

def select_port(for_flashing=False):
    """Interactive port selection"""
    print(f"\n{color('[INFO]', Colors.BLUE)} Scanning for serial ports...")
    ports = list_serial_ports()

    if not ports:
        print(f"{color('[WARNING]', Colors.YELLOW)} No serial ports detected!")

        if for_flashing:
            print(f"\n{color('[TIP]', Colors.CYAN)} The ESP32-S3 might not be in download mode yet.")
            print_bootloader_instructions()
            wait_for_user("Put board in download mode, then press ENTER to scan again...")
            ports = list_serial_ports()

        if not ports:
            print("\nTroubleshooting:")
            print("  1. Connect the ESP32-S3 via USB-C")
            print("  2. Try a different USB cable (data cable, not charge-only)")
            print("  3. Install CP2102/CH340 drivers if needed")
            print("  4. Try a different USB port")

            manual = input(f"\n{color('[?]', Colors.CYAN)} Enter COM port manually (or press Enter to cancel): ").strip()
            return manual if manual else None

    print(f"\n{color('Available Ports:', Colors.GREEN)}")
    print("-" * 60)

    for i, (port, desc) in enumerate(ports, 1):
        # Highlight likely ESP32-S3 ports
        if "USB" in desc.upper() or "SERIAL" in desc.upper() or "CP210" in desc.upper() or "CH340" in desc.upper() or "JTAG" in desc.upper():
            print(f"  {color(f'[{i}]', Colors.CYAN)} {color(port, Colors.GREEN)} - {desc}")
        else:
            print(f"  [{i}] {port} - {desc}")

    print("-" * 60)

    while True:
        try:
            choice = input(f"\n{color('[?]', Colors.CYAN)} Select port number (1-{len(ports)}) or enter COM port: ").strip()

            if not choice:
                return None

            # Check if it's a number selection
            if choice.isdigit():
                idx = int(choice) - 1
                if 0 <= idx < len(ports):
                    return ports[idx][0]
                print(f"{color('[ERROR]', Colors.RED)} Invalid selection!")
            else:
                # Assume it's a direct port name
                return choice.upper() if choice.upper().startswith("COM") else choice

        except (ValueError, IndexError):
            print(f"{color('[ERROR]', Colors.RED)} Invalid input!")

def check_firmware_exists():
    """Check if firmware files exist"""
    script_dir = get_script_dir()
    build_dir = script_dir / ".pio" / "build" / ENV_NAME

    factory_bin = None
    firmware_bin = build_dir / "firmware.bin"

    # Check for factory bin in firmware folder
    firmware_dirs = list((script_dir / "firmware").glob("*")) if (script_dir / "firmware").exists() else []
    for fw_dir in sorted(firmware_dirs, reverse=True):  # Newest first
        factory = fw_dir / f"{ENV_NAME}_factory.bin"
        if factory.exists():
            factory_bin = factory
            break

    return {
        "build_firmware": firmware_bin if firmware_bin.exists() else None,
        "factory_bin": factory_bin,
        "build_dir": build_dir
    }

def build_firmware():
    """Build the firmware"""
    if not check_pio_installed():
        print(f"{color('[ERROR]', Colors.RED)} PlatformIO not found!")
        return False

    print(f"\n{color('=' * 60, Colors.CYAN)}")
    print(f"{color(' Building SparkMiner for ESP32-S3 Display', Colors.BOLD)}")
    print(f"{color('=' * 60, Colors.CYAN)}")
    print(f"  Environment: {ENV_NAME}")
    print(f"  Chip: {CHIP}")
    print(f"{color('=' * 60, Colors.CYAN)}\n")

    result = run_command(get_pio_cmd() + ["run", "-e", ENV_NAME])

    if result and result.returncode == 0:
        print(f"\n{color('[SUCCESS]', Colors.GREEN)} Build completed successfully!")

        # Show firmware info
        fw = check_firmware_exists()
        if fw["factory_bin"]:
            size = fw["factory_bin"].stat().st_size
            print(f"  Factory: {fw['factory_bin'].name} ({size:,} bytes)")
        return True
    else:
        print(f"\n{color('[ERROR]', Colors.RED)} Build failed!")
        return False

def flash_firmware(port, interactive=True):
    """Flash firmware to the device"""
    if not port:
        print(f"{color('[ERROR]', Colors.RED)} No port specified!")
        return False

    fw = check_firmware_exists()

    # Prefer factory bin if available
    firmware_file = fw["factory_bin"] or fw["build_firmware"]

    if not firmware_file:
        print(f"{color('[ERROR]', Colors.RED)} No firmware found! Run build first.")
        return False

    python_path = get_python_path()

    print(f"\n{color('=' * 60, Colors.CYAN)}")
    print(f"{color(' Flashing SparkMiner to ESP32-S3', Colors.BOLD)}")
    print(f"{color('=' * 60, Colors.CYAN)}")
    print(f"  Port: {port}")
    print(f"  Firmware: {firmware_file.name}")
    print(f"  Size: {firmware_file.stat().st_size:,} bytes")
    print(f"  Baud: {BAUD_RATE}")
    print(f"{color('=' * 60, Colors.CYAN)}")

    if interactive:
        print_bootloader_instructions()
        wait_for_user("Put the board in DOWNLOAD MODE, then press ENTER to flash...")

    print(f"\n{color('[FLASH]', Colors.YELLOW)} Flashing firmware...")

    # Build esptool command for ESP32-S3
    # Factory bin goes at 0x0, firmware.bin goes at 0x10000
    if "factory" in str(firmware_file):
        flash_addr = "0x0"
    else:
        flash_addr = "0x10000"

    cmd = [
        python_path, "-m", "esptool",
        "--chip", CHIP,
        "--port", port,
        "--baud", str(BAUD_RATE),
        "--before", "default_reset",
        "--after", "hard_reset",
        "write_flash",
        "-z",
        "--flash-mode", "dio",
        "--flash-freq", "80m",
        "--flash-size", "detect",
        flash_addr, str(firmware_file)
    ]

    result = run_command(cmd)

    if result and result.returncode == 0:
        print(f"\n{color('[SUCCESS]', Colors.GREEN)} Flash completed successfully!")
        return True
    else:
        print(f"\n{color('[ERROR]', Colors.RED)} Flash failed!")
        print(f"\n{color('Troubleshooting:', Colors.YELLOW)}")
        print("  1. Make sure you entered download mode correctly:")
        print("     - Hold BOOT, press RESET, release BOOT")
        print("  2. Try a different USB port or cable")
        print("  3. Check if another program is using the port")
        print("  4. Ensure drivers are installed (CP2102/CH340)")
        print("  5. Try again - sometimes it takes a few attempts")
        return False

def open_monitor(port, after_flash=False):
    """Open serial monitor"""
    if not port:
        print(f"{color('[ERROR]', Colors.RED)} No port specified!")
        return

    if not check_pio_installed():
        print(f"{color('[ERROR]', Colors.RED)} PlatformIO not found!")
        return

    if after_flash:
        print_reset_instructions()
        wait_for_user("Press RESET on the board, then press ENTER to start monitor...")

    print(f"\n{color('=' * 60, Colors.CYAN)}")
    print(f"{color(' Serial Monitor', Colors.BOLD)}")
    print(f"{color('=' * 60, Colors.CYAN)}")
    print(f"  Port: {port}")
    print(f"  Baud: {MONITOR_BAUD}")
    print(f"  Exit: Ctrl+C")
    print(f"{color('=' * 60, Colors.CYAN)}\n")

    run_command(get_pio_cmd() + ["device", "monitor", "-b", str(MONITOR_BAUD), "-p", port])

def interactive_menu():
    """Show interactive menu"""
    while True:
        print(f"\n{color('Main Menu:', Colors.GREEN)}")
        print("-" * 40)
        print(f"  {color('[1]', Colors.CYAN)} Build firmware")
        print(f"  {color('[2]', Colors.CYAN)} Flash firmware")
        print(f"  {color('[3]', Colors.CYAN)} Serial monitor")
        print(f"  {color('[4]', Colors.CYAN)} Build + Flash + Monitor (Full cycle)")
        print(f"  {color('[5]', Colors.CYAN)} Check firmware status")
        print(f"  {color('[6]', Colors.CYAN)} Clean build")
        print(f"  {color('[0]', Colors.CYAN)} Exit")
        print("-" * 40)

        choice = input(f"\n{color('[?]', Colors.CYAN)} Select option: ").strip()

        if choice == "1":
            build_firmware()

        elif choice == "2":
            port = select_port(for_flashing=True)
            if port:
                flash_firmware(port, interactive=True)

        elif choice == "3":
            port = select_port(for_flashing=False)
            if port:
                open_monitor(port, after_flash=False)

        elif choice == "4":
            # Full cycle
            if build_firmware():
                port = select_port(for_flashing=True)
                if port and flash_firmware(port, interactive=True):
                    open_monitor(port, after_flash=True)

        elif choice == "5":
            fw = check_firmware_exists()
            print(f"\n{color('Firmware Status:', Colors.GREEN)}")
            print("-" * 40)
            if fw["factory_bin"]:
                size = fw["factory_bin"].stat().st_size
                print(f"  {color('[OK]', Colors.GREEN)} Factory: {fw['factory_bin'].name}")
                print(f"           Size: {size:,} bytes")
            else:
                print(f"  {color('[X]', Colors.RED)} Factory: Not found")

            if fw["build_firmware"]:
                size = fw["build_firmware"].stat().st_size
                print(f"  {color('[OK]', Colors.GREEN)} Build: {fw['build_firmware'].name}")
                print(f"         Size: {size:,} bytes")
            else:
                print(f"  {color('[X]', Colors.RED)} Build: Not found (run build first)")
            print("-" * 40)

        elif choice == "6":
            if check_pio_installed():
                run_command(get_pio_cmd() + ["run", "-e", ENV_NAME, "-t", "clean"], "Cleaning build...")
                print(f"{color('[SUCCESS]', Colors.GREEN)} Build cleaned!")

        elif choice == "0":
            print(f"\n{color('Goodbye! Happy mining!', Colors.CYAN)}\n")
            break

        else:
            print(f"{color('[ERROR]', Colors.RED)} Invalid option!")

def main():
    """Main entry point"""
    # Enable ANSI colors on Windows
    if sys.platform == "win32":
        os.system("")  # Enable ANSI escape codes

    parser = argparse.ArgumentParser(
        description="SparkMiner Build & Flash Tool for Freenove ESP32-S3 Display",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python flash-s3-display.py           # Interactive mode
  python flash-s3-display.py --build   # Build only
  python flash-s3-display.py --flash COM5   # Flash to COM5
  python flash-s3-display.py --all COM5     # Build, flash, monitor
        """
    )

    parser.add_argument("--build", "-b", action="store_true", help="Build firmware")
    parser.add_argument("--flash", "-f", nargs="?", const="auto", metavar="PORT", help="Flash firmware (auto-detect or specify port)")
    parser.add_argument("--monitor", "-m", nargs="?", const="auto", metavar="PORT", help="Open serial monitor")
    parser.add_argument("--all", "-a", nargs="?", const="auto", metavar="PORT", help="Build, flash, and monitor")
    parser.add_argument("--clean", "-c", action="store_true", help="Clean build directory")
    parser.add_argument("--no-interactive", "-y", action="store_true", help="Skip interactive prompts (for automation)")

    args = parser.parse_args()

    print_banner()

    # Check PlatformIO
    if not check_pio_installed():
        print(f"{color('[ERROR]', Colors.RED)} PlatformIO not found!")
        print("\nInstall with: pip install platformio")
        print("Or activate venv: .venv\\Scripts\\activate")
        sys.exit(1)

    interactive = not args.no_interactive

    # Handle command-line arguments
    if args.clean:
        run_command(get_pio_cmd() + ["run", "-e", ENV_NAME, "-t", "clean"], "Cleaning build...")
        return

    if args.build or args.all:
        if not build_firmware():
            if args.all:
                sys.exit(1)
            return

    if args.flash or args.all:
        port_arg = args.flash if args.flash and args.flash != "auto" else (args.all if args.all and args.all != "auto" else None)
        port = port_arg or select_port(for_flashing=True)
        if port:
            if not flash_firmware(port, interactive=interactive):
                sys.exit(1)

            if args.all or args.monitor:
                open_monitor(port, after_flash=interactive)
        return

    if args.monitor:
        port = args.monitor if args.monitor != "auto" else select_port(for_flashing=False)
        if port:
            open_monitor(port, after_flash=False)
        return

    # No arguments - interactive mode
    interactive_menu()

if __name__ == "__main__":
    main()
