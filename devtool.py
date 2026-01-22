#!/usr/bin/env python3
"""
ESP32 DevTool - Universal Build, Flash & Monitor Suite
=======================================================

A comprehensive, interactive development tool for ESP32 projects.
Configure via devtool.toml for any project.

Usage:
    python devtool.py                     # Interactive menu
    python devtool.py build               # Build current board
    python devtool.py flash               # Flash current board
    python devtool.py monitor             # Serial monitor
    python devtool.py all                 # Build + Flash + Monitor
    python devtool.py release             # Build all boards for release
    python devtool.py --help              # Full help

Configuration:
    Edit devtool.toml to customize boards, settings, and project info.
"""

import os
import sys
import subprocess
import argparse
import time
import shutil
import json
from pathlib import Path
from dataclasses import dataclass, field
from typing import Optional, List, Dict, Tuple, Any
from concurrent.futures import ThreadPoolExecutor, as_completed

# Version of this tool
DEVTOOL_VERSION = "1.0.0"

# ============================================================
# Configuration
# ============================================================

@dataclass
class BoardConfig:
    """Configuration for a supported board"""
    key: str
    name: str
    env: str
    chip: str
    description: str = ""
    flash_baud: int = 921600
    monitor_baud: int = 115200
    flash_mode: str = "dio"
    flash_freq: str = "80m"
    needs_boot_mode: bool = True
    port_changes_on_reset: bool = False
    boot_button: str = "BOOT"
    reset_button: str = "RESET"
    group: str = "Other"


@dataclass
class ProjectConfig:
    """Project-level configuration"""
    name: str = "ESP32 Project"
    description: str = ""
    version_source: str = "git"
    firmware_dir: str = "firmware"
    pio_dir: str = "."
    default_board: str = ""
    boards: Dict[str, BoardConfig] = field(default_factory=dict)
    release_parallel: bool = False
    keep_releases: int = 5
    monitor_filters: List[str] = field(default_factory=list)


def load_config(config_path: Path) -> ProjectConfig:
    """Load configuration from TOML file"""
    config = ProjectConfig()

    if not config_path.exists():
        print(f"{Colors.YELLOW}[WARN]{Colors.ENDC} No devtool.toml found, using defaults")
        return config

    try:
        # Try tomllib (Python 3.11+) first, fall back to toml package
        try:
            import tomllib
            with open(config_path, "rb") as f:
                data = tomllib.load(f)
        except ImportError:
            try:
                import toml
                with open(config_path, "r") as f:
                    data = toml.load(f)
            except ImportError:
                # Manual TOML parsing for basic cases
                data = parse_simple_toml(config_path)

        # Project settings
        if "project" in data:
            p = data["project"]
            config.name = p.get("name", config.name)
            config.description = p.get("description", config.description)
            config.version_source = p.get("version_source", config.version_source)
            config.firmware_dir = p.get("firmware_dir", config.firmware_dir)
            config.pio_dir = p.get("pio_dir", config.pio_dir)

        # Defaults
        defaults = data.get("defaults", {})
        config.default_board = defaults.get("board", "")
        default_flash_baud = defaults.get("flash_baud", 921600)
        default_monitor_baud = defaults.get("monitor_baud", 115200)
        default_flash_mode = defaults.get("flash_mode", "dio")
        default_flash_freq = defaults.get("flash_freq", "80m")

        # Boards
        if "boards" in data:
            for key, b in data["boards"].items():
                config.boards[key] = BoardConfig(
                    key=key,
                    name=b.get("name", key),
                    env=b.get("env", key),
                    chip=b.get("chip", "esp32"),
                    description=b.get("description", ""),
                    flash_baud=b.get("flash_baud", default_flash_baud),
                    monitor_baud=b.get("monitor_baud", default_monitor_baud),
                    flash_mode=b.get("flash_mode", default_flash_mode),
                    flash_freq=b.get("flash_freq", default_flash_freq),
                    needs_boot_mode=b.get("needs_boot_mode", True),
                    port_changes_on_reset=b.get("port_changes_on_reset", False),
                    boot_button=b.get("boot_button", "BOOT"),
                    reset_button=b.get("reset_button", "RESET"),
                    group=b.get("group", "Other"),
                )

        # Release settings
        if "release" in data:
            r = data["release"]
            config.release_parallel = r.get("parallel", False)
            config.keep_releases = r.get("keep_releases", 5)

        # Monitor settings
        if "monitor" in data:
            config.monitor_filters = data["monitor"].get("filters", [])

        if not config.default_board and config.boards:
            config.default_board = list(config.boards.keys())[0]

    except Exception as e:
        print(f"{Colors.RED}[ERROR]{Colors.ENDC} Failed to load config: {e}")

    return config


def parse_simple_toml(path: Path) -> dict:
    """Simple TOML parser for basic config files (fallback)"""
    data = {}
    current_section = data
    current_key = None

    with open(path, "r") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue

            # Section header
            if line.startswith("[") and line.endswith("]"):
                section_path = line[1:-1].split(".")
                current_section = data
                for part in section_path:
                    if part not in current_section:
                        current_section[part] = {}
                    current_section = current_section[part]
                continue

            # Key-value
            if "=" in line:
                key, value = line.split("=", 1)
                key = key.strip()
                value = value.strip()

                # Parse value
                if value.startswith('"') and value.endswith('"'):
                    value = value[1:-1]
                elif value.startswith("'") and value.endswith("'"):
                    value = value[1:-1]
                elif value.lower() == "true":
                    value = True
                elif value.lower() == "false":
                    value = False
                elif value.isdigit():
                    value = int(value)
                elif value.startswith("[") and value.endswith("]"):
                    # Simple array parsing
                    value = [v.strip().strip('"').strip("'") for v in value[1:-1].split(",") if v.strip()]

                current_section[key] = value

    return data


# ============================================================
# Terminal Colors
# ============================================================

class Colors:
    HEADER = '\033[95m'
    BLUE = '\033[94m'
    CYAN = '\033[96m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    ENDC = '\033[0m'
    BOLD = '\033[1m'
    DIM = '\033[2m'

    @staticmethod
    def enable():
        """Enable ANSI colors on Windows"""
        if sys.platform == "win32":
            os.system("")


def c(text: str, color: str) -> str:
    """Wrap text in color codes"""
    return f"{color}{text}{Colors.ENDC}"


# ============================================================
# Global State
# ============================================================

class DevTool:
    """Main DevTool application state"""

    def __init__(self):
        self.script_dir = Path(__file__).parent.resolve()
        self.config_path = self.script_dir / "devtool.toml"
        self.config = load_config(self.config_path)
        self.current_board: Optional[BoardConfig] = None
        self.current_port: Optional[str] = None

        # Set default board
        if self.config.default_board and self.config.default_board in self.config.boards:
            self.current_board = self.config.boards[self.config.default_board]

    # ============================================================
    # Utility Methods
    # ============================================================

    def get_python_path(self) -> str:
        """Get Python executable path (prefer venv)"""
        venv_python = self.script_dir / ".venv" / "Scripts" / "python.exe"
        if venv_python.exists():
            return str(venv_python)
        venv_python_unix = self.script_dir / ".venv" / "bin" / "python"
        if venv_python_unix.exists():
            return str(venv_python_unix)
        return sys.executable

    def get_pio_cmd(self) -> List[str]:
        """Get PlatformIO command"""
        return [self.get_python_path(), "-m", "platformio"]

    def check_pio_installed(self) -> bool:
        """Check if PlatformIO is available"""
        try:
            result = subprocess.run(
                self.get_pio_cmd() + ["--version"],
                capture_output=True, text=True
            )
            return result.returncode == 0
        except Exception:
            return False

    def run_cmd(self, cmd: List[str], capture: bool = False, quiet: bool = False,
                cwd: Path = None) -> Optional[subprocess.CompletedProcess]:
        """Run a command"""
        try:
            kwargs = {"cwd": cwd or self.script_dir}
            if capture or quiet:
                kwargs["capture_output"] = True
                kwargs["text"] = True
            return subprocess.run(cmd, **kwargs)
        except FileNotFoundError:
            print(f"{c('[ERROR]', Colors.RED)} Command not found: {cmd[0]}")
            return None
        except KeyboardInterrupt:
            print(f"\n{c('[CANCELLED]', Colors.YELLOW)} Operation cancelled")
            return None

    def get_version(self) -> str:
        """Get project version"""
        if self.config.version_source == "git":
            result = self.run_cmd(["git", "describe", "--tags", "--dirty"], capture=True)
            if result and result.returncode == 0:
                return result.stdout.strip()
        elif self.config.version_source == "file":
            version_file = self.script_dir / "version.txt"
            if version_file.exists():
                return version_file.read_text().strip()
        return "dev"

    def get_optimal_workers(self) -> int:
        """
        Determine optimal number of parallel build workers based on host hardware.

        Strategy:
        - Get CPU core count (physical or logical)
        - Get available memory
        - Cap workers to avoid overloading system
        - Leave headroom for system responsiveness
        """
        try:
            # Try psutil first (more accurate)
            import psutil
            cpu_count = psutil.cpu_count(logical=True)
            physical_cores = psutil.cpu_count(logical=False)
            available_mem_gb = psutil.virtual_memory().available / (1024**3)
        except ImportError:
            # Fallback to os module
            cpu_count = os.cpu_count() or 4
            physical_cores = cpu_count // 2  # Assume hyperthreading
            # Estimate available memory based on core count (assume ~4GB per core typical)
            available_mem_gb = max(8, physical_cores * 2)

        # PlatformIO builds use ~2-4GB RAM per job
        # and are CPU-intensive during compilation
        mem_based_limit = max(1, int(available_mem_gb / 3))

        # Use physical cores as base, leave 1-2 for system
        if physical_cores <= 2:
            # Weak CPU (dual-core): single-threaded
            cpu_based_limit = 1
        elif physical_cores <= 4:
            # Mid-range (quad-core): 2-3 workers
            cpu_based_limit = physical_cores - 1
        else:
            # Beefy CPU (6+ cores): scale up but cap at 8
            cpu_based_limit = min(physical_cores - 2, 8)

        optimal = min(cpu_based_limit, mem_based_limit)

        return max(1, optimal)  # At least 1 worker

    def get_system_info(self) -> str:
        """Get brief system info string for build output"""
        try:
            import psutil
            cores = psutil.cpu_count(logical=False)
            threads = psutil.cpu_count(logical=True)
            mem_gb = psutil.virtual_memory().total / (1024**3)
            return f"{cores}C/{threads}T, {mem_gb:.0f}GB RAM"
        except ImportError:
            cores = os.cpu_count() or 4
            return f"{cores} threads"

    def is_git_dirty(self) -> bool:
        """Check for uncommitted changes"""
        result = self.run_cmd(["git", "status", "--porcelain"], capture=True)
        return bool(result and result.stdout.strip())

    def get_firmware_dir(self) -> Path:
        """Get firmware output directory"""
        return self.script_dir / self.config.firmware_dir

    # ============================================================
    # Serial Port Handling
    # ============================================================

    def list_serial_ports(self) -> List[Tuple[str, str]]:
        """List available serial ports"""
        result = self.run_cmd(self.get_pio_cmd() + ["device", "list", "--serial"], capture=True)
        if not result or result.returncode != 0:
            return []

        ports = []
        current_port = None
        current_desc = ""

        for line in result.stdout.split('\n'):
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

    def is_usb_port(self, desc: str) -> bool:
        """Check if port is likely USB serial"""
        desc_upper = desc.upper()
        if "BLUETOOTH" in desc_upper:
            return False
        return any(x in desc_upper for x in ["USB", "CP210", "CH340", "JTAG", "ESP", "FTDI", "SILICON"])

    def filter_usb_ports(self, ports: List[Tuple[str, str]]) -> List[Tuple[str, str]]:
        """Filter to USB serial ports only"""
        return [(p, d) for p, d in ports if self.is_usb_port(d)]

    def select_port(self, for_flashing: bool = False) -> Optional[str]:
        """Interactive port selection"""
        print(f"\n{c('[INFO]', Colors.BLUE)} Scanning serial ports...")
        ports = self.list_serial_ports()

        if not ports:
            if for_flashing and self.current_board and self.current_board.needs_boot_mode:
                self.print_bootloader_instructions()
                input(f"\n{c('[?]', Colors.CYAN)} Press ENTER after entering download mode...")
                ports = self.list_serial_ports()

            if not ports:
                print(f"{c('[WARN]', Colors.YELLOW)} No serial ports found!")
                manual = input(f"{c('[?]', Colors.CYAN)} Enter port manually (or Enter to cancel): ").strip()
                return manual if manual else None

        usb_ports = self.filter_usb_ports(ports)
        display_ports = usb_ports if usb_ports else ports

        print(f"\n{c('Available Ports:', Colors.GREEN)} [B=Back]")
        print("-" * 55)
        for i, (port, desc) in enumerate(display_ports, 1):
            color_code = Colors.GREEN if self.is_usb_port(desc) else Colors.DIM
            print(f"  {c(f'[{i}]', Colors.CYAN)} {c(port, color_code)} - {desc}")
        print("-" * 55)

        while True:
            choice = input(f"\n{c('[?]', Colors.CYAN)} Select port (1-{len(display_ports)}, B=back): ").strip()

            if not choice or choice.lower() in ('b', 'q'):
                return self.current_port  # Return current (unchanged)
            if choice.isdigit():
                idx = int(choice) - 1
                if 0 <= idx < len(display_ports):
                    self.current_port = display_ports[idx][0]
                    return self.current_port
            else:
                # Allow direct port name entry
                port_name = choice.upper() if choice.upper().startswith("COM") else choice
                self.current_port = port_name
                return self.current_port
            print(f"{c('[ERROR]', Colors.RED)} Invalid selection!")

    def find_new_port(self, old_port: str) -> Optional[str]:
        """Handle COM port change after reset (ESP32-S3)"""
        if not self.current_board or not self.current_board.port_changes_on_reset:
            return old_port

        print(f"\n{c('[INFO]', Colors.BLUE)} Waiting for device to enumerate...")
        time.sleep(3)

        ports = self.list_serial_ports()
        usb_ports = self.filter_usb_ports(ports)
        usb_names = [p[0] for p in usb_ports]

        if old_port in usb_names:
            return old_port

        print(f"{c('[INFO]', Colors.YELLOW)} Port {old_port} changed (common for ESP32-S3)")

        if len(usb_ports) == 1:
            new_port = usb_ports[0][0]
            print(f"{c('[OK]', Colors.GREEN)} Found device on {new_port}")
            self.current_port = new_port
            return new_port

        if usb_ports:
            return self.select_port()

        return None

    # ============================================================
    # Board Selection
    # ============================================================

    def get_board_groups(self) -> Dict[str, List[BoardConfig]]:
        """Group boards by their group field"""
        groups: Dict[str, List[BoardConfig]] = {}
        for board in self.config.boards.values():
            if board.group not in groups:
                groups[board.group] = []
            groups[board.group].append(board)
        return groups

    def select_board(self) -> Optional[BoardConfig]:
        """Interactive board selection"""
        groups = self.get_board_groups()

        print(f"\n{c('Select Board:', Colors.GREEN)} [B=Back]")
        print("=" * 60)

        board_list = []
        idx = 1

        for group_name, boards in groups.items():
            print(f"\n  {c(group_name, Colors.CYAN)}")
            print(f"  {'-' * 40}")

            for board in boards:
                board_list.append(board)
                marker = " *" if board == self.current_board else ""
                print(f"    {c(f'[{idx}]', Colors.CYAN)} {board.name}{c(marker, Colors.GREEN)}")
                print(f"        {c(board.description, Colors.DIM)}")
                idx += 1

        print("\n" + "=" * 60)

        while True:
            prompt = f"Select (1-{len(board_list)}, B=back)"
            if self.current_board:
                prompt += f" or Enter=[{self.current_board.key}]"
            choice = input(f"\n{c('[?]', Colors.CYAN)} {prompt}: ").strip()

            if not choice and self.current_board:
                return self.current_board
            if choice.lower() in ('q', 'b'):
                return self.current_board  # Return current (unchanged) instead of None
            if choice.isdigit():
                idx = int(choice) - 1
                if 0 <= idx < len(board_list):
                    self.current_board = board_list[idx]
                    print(f"\n{c('[OK]', Colors.GREEN)} Selected: {self.current_board.name}")
                    return self.current_board

            # Try by key
            if choice in self.config.boards:
                self.current_board = self.config.boards[choice]
                return self.current_board

            print(f"{c('[ERROR]', Colors.RED)} Invalid selection!")

    # ============================================================
    # Build, Flash, Monitor
    # ============================================================

    def build(self, board: BoardConfig = None, verbose: bool = True) -> bool:
        """Build firmware for a board"""
        board = board or self.current_board
        if not board:
            print(f"{c('[ERROR]', Colors.RED)} No board selected!")
            return False

        if not self.check_pio_installed():
            print(f"{c('[ERROR]', Colors.RED)} PlatformIO not found!")
            return False

        version = self.get_version()

        if verbose:
            print(f"\n{c('=' * 60, Colors.CYAN)}")
            print(f"{c(f' Building {self.config.name}', Colors.BOLD)}")
            print(f"{c('=' * 60, Colors.CYAN)}")
            print(f"  Board:   {board.name}")
            print(f"  Env:     {board.env}")
            print(f"  Version: {version}")
            print(f"{c('=' * 60, Colors.CYAN)}\n")

        result = self.run_cmd(self.get_pio_cmd() + ["run", "-e", board.env],
                             capture=not verbose, quiet=not verbose)

        if result and result.returncode == 0:
            if verbose:
                print(f"\n{c('[SUCCESS]', Colors.GREEN)} Build completed!")
            return True
        else:
            if verbose:
                print(f"\n{c('[ERROR]', Colors.RED)} Build failed!")
            return False

    def get_firmware_path(self, board: BoardConfig) -> Optional[Path]:
        """Find firmware file for a board"""
        # Check release firmware first (try friendly key name, then env name)
        version = self.get_version()
        fw_dir = self.get_firmware_dir() / version

        # Try friendly name first (e.g., cyd-1usb_factory.bin)
        factory = fw_dir / f"{board.key}_factory.bin"
        if factory.exists():
            return factory

        # Fallback to env name for backwards compatibility (e.g., esp32-2432s028_factory.bin)
        factory = fw_dir / f"{board.env}_factory.bin"
        if factory.exists():
            return factory

        # Check latest release (try both naming schemes)
        if self.get_firmware_dir().exists():
            for version_dir in sorted(self.get_firmware_dir().iterdir(), reverse=True):
                if version_dir.is_dir():
                    # Try friendly name first
                    factory = version_dir / f"{board.key}_factory.bin"
                    if factory.exists():
                        return factory
                    # Fallback to env name
                    factory = version_dir / f"{board.env}_factory.bin"
                    if factory.exists():
                        return factory

        # Check build directory (raw PIO output, no friendly names)
        build_fw = self.script_dir / ".pio" / "build" / board.env / "firmware.bin"
        if build_fw.exists():
            return build_fw

        return None

    def flash(self, board: BoardConfig = None, port: str = None,
              interactive: bool = True) -> bool:
        """Flash firmware to device"""
        board = board or self.current_board
        if not board:
            print(f"{c('[ERROR]', Colors.RED)} No board selected!")
            return False

        firmware = self.get_firmware_path(board)
        if not firmware:
            print(f"{c('[ERROR]', Colors.RED)} No firmware found! Build first.")
            return False

        port = port or self.current_port
        if not port:
            port = self.select_port(for_flashing=True)
            if not port:
                return False

        print(f"\n{c('=' * 60, Colors.CYAN)}")
        print(f"{c(' Flashing Firmware', Colors.BOLD)}")
        print(f"{c('=' * 60, Colors.CYAN)}")
        print(f"  Board:    {board.name}")
        print(f"  Port:     {port}")
        print(f"  Firmware: {firmware.name}")
        print(f"  Size:     {firmware.stat().st_size:,} bytes")
        print(f"{c('=' * 60, Colors.CYAN)}")

        if interactive and board.needs_boot_mode:
            self.print_bootloader_instructions()
            input(f"\n{c('[?]', Colors.CYAN)} Press ENTER when in download mode...")

        flash_addr = "0x0" if "factory" in firmware.name else "0x10000"

        cmd = [
            self.get_python_path(), "-m", "esptool",
            "--chip", board.chip,
            "--port", port,
            "--baud", str(board.flash_baud),
            "--before", "default-reset",
            "--after", "hard-reset",
            "write-flash",
            "-z",
            "--flash-mode", board.flash_mode,
            "--flash-freq", board.flash_freq,
            "--flash-size", "detect",
            flash_addr, str(firmware)
        ]

        result = self.run_cmd(cmd)

        if result and result.returncode == 0:
            print(f"\n{c('[SUCCESS]', Colors.GREEN)} Flash completed!")
            self.current_port = port
            return True
        else:
            print(f"\n{c('[ERROR]', Colors.RED)} Flash failed!")
            return False

    def monitor(self, board: BoardConfig = None, port: str = None,
                after_flash: bool = False) -> None:
        """Open serial monitor"""
        board = board or self.current_board
        if not board:
            print(f"{c('[ERROR]', Colors.RED)} No board selected!")
            return

        port = port or self.current_port
        if not port:
            port = self.select_port()
            if not port:
                return

        if after_flash:
            self.print_reset_instructions()
            input(f"\n{c('[?]', Colors.CYAN)} Press ENTER after pressing {board.reset_button}...")
            port = self.find_new_port(port) or port

        print(f"\n{c('=' * 60, Colors.CYAN)}")
        print(f"{c(' Serial Monitor', Colors.BOLD)}")
        print(f"{c('=' * 60, Colors.CYAN)}")
        print(f"  Port: {port}")
        print(f"  Baud: {board.monitor_baud}")
        print(f"  Exit: Ctrl+C")
        print(f"{c('=' * 60, Colors.CYAN)}\n")

        cmd = self.get_pio_cmd() + ["device", "monitor", "-b", str(board.monitor_baud), "-p", port]

        # Add filters
        for f in self.config.monitor_filters:
            cmd.extend(["-f", f])

        self.run_cmd(cmd)

    def all_in_one(self, board: BoardConfig = None, port: str = None) -> bool:
        """Build, flash, and monitor"""
        board = board or self.current_board
        if not board:
            board = self.select_board()
            if not board:
                return False

        if not self.build(board):
            return False

        if not self.flash(board, port):
            return False

        self.monitor(board, self.current_port, after_flash=True)
        return True

    # ============================================================
    # Release Management
    # ============================================================

    def build_release(self, board_keys: List[str] = None,
                      parallel: bool = None) -> Dict[str, bool]:
        """Build firmware for multiple boards"""
        if parallel is None:
            parallel = self.config.release_parallel

        boards = []
        if board_keys:
            for key in board_keys:
                if key in self.config.boards:
                    boards.append(self.config.boards[key])
        else:
            boards = list(self.config.boards.values())

        if not boards:
            print(f"{c('[ERROR]', Colors.RED)} No boards to build!")
            return {}

        version = self.get_version()
        dirty = self.is_git_dirty()

        print(f"\n{c('=' * 60, Colors.CYAN)}")
        print(f"{c(f' {self.config.name} Release Build', Colors.BOLD)}")
        print(f"{c('=' * 60, Colors.CYAN)}")

        if dirty:
            print(f"  Version: {c(version, Colors.YELLOW)} {c('(dirty)', Colors.RED)}")
        else:
            print(f"  Version: {c(version, Colors.GREEN)}")

        print(f"  Boards:  {len(boards)}")
        print(f"  Mode:    {'Parallel' if parallel else 'Sequential'}")
        if parallel:
            optimal_workers = self.get_optimal_workers()
            actual_workers = min(optimal_workers, len(boards))
            print(f"  System:  {self.get_system_info()}")
            print(f"  Workers: {actual_workers} (optimal: {optimal_workers})")
        print(f"  Output:  {self.config.firmware_dir}/{version}/")
        print(f"{c('=' * 60, Colors.CYAN)}")

        results = {}
        start_time = time.time()

        if parallel:
            with ThreadPoolExecutor(max_workers=actual_workers) as executor:
                futures = {executor.submit(self.build, b, False): b for b in boards}
                for future in as_completed(futures):
                    board = futures[future]
                    success = future.result()
                    results[board.key] = success
                    status = c('[OK]', Colors.GREEN) if success else c('[FAIL]', Colors.RED)
                    print(f"  {status} {board.name}")
        else:
            for i, board in enumerate(boards, 1):
                print(f"\n{c(f'[{i}/{len(boards)}]', Colors.CYAN)} Building {board.name}...")
                success = self.build(board, verbose=True)
                results[board.key] = success

        elapsed = time.time() - start_time
        success_count = sum(1 for v in results.values() if v)
        fail_count = len(results) - success_count

        print(f"\n{c('=' * 60, Colors.CYAN)}")
        print(f"{c(' Build Summary', Colors.BOLD)}")
        print(f"{c('=' * 60, Colors.CYAN)}")
        print(f"  Time:    {elapsed:.1f}s")
        print(f"  Success: {c(str(success_count), Colors.GREEN)}")
        if fail_count:
            print(f"  Failed:  {c(str(fail_count), Colors.RED)}")
        print(f"{c('=' * 60, Colors.CYAN)}")

        return results

    def create_git_tag(self, version: str, force: bool = False) -> bool:
        """Create a git tag for release"""
        if not version.startswith("v"):
            version = f"v{version}"

        # Check if tag exists
        result = self.run_cmd(["git", "tag", "-l", version], capture=True)
        if result and result.stdout.strip() == version:
            if not force:
                print(f"{c('[INFO]', Colors.YELLOW)} Tag {version} already exists")
                return True
            self.run_cmd(["git", "tag", "-d", version], capture=True)

        result = self.run_cmd(
            ["git", "tag", "-a", version, "-m", f"Release {version}"],
            capture=True
        )

        if result and result.returncode == 0:
            print(f"{c('[OK]', Colors.GREEN)} Created git tag: {version}")
            return True
        else:
            print(f"{c('[ERROR]', Colors.RED)} Failed to create tag")
            return False

    def list_releases(self) -> List[Tuple[str, int, int]]:
        """List firmware releases"""
        fw_dir = self.get_firmware_dir()
        if not fw_dir.exists():
            return []

        releases = []
        for version_dir in sorted(fw_dir.iterdir(), reverse=True):
            if version_dir.is_dir() and not version_dir.name.startswith('.'):
                files = list(version_dir.glob("*.bin"))
                total_size = sum(f.stat().st_size for f in files)
                releases.append((version_dir.name, len(files), total_size))

        return releases

    def prune_releases(self, keep: int = None) -> int:
        """Remove old releases"""
        keep = keep or self.config.keep_releases
        releases = self.list_releases()

        if len(releases) <= keep:
            return 0

        removed = 0
        fw_dir = self.get_firmware_dir()

        for version, _, _ in releases[keep:]:
            version_dir = fw_dir / version
            if version_dir.exists():
                try:
                    shutil.rmtree(version_dir)
                    print(f"  {c('[REMOVED]', Colors.YELLOW)} {version}")
                    removed += 1
                except Exception as e:
                    print(f"  {c('[ERROR]', Colors.RED)} {version}: {e}")

        return removed

    def clean(self, board: BoardConfig = None) -> None:
        """Clean build files"""
        if board:
            print(f"{c('[CLEAN]', Colors.YELLOW)} {board.name}")
            self.run_cmd(self.get_pio_cmd() + ["run", "-e", board.env, "-t", "clean"], quiet=True)
        else:
            print(f"{c('[CLEAN]', Colors.YELLOW)} All builds")
            self.run_cmd(self.get_pio_cmd() + ["run", "-t", "clean"], quiet=True)
        print(f"{c('[OK]', Colors.GREEN)} Clean complete")

    # ============================================================
    # UI / Instructions
    # ============================================================

    def print_banner(self):
        """Print application banner"""
        board_info = f"Board: {self.current_board.name}" if self.current_board else "No board selected"
        version = self.get_version()

        banner = f"""
{c('=' * 60, Colors.CYAN)}
  {c(self.config.name, Colors.BOLD)} DevTool v{DEVTOOL_VERSION}
{c('=' * 60, Colors.CYAN)}
  {board_info}
  Version: {version}
{c('=' * 60, Colors.CYAN)}
"""
        print(banner)

    def print_bootloader_instructions(self):
        """Print bootloader mode instructions"""
        board = self.current_board
        if not board:
            return

        print(f"\n{c('=' * 55, Colors.YELLOW)}")
        print(f"{c(f' {board.chip.upper()} DOWNLOAD MODE', Colors.BOLD)}")
        print(f"{c('=' * 55, Colors.YELLOW)}")

        if board.chip == "esp32s3":
            print(f"""
  1. HOLD the {board.boot_button} button
  2. Press and release {board.reset_button}
  3. Release {board.boot_button}

  Display will be blank - this is normal!
""")
        else:
            print(f"""
  The ESP32 usually auto-resets into download mode.

  If flash fails, try:
    Hold {board.boot_button}, press {board.reset_button}, release {board.boot_button}
""")
        print(f"{c('=' * 55, Colors.YELLOW)}")

    def print_reset_instructions(self):
        """Print reset instructions after flash"""
        board = self.current_board
        if not board:
            return

        print(f"\n{c('=' * 55, Colors.GREEN)}")
        print(f"{c(' FLASH COMPLETE', Colors.BOLD)}")
        print(f"{c('=' * 55, Colors.GREEN)}")
        print(f"""
  Press the {board.reset_button} button to start the new firmware.
""")
        print(f"{c('=' * 55, Colors.GREEN)}")

    # ============================================================
    # Interactive Menu
    # ============================================================

    def browse_firmware_file(self) -> Optional[Path]:
        """Open file dialog to select firmware file"""
        try:
            import tkinter as tk
            from tkinter import filedialog

            # Hide the root window
            root = tk.Tk()
            root.withdraw()
            root.attributes('-topmost', True)

            # Open file dialog
            file_path = filedialog.askopenfilename(
                title="Select Firmware File",
                initialdir=str(self.get_firmware_dir()),
                filetypes=[
                    ("Binary files", "*.bin"),
                    ("All files", "*.*")
                ]
            )

            root.destroy()

            if file_path:
                return Path(file_path)
            return None

        except ImportError:
            print(f"{c('[ERROR]', Colors.RED)} tkinter not available for file browser")
            # Fallback to manual path entry
            path = input(f"{c('[?]', Colors.CYAN)} Enter firmware path: ").strip()
            if path and Path(path).exists():
                return Path(path)
            return None

    def flash_custom_firmware(self, firmware_path: Path = None) -> bool:
        """Flash a custom firmware file"""
        if not firmware_path:
            firmware_path = self.browse_firmware_file()

        if not firmware_path or not firmware_path.exists():
            print(f"{c('[ERROR]', Colors.RED)} No firmware file selected")
            return False

        board = self.current_board
        if not board:
            board = self.select_board()
            if not board:
                return False

        port = self.current_port
        if not port:
            port = self.select_port(for_flashing=True)
            if not port:
                return False

        print(f"\n{c('=' * 60, Colors.CYAN)}")
        print(f"{c(' Flashing Custom Firmware', Colors.BOLD)}")
        print(f"{c('=' * 60, Colors.CYAN)}")
        print(f"  Board:    {board.name}")
        print(f"  Port:     {port}")
        print(f"  File:     {firmware_path.name}")
        print(f"  Size:     {firmware_path.stat().st_size:,} bytes")
        print(f"{c('=' * 60, Colors.CYAN)}")

        if board.needs_boot_mode:
            self.print_bootloader_instructions()
            input(f"\n{c('[?]', Colors.CYAN)} Press ENTER when in download mode...")

        # Determine flash address
        flash_addr = "0x0" if "factory" in firmware_path.name else "0x10000"

        cmd = [
            self.get_python_path(), "-m", "esptool",
            "--chip", board.chip,
            "--port", port,
            "--baud", str(board.flash_baud),
            "--before", "default-reset",
            "--after", "hard-reset",
            "write-flash",
            "-z",
            "--flash-mode", board.flash_mode,
            "--flash-freq", board.flash_freq,
            "--flash-size", "detect",
            flash_addr, str(firmware_path)
        ]

        result = self.run_cmd(cmd)

        if result and result.returncode == 0:
            print(f"\n{c('[SUCCESS]', Colors.GREEN)} Flash completed!")
            self.current_port = port
            return True
        else:
            print(f"\n{c('[ERROR]', Colors.RED)} Flash failed!")
            return False

    def interactive_menu(self):
        """Main interactive menu"""
        while True:
            board_name = self.current_board.name if self.current_board else "None"
            port_name = self.current_port or "Auto"

            print(f"\n{c('Main Menu', Colors.GREEN)} [{board_name}] [{port_name}]")
            print("-" * 55)
            print(f"  {c('[1]', Colors.CYAN)} Build")
            print(f"  {c('[2]', Colors.CYAN)} Flash")
            print(f"  {c('[3]', Colors.CYAN)} Monitor")
            print(f"  {c('[4]', Colors.CYAN)} Build + Flash + Monitor")
            print(f"  {c('[5]', Colors.CYAN)} Release build (all boards)")
            print(f"  {c('[6]', Colors.CYAN)} Select board")
            print(f"  {c('[7]', Colors.CYAN)} Select port")
            print(f"  {c('[8]', Colors.CYAN)} Clean build")
            print(f"  {c('[9]', Colors.CYAN)} List releases")
            print(f"  {c('[F]', Colors.CYAN)} Flash custom firmware file")
            print(f"  {c('[0]', Colors.CYAN)} Exit")
            print("-" * 55)

            choice = input(f"\n{c('[?]', Colors.CYAN)} Select: ").strip().lower()

            if choice == "1":
                if not self.current_board:
                    self.select_board()
                if self.current_board:
                    self.build()

            elif choice == "2":
                if not self.current_board:
                    self.select_board()
                if self.current_board:
                    self.flash()

            elif choice == "3":
                if not self.current_board:
                    self.select_board()
                if self.current_board:
                    self.monitor()

            elif choice == "4":
                if not self.current_board:
                    self.select_board()
                if self.current_board:
                    self.all_in_one()

            elif choice == "5":
                self.build_release()

            elif choice == "6":
                self.select_board()

            elif choice == "7":
                self.select_port()

            elif choice == "8":
                self.clean(self.current_board)

            elif choice == "9":
                releases = self.list_releases()
                print(f"\n{c('Releases:', Colors.GREEN)}")
                print("-" * 55)
                if releases:
                    for ver, count, size in releases:
                        mb = size / (1024 * 1024)
                        print(f"  {c(ver, Colors.CYAN):20} {count} files  {mb:.2f} MB")
                else:
                    print(f"  {c('No releases found', Colors.DIM)}")
                print("-" * 55)

            elif choice == "f":
                self.flash_custom_firmware()

            elif choice == "0":
                print(f"\n{c('Goodbye!', Colors.CYAN)}\n")
                break

            else:
                print(f"{c('[ERROR]', Colors.RED)} Invalid option!")


# ============================================================
# CLI Entry Point
# ============================================================

def main():
    Colors.enable()

    parser = argparse.ArgumentParser(
        description=f"ESP32 DevTool - Universal Build/Flash/Monitor Suite",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Commands:
  (none)        Interactive menu mode
  build         Build firmware
  flash         Flash firmware
  monitor       Serial monitor
  all           Build + Flash + Monitor
  release       Build all boards for release
  clean         Clean build files
  list          List boards and releases

Examples:
  python devtool.py                        # Interactive mode
  python devtool.py build -b cyd-1usb      # Build specific board
  python devtool.py all -b cyd-2usb        # Full cycle for board
  python devtool.py release                # Build all boards
  python devtool.py release -v v2.9.0      # Tag and build release
"""
    )

    parser.add_argument("command", nargs="?", default="menu",
                       choices=["menu", "build", "flash", "monitor", "all", "release", "clean", "list"],
                       help="Command to run (default: menu)")
    parser.add_argument("-b", "--board", metavar="KEY", help="Board key (see 'list')")
    parser.add_argument("-p", "--port", metavar="PORT", help="Serial port")
    parser.add_argument("-v", "--version", metavar="TAG", help="Create git tag for release")
    parser.add_argument("--parallel", action="store_true", help="Parallel release builds")
    parser.add_argument("--prune", action="store_true", help="Remove old releases")
    parser.add_argument("--keep", type=int, default=5, help="Releases to keep when pruning")
    parser.add_argument("-y", "--yes", action="store_true", help="Skip confirmations")

    args = parser.parse_args()

    # Initialize
    tool = DevTool()

    # Select board if specified
    if args.board:
        if args.board in tool.config.boards:
            tool.current_board = tool.config.boards[args.board]
        else:
            print(f"{c('[ERROR]', Colors.RED)} Unknown board: {args.board}")
            print(f"Available: {', '.join(tool.config.boards.keys())}")
            sys.exit(1)

    if args.port:
        tool.current_port = args.port

    # Handle commands
    if args.command == "menu":
        tool.print_banner()
        if not tool.check_pio_installed():
            print(f"{c('[ERROR]', Colors.RED)} PlatformIO not found!")
            print("Install: pip install platformio")
            sys.exit(1)
        tool.interactive_menu()

    elif args.command == "build":
        if not tool.current_board:
            tool.select_board()
        if tool.current_board:
            sys.exit(0 if tool.build() else 1)

    elif args.command == "flash":
        if not tool.current_board:
            tool.select_board()
        if tool.current_board:
            sys.exit(0 if tool.flash(interactive=not args.yes) else 1)

    elif args.command == "monitor":
        if not tool.current_board:
            tool.select_board()
        if tool.current_board:
            tool.monitor()

    elif args.command == "all":
        if not tool.current_board:
            tool.select_board()
        if tool.current_board:
            sys.exit(0 if tool.all_in_one() else 1)

    elif args.command == "release":
        if args.version:
            if tool.is_git_dirty() and not args.yes:
                print(f"{c('[WARN]', Colors.YELLOW)} Uncommitted changes detected!")
                resp = input(f"{c('[?]', Colors.CYAN)} Continue? (y/N): ").strip().lower()
                if resp != 'y':
                    sys.exit(0)
            tool.create_git_tag(args.version)

        results = tool.build_release(parallel=args.parallel)

        if args.prune:
            tool.prune_releases(args.keep)

        sys.exit(0 if all(results.values()) else 1)

    elif args.command == "clean":
        tool.clean(tool.current_board)

    elif args.command == "list":
        print(f"\n{c('Boards:', Colors.GREEN)}")
        print("-" * 55)
        for key, board in tool.config.boards.items():
            print(f"  {c(key, Colors.CYAN):18} {board.name}")
        print("-" * 55)

        releases = tool.list_releases()
        print(f"\n{c('Releases:', Colors.GREEN)}")
        print("-" * 55)
        if releases:
            for ver, count, size in releases:
                mb = size / (1024 * 1024)
                print(f"  {c(ver, Colors.CYAN):18} {count} files  {mb:.2f} MB")
        else:
            print(f"  {c('No releases', Colors.DIM)}")
        print("-" * 55)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print(f"\n\n{c('Cancelled.', Colors.CYAN)}\n")
        sys.exit(0)
