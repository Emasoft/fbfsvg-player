#!/usr/bin/env python3
"""
fbfsvg-player Release Automation Script

Automates the complete release process for all platforms:
1. Pre-flight checks (dependencies, Skia libraries, disk space)
2. Version validation and tagging
3. Building for macOS (ARM64 + x64 in parallel), Linux, Windows
4. Creating distribution packages
5. Draft release creation on GitHub
6. Asset upload and checksum generation
7. Validation of all assets
8. Interactive publish confirmation
9. Publishing the release
10. Updating package manifests (Homebrew, Scoop)

Usage:
    python3 scripts/release.py --version 0.2.0
    python3 scripts/release.py --version 0.2.0 --dry-run
    python3 scripts/release.py --version 0.2.0 --skip-build
    python3 scripts/release.py --version 0.2.0 --platform macos
    python3 scripts/release.py --version 0.2.0 --no-confirm
    python3 scripts/release.py --version 0.2.0 --ci  # GitHub Actions mode
"""

import argparse
import concurrent.futures
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any, Optional


# =============================================================================
# Configuration
# =============================================================================

PROJECT_NAME = "fbfsvg-player"
GITHUB_REPO = "Emasoft/fbfsvg-player"
LICENSE = "BSD-3-Clause"

# Minimum requirements
MIN_DISK_SPACE_GB = 2.0
NETWORK_RETRY_COUNT = 3
NETWORK_RETRY_DELAY = 5  # seconds

# Platform configurations
# Note: macOS has two separate architectures (arm64 and x64) with separate builds
PLATFORMS: dict[str, dict[str, Any]] = {
    "macos-arm64": {
        "os": "Darwin",
        "arch": "arm64",
        "build_script": "scripts/build-macos-arch.sh",
        "build_args": ["arm64"],
        "binary_name": "fbfsvg-player-macos-arm64",
        "package_format": "tar.gz",
        "asset_name": "{name}-{version}-macos-arm64.tar.gz",
        "display_name": "macOS (Apple Silicon)",
        "skia_dir": "release-macos-arm64",
        "lipo_arch": "arm64",
    },
    "macos-x64": {
        "os": "Darwin",
        "arch": "x86_64",
        "build_script": "scripts/build-macos-arch.sh",
        "build_args": ["x64"],
        "binary_name": "fbfsvg-player-macos-x64",
        "package_format": "tar.gz",
        "asset_name": "{name}-{version}-macos-x64.tar.gz",
        "display_name": "macOS (Intel)",
        "skia_dir": "release-macos-x64",
        "lipo_arch": "x86_64",
        "rosetta_prefix": ["arch", "-x86_64"],  # Cross-compile on ARM64
    },
    "linux": {
        "os": "Linux",
        "arch": "x86_64",
        "build_script": "scripts/build-linux.sh",
        "build_args": [],
        "binary_name": "fbfsvg-player",
        "package_formats": ["tar.gz", "deb", "appimage"],
        "asset_names": {
            "tar.gz": "{name}-{version}-linux-x64.tar.gz",
            "deb": "{name}_{version}_amd64.deb",
            "appimage": "{name}-{version}-x86_64.AppImage",
        },
        "display_name": "Linux (x64)",
        "skia_dir": "release-linux",
    },
    "windows": {
        "os": "Windows",
        "arch": "x86_64",
        "build_script": "scripts/build-windows.bat",
        "build_args": [],
        "binary_name": "fbfsvg-player.exe",
        "package_format": "zip",
        "asset_name": "{name}-{version}-windows-x64.zip",
        "display_name": "Windows (x64)",
        "skia_dir": "release-windows",
    },
}

# Platform aliases for convenience
PLATFORM_ALIASES = {
    "macos": ["macos-arm64", "macos-x64"],  # "macos" means both architectures
}


# =============================================================================
# Logging
# =============================================================================

LOG_FILE: Optional[Path] = None
CI_MODE: bool = (
    False  # Set to True to suppress colors and enable machine-readable output
)


def set_ci_mode(enabled: bool) -> None:
    """Enable or disable CI mode globally."""
    global CI_MODE
    CI_MODE = enabled


def init_logging(log_dir: Path) -> None:
    """Initialize log file."""
    global LOG_FILE
    log_dir.mkdir(parents=True, exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    LOG_FILE = log_dir / f"release_{timestamp}.log"
    LOG_FILE.touch()


def log(msg: str, level: str = "INFO") -> None:
    """Print a log message with timestamp and level."""
    timestamp = datetime.now().strftime("%H:%M:%S")

    # Suppress colors in CI mode for machine-readable output
    if CI_MODE:
        colors: dict[str, str] = {
            "INFO": "",
            "SUCCESS": "",
            "WARNING": "",
            "ERROR": "",
            "STEP": "",
            "DEBUG": "",
        }
        reset = ""
    else:
        colors = {
            "INFO": "\033[0;36m",  # Cyan
            "SUCCESS": "\033[0;32m",  # Green
            "WARNING": "\033[0;33m",  # Yellow
            "ERROR": "\033[0;31m",  # Red
            "STEP": "\033[1;35m",  # Bold Magenta
            "DEBUG": "\033[0;90m",  # Gray
        }
        reset = "\033[0m"
    color = colors.get(level, "")

    # Console output
    print(f"{color}[{timestamp}] [{level}] {msg}{reset}")

    # File output (no colors)
    if LOG_FILE:
        with open(LOG_FILE, "a") as f:
            f.write(f"[{timestamp}] [{level}] {msg}\n")


# =============================================================================
# Utility Functions
# =============================================================================


def run_cmd(
    cmd: list[str],
    cwd: Optional[Path] = None,
    capture: bool = False,
    check: bool = True,
    dry_run: bool = False,
    retry: int = 0,
    retry_delay: int = NETWORK_RETRY_DELAY,
    timeout: Optional[int] = None,
    env: Optional[dict[str, str]] = None,
) -> subprocess.CompletedProcess[str]:
    """Run a shell command with logging and optional retry."""
    cmd_str = " ".join(str(c) for c in cmd)
    if dry_run:
        log(f"[DRY-RUN] Would execute: {cmd_str}", "WARNING")
        return subprocess.CompletedProcess(cmd, 0, stdout="", stderr="")

    log(f"Executing: {cmd_str}", "DEBUG")

    # Merge custom env with current environment if provided
    run_env = None
    if env:
        run_env = os.environ.copy()
        run_env.update(env)

    last_error = None
    for attempt in range(retry + 1):
        try:
            result = subprocess.run(
                cmd,
                cwd=cwd,
                capture_output=capture,
                text=True,
                check=check,
                timeout=timeout,
                env=run_env,
            )
            return result
        except subprocess.CalledProcessError as e:
            last_error = e
            if attempt < retry:
                log(
                    f"Command failed (attempt {attempt + 1}/{retry + 1}), retrying in {retry_delay}s...",
                    "WARNING",
                )
                time.sleep(retry_delay)
            else:
                raise
        except subprocess.TimeoutExpired:
            log(f"Command timed out after {timeout}s", "ERROR")
            raise

    # Should never reach here - raise RuntimeError as fallback
    assert last_error is not None, "Unexpected execution path in run_cmd"
    raise last_error


def sha256_file(filepath: Path) -> str:
    """Calculate SHA256 hash of a file."""
    sha256 = hashlib.sha256()
    with open(filepath, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            sha256.update(chunk)
    return sha256.hexdigest()


def get_file_size(filepath: Path) -> str:
    """Get human-readable file size."""
    size: float = filepath.stat().st_size
    for unit in ["B", "KB", "MB", "GB"]:
        if size < 1024:
            return f"{size:.1f} {unit}"
        size /= 1024
    return f"{size:.1f} TB"


def validate_version(version: str) -> bool:
    """Validate semantic version format."""
    pattern = r"^\d+\.\d+\.\d+(-[a-zA-Z0-9]+(\.[a-zA-Z0-9]+)*)?$"
    return bool(re.match(pattern, version))


def get_current_platform() -> str:
    """Detect the current platform."""
    system = platform.system()
    if system == "Darwin":
        return "macos"
    elif system == "Linux":
        return "linux"
    elif system == "Windows":
        return "windows"
    else:
        raise RuntimeError(f"Unsupported platform: {system}")


def get_current_arch() -> str:
    """Detect the current CPU architecture."""
    machine = platform.machine()
    if machine in ("arm64", "aarch64"):
        return "arm64"
    elif machine in ("x86_64", "AMD64"):
        return "x86_64"
    else:
        return machine


def get_disk_space_gb(path: Path) -> float:
    """Get available disk space in GB."""
    stat = shutil.disk_usage(path)
    return stat.free / (1024**3)


def verify_binary_architecture(binary_path: Path, expected_arch: str) -> bool:
    """Verify binary architecture using lipo (macOS) or file command."""
    if not binary_path.exists():
        return False

    try:
        if get_current_platform() == "macos":
            result = subprocess.run(
                ["lipo", "-info", str(binary_path)],
                capture_output=True,
                text=True,
                check=True,
            )
            return expected_arch in result.stdout
        else:
            result = subprocess.run(
                ["file", str(binary_path)], capture_output=True, text=True, check=True
            )
            if expected_arch == "x86_64":
                return "x86-64" in result.stdout or "x86_64" in result.stdout
            elif expected_arch == "arm64":
                return "arm64" in result.stdout or "aarch64" in result.stdout
    except subprocess.CalledProcessError:
        pass
    return False


# =============================================================================
# Pre-flight Checks
# =============================================================================


@dataclass
class PreflightResult:
    """Result of a preflight check."""

    name: str
    passed: bool
    message: str
    fix_hint: Optional[str] = None


def check_gh_cli() -> PreflightResult:
    """Check if GitHub CLI is installed and authenticated."""
    try:
        result = subprocess.run(
            ["gh", "auth", "status"], capture_output=True, text=True, check=False
        )
        if result.returncode == 0:
            return PreflightResult("GitHub CLI", True, "Authenticated")
        else:
            return PreflightResult(
                "GitHub CLI", False, "Not authenticated", "Run: gh auth login"
            )
    except FileNotFoundError:
        return PreflightResult(
            "GitHub CLI", False, "Not installed", "Install: brew install gh"
        )


def check_docker() -> PreflightResult:
    """Check if Docker is available."""
    try:
        result = subprocess.run(
            ["docker", "info"], capture_output=True, text=True, check=False, timeout=10
        )
        if result.returncode == 0:
            return PreflightResult("Docker", True, "Running")
        else:
            return PreflightResult(
                "Docker",
                False,
                "Not running",
                "Start Docker Desktop or: sudo systemctl start docker",
            )
    except FileNotFoundError:
        return PreflightResult(
            "Docker",
            False,
            "Not installed",
            "Install Docker Desktop from https://docker.com",
        )
    except subprocess.TimeoutExpired:
        return PreflightResult("Docker", False, "Not responding", "Restart Docker")


def check_skia_libraries(
    project_root: Path, platforms: list[str]
) -> list[PreflightResult]:
    """Check if Skia libraries exist for target platforms."""
    results = []
    skia_base = project_root / "skia-build" / "src" / "skia" / "out"

    for plat in platforms:
        if plat not in PLATFORMS:
            continue

        config = PLATFORMS[plat]
        skia_dir = config.get("skia_dir")
        if not skia_dir:
            continue

        skia_path = skia_base / skia_dir / "libskia.a"

        # Try fallback for macOS
        if not skia_path.exists() and plat.startswith("macos"):
            skia_path = skia_base / "release-macos" / "libskia.a"

        if skia_path.exists():
            # Verify architecture for macOS
            if plat.startswith("macos"):
                expected_arch = config.get("lipo_arch", "")
                try:
                    result = subprocess.run(
                        ["lipo", "-info", str(skia_path)],
                        capture_output=True,
                        text=True,
                        check=True,
                    )
                    if expected_arch in result.stdout:
                        results.append(
                            PreflightResult(
                                f"Skia ({config['display_name']})",
                                True,
                                f"Found at {skia_path.relative_to(project_root)}",
                            )
                        )
                    else:
                        results.append(
                            PreflightResult(
                                f"Skia ({config['display_name']})",
                                False,
                                f"Wrong architecture (expected {expected_arch})",
                                f"Rebuild: cd skia-build && ./build-macos-{config['build_args'][0]}.sh",
                            )
                        )
                except subprocess.CalledProcessError:
                    results.append(
                        PreflightResult(
                            f"Skia ({config['display_name']})",
                            True,
                            "Found (arch unverified)",
                        )
                    )
            else:
                results.append(
                    PreflightResult(
                        f"Skia ({config['display_name']})",
                        True,
                        f"Found at {skia_path.relative_to(project_root)}",
                    )
                )
        else:
            arch_suffix = config["build_args"][0] if config.get("build_args") else ""
            build_cmd = f"cd skia-build && ./build-{plat.split('-')[0]}"
            if arch_suffix:
                build_cmd += f"-{arch_suffix}"
            build_cmd += ".sh"

            results.append(
                PreflightResult(
                    f"Skia ({config['display_name']})",
                    False,
                    f"Not found at {skia_path}",
                    f"Build: {build_cmd}",
                )
            )

    return results


def check_disk_space(project_root: Path) -> PreflightResult:
    """Check available disk space."""
    free_gb = get_disk_space_gb(project_root)
    if free_gb >= MIN_DISK_SPACE_GB:
        return PreflightResult("Disk Space", True, f"{free_gb:.1f} GB available")
    else:
        return PreflightResult(
            "Disk Space",
            False,
            f"Only {free_gb:.1f} GB available (need {MIN_DISK_SPACE_GB} GB)",
            "Free up disk space",
        )


def check_git_clean(project_root: Path) -> PreflightResult:
    """Check if git working directory is clean (optional warning)."""
    try:
        result = subprocess.run(
            ["git", "status", "--porcelain"],
            capture_output=True,
            text=True,
            check=True,
            cwd=project_root,
        )
        if result.stdout.strip():
            return PreflightResult(
                "Git Status",
                True,  # Warning, not failure
                "Uncommitted changes present (will continue)",
                "Consider: git stash or git commit",
            )
        return PreflightResult("Git Status", True, "Clean")
    except subprocess.CalledProcessError:
        return PreflightResult("Git Status", True, "Could not check (not a git repo?)")


def check_rosetta(need_x64_on_arm: bool) -> PreflightResult:
    """Check if Rosetta 2 is available for x64 cross-compilation on ARM64."""
    if not need_x64_on_arm:
        return PreflightResult("Rosetta 2", True, "Not needed")

    if get_current_platform() != "macos" or get_current_arch() != "arm64":
        return PreflightResult("Rosetta 2", True, "Not needed (not ARM64 Mac)")

    try:
        # Check if Rosetta is installed by running a simple x64 command
        result = subprocess.run(
            ["arch", "-x86_64", "uname", "-m"],
            capture_output=True,
            text=True,
            check=False,
            timeout=5,
        )
        if result.returncode == 0 and "x86_64" in result.stdout:
            return PreflightResult("Rosetta 2", True, "Installed and working")
        else:
            return PreflightResult(
                "Rosetta 2",
                False,
                "Not installed or not working",
                "Install: softwareupdate --install-rosetta",
            )
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return PreflightResult(
            "Rosetta 2",
            False,
            "Could not verify",
            "Install: softwareupdate --install-rosetta",
        )


def run_preflight_checks(
    project_root: Path, platforms: list[str], skip_build: bool
) -> bool:
    """Run all preflight checks."""
    log("Running pre-flight checks...", "STEP")

    checks = []

    # Essential checks
    checks.append(check_gh_cli())
    checks.append(check_disk_space(project_root))
    checks.append(check_git_clean(project_root))

    # Platform-specific checks
    if not skip_build:
        if "linux" in platforms:
            checks.append(check_docker())

        # Check Skia libraries
        checks.extend(check_skia_libraries(project_root, platforms))

        # Check Rosetta if building x64 on ARM64 Mac
        need_x64 = "macos-x64" in platforms
        if need_x64 and get_current_arch() == "arm64":
            checks.append(check_rosetta(True))

    # Report results
    all_passed = True
    for check in checks:
        status = "✓" if check.passed else "✗"
        level = "SUCCESS" if check.passed else "ERROR"
        log(f"  {status} {check.name}: {check.message}", level)
        if not check.passed and check.fix_hint:
            log(f"    → Fix: {check.fix_hint}", "WARNING")
            all_passed = False

    if not all_passed:
        log("\nSome pre-flight checks failed. Fix the issues above and retry.", "ERROR")

    return all_passed


# =============================================================================
# Build Functions
# =============================================================================


@dataclass
class BuildResult:
    """Result of a platform build."""

    platform: str
    success: bool
    binary_path: Optional[Path]
    error: Optional[str] = None
    duration_seconds: float = 0.0


def build_macos_arch(
    project_root: Path, arch: str, dry_run: bool = False
) -> BuildResult:
    """Build for macOS with specific architecture (arm64 or x64)."""
    start_time = time.time()
    platform_key = f"macos-{arch}"
    config = PLATFORMS[platform_key]
    display_name = config["display_name"]
    log(f"Building for {display_name}...", "STEP")

    if get_current_platform() != "macos":
        return BuildResult(platform_key, False, None, "Requires macOS host")

    build_script = project_root / "scripts" / "build-macos-arch.sh"
    if not build_script.exists():
        return BuildResult(
            platform_key, False, None, f"Build script not found: {build_script}"
        )

    try:
        # Check if cross-compilation needed (building x64 on ARM64)
        current_arch = get_current_arch()
        cmd = ["bash", str(build_script), arch]

        if arch == "x64" and current_arch == "arm64":
            log("Cross-compiling x64 on ARM64 via Rosetta 2", "INFO")
            # Use Rosetta prefix for x64 build on ARM64
            rosetta_prefix = config.get("rosetta_prefix", [])
            if rosetta_prefix:
                cmd = rosetta_prefix + cmd

        # Pass CODESIGN_IDENTITY to build script if available
        build_env = {}
        codesign_identity = os.environ.get("CODESIGN_IDENTITY")
        if codesign_identity:
            build_env["CODESIGN_IDENTITY"] = codesign_identity
            log(f"Code signing enabled with identity: {codesign_identity}", "INFO")

        run_cmd(
            cmd,
            cwd=project_root,
            dry_run=dry_run,
            timeout=1800,
            env=build_env if build_env else None,
        )  # 30 min timeout

        binary_name = config["binary_name"]
        binary_path = project_root / "build" / binary_name
        duration = time.time() - start_time

        if dry_run:
            log(f"{display_name} build successful (dry-run)", "SUCCESS")
            return BuildResult(
                platform_key, True, binary_path, duration_seconds=duration
            )

        if binary_path.exists():
            # Verify architecture
            expected_lipo_arch = config.get("lipo_arch", arch)
            if verify_binary_architecture(binary_path, expected_lipo_arch):
                log(f"{display_name} build successful ({duration:.1f}s)", "SUCCESS")
                return BuildResult(
                    platform_key, True, binary_path, duration_seconds=duration
                )
            else:
                return BuildResult(
                    platform_key,
                    False,
                    None,
                    f"Binary architecture mismatch (expected {expected_lipo_arch})",
                    duration_seconds=duration,
                )
        else:
            return BuildResult(
                platform_key,
                False,
                None,
                "Binary not found after build",
                duration_seconds=duration,
            )

    except subprocess.CalledProcessError as e:
        duration = time.time() - start_time
        return BuildResult(
            platform_key, False, None, f"Build failed: {e}", duration_seconds=duration
        )
    except subprocess.TimeoutExpired:
        duration = time.time() - start_time
        return BuildResult(
            platform_key,
            False,
            None,
            "Build timed out (30 min)",
            duration_seconds=duration,
        )


def build_linux(project_root: Path, dry_run: bool = False) -> BuildResult:
    """Build for Linux using Docker."""
    start_time = time.time()
    log("Building for Linux (x64) via Docker...", "STEP")

    docker_dir = project_root / "docker"

    try:
        # Ensure container is running
        run_cmd(
            ["docker-compose", "up", "-d"],
            cwd=docker_dir,
            dry_run=dry_run,
            timeout=120,
        )

        # Execute build inside container
        run_cmd(
            [
                "docker-compose",
                "exec",
                "-T",
                "dev",
                "bash",
                "-c",
                "cd /workspace && ./scripts/build-linux.sh",
            ],
            cwd=docker_dir,
            dry_run=dry_run,
            timeout=1800,  # 30 min timeout
        )

        binary_path = project_root / "build" / "linux" / "fbfsvg-player"
        duration = time.time() - start_time

        if dry_run or binary_path.exists():
            log(f"Linux build successful ({duration:.1f}s)", "SUCCESS")
            return BuildResult("linux", True, binary_path, duration_seconds=duration)
        else:
            return BuildResult(
                "linux",
                False,
                None,
                "Binary not found after build",
                duration_seconds=duration,
            )

    except subprocess.CalledProcessError as e:
        duration = time.time() - start_time
        return BuildResult(
            "linux", False, None, f"Build failed: {e}", duration_seconds=duration
        )
    except subprocess.TimeoutExpired:
        duration = time.time() - start_time
        return BuildResult(
            "linux", False, None, "Build timed out (30 min)", duration_seconds=duration
        )


def build_windows(project_root: Path, dry_run: bool = False) -> BuildResult:
    """Build for Windows."""
    start_time = time.time()
    log("Building for Windows (x64)...", "STEP")

    if get_current_platform() != "windows":
        return BuildResult("windows", False, None, "Requires Windows host or CI")

    build_script = project_root / "scripts" / "build-windows.bat"
    if not build_script.exists():
        return BuildResult(
            "windows", False, None, f"Build script not found: {build_script}"
        )

    try:
        run_cmd(
            ["cmd", "/c", str(build_script)],
            cwd=project_root,
            dry_run=dry_run,
            timeout=1800,
        )
        binary_path = project_root / "build" / "windows" / "fbfsvg-player.exe"
        duration = time.time() - start_time

        if dry_run or binary_path.exists():
            log(f"Windows build successful ({duration:.1f}s)", "SUCCESS")
            return BuildResult("windows", True, binary_path, duration_seconds=duration)
        else:
            return BuildResult(
                "windows",
                False,
                None,
                "Binary not found after build",
                duration_seconds=duration,
            )

    except subprocess.CalledProcessError as e:
        duration = time.time() - start_time
        return BuildResult(
            "windows", False, None, f"Build failed: {e}", duration_seconds=duration
        )


def build_macos_parallel(
    project_root: Path, dry_run: bool = False
) -> dict[str, BuildResult]:
    """Build both macOS architectures in parallel."""
    log("Building macOS ARM64 and x64 in parallel...", "STEP")

    results = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
        futures = {
            executor.submit(
                build_macos_arch, project_root, "arm64", dry_run
            ): "macos-arm64",
            executor.submit(
                build_macos_arch, project_root, "x64", dry_run
            ): "macos-x64",
        }

        for future in concurrent.futures.as_completed(futures):
            platform_key = futures[future]
            try:
                results[platform_key] = future.result()
            except Exception as e:
                results[platform_key] = BuildResult(platform_key, False, None, str(e))

    return results


# =============================================================================
# Packaging Functions
# =============================================================================


@dataclass
class PackageResult:
    """Result of package creation."""

    platform: str
    format: str
    path: Path
    sha256: str
    size: str


def create_tarball(binary_path: Path, output_path: Path, name: str) -> Path:
    """Create a minimal .tar.gz package."""
    with tarfile.open(output_path, "w:gz") as tar:
        tar.add(binary_path, arcname=name)
    return output_path


def create_zip_package(binary_path: Path, output_path: Path, name: str) -> Path:
    """Create a .zip package."""
    import zipfile

    with zipfile.ZipFile(output_path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.write(binary_path, arcname=name)
    return output_path


def create_deb_package(binary_path: Path, output_dir: Path, version: str) -> Path:
    """Create a minimal .deb package."""
    deb_root = output_dir / "deb_build"
    deb_root.mkdir(parents=True, exist_ok=True)

    # Create directory structure
    (deb_root / "DEBIAN").mkdir(exist_ok=True)
    (deb_root / "usr" / "bin").mkdir(parents=True, exist_ok=True)

    # Copy binary
    shutil.copy2(binary_path, deb_root / "usr" / "bin" / "fbfsvg-player")
    os.chmod(deb_root / "usr" / "bin" / "fbfsvg-player", 0o755)

    # Create control file
    control_content = f"""Package: fbfsvg-player
Version: {version}
Section: graphics
Priority: optional
Architecture: amd64
Maintainer: Emasoft <713559+Emasoft@users.noreply.github.com>
Description: High-performance animated SVG player for FBF.SVG format
 fbfsvg-player is a multi-platform player for the FBF.SVG vector video format,
 built using Skia for hardware-accelerated rendering.
Homepage: https://github.com/Emasoft/fbfsvg-player
"""
    (deb_root / "DEBIAN" / "control").write_text(control_content)

    # Build .deb
    output_path = output_dir / f"fbfsvg-player_{version}_amd64.deb"
    run_cmd(["dpkg-deb", "--build", str(deb_root), str(output_path)])

    # Cleanup
    shutil.rmtree(deb_root)

    return output_path


def create_appimage(binary_path: Path, output_dir: Path, version: str) -> Path:
    """Create a minimal AppImage package."""
    appdir = output_dir / "AppDir"
    appdir.mkdir(parents=True, exist_ok=True)

    # Create directory structure
    (appdir / "usr" / "bin").mkdir(parents=True, exist_ok=True)

    # Copy binary
    shutil.copy2(binary_path, appdir / "usr" / "bin" / "fbfsvg-player")
    os.chmod(appdir / "usr" / "bin" / "fbfsvg-player", 0o755)

    # Create .desktop file
    desktop_content = """[Desktop Entry]
Name=FBF.SVG Player
Exec=fbfsvg-player
Icon=fbfsvg-player
Type=Application
Categories=Graphics;Video;
Comment=High-performance animated SVG player for FBF.SVG format
"""
    (appdir / "fbfsvg-player.desktop").write_text(desktop_content)

    # Create AppRun
    apprun_content = """#!/bin/bash
HERE="$(dirname "$(readlink -f "${0}")")"
exec "$HERE/usr/bin/fbfsvg-player" "$@"
"""
    apprun_path = appdir / "AppRun"
    apprun_path.write_text(apprun_content)
    os.chmod(apprun_path, 0o755)

    # Create a placeholder icon (1x1 PNG)
    icon_path = appdir / "fbfsvg-player.png"
    # Minimal 1x1 transparent PNG
    icon_data = bytes(
        [
            0x89,
            0x50,
            0x4E,
            0x47,
            0x0D,
            0x0A,
            0x1A,
            0x0A,
            0x00,
            0x00,
            0x00,
            0x0D,
            0x49,
            0x48,
            0x44,
            0x52,
            0x00,
            0x00,
            0x00,
            0x01,
            0x00,
            0x00,
            0x00,
            0x01,
            0x08,
            0x06,
            0x00,
            0x00,
            0x00,
            0x1F,
            0x15,
            0xC4,
            0x89,
            0x00,
            0x00,
            0x00,
            0x0A,
            0x49,
            0x44,
            0x41,
            0x54,
            0x78,
            0x9C,
            0x63,
            0x00,
            0x01,
            0x00,
            0x00,
            0x05,
            0x00,
            0x01,
            0x0D,
            0x0A,
            0x2D,
            0xB4,
            0x00,
            0x00,
            0x00,
            0x00,
            0x49,
            0x45,
            0x4E,
            0x44,
            0xAE,
            0x42,
            0x60,
            0x82,
        ]
    )
    icon_path.write_bytes(icon_data)

    # Check for appimagetool
    output_path = output_dir / f"fbfsvg-player-{version}-x86_64.AppImage"

    try:
        run_cmd(["appimagetool", "--version"], capture=True)
        run_cmd(["appimagetool", str(appdir), str(output_path)])
    except FileNotFoundError:
        log("appimagetool not found, creating tarball instead", "WARNING")
        # Fallback: create a tarball of AppDir
        output_path = output_dir / f"fbfsvg-player-{version}-x86_64.AppDir.tar.gz"
        with tarfile.open(output_path, "w:gz") as tar:
            tar.add(appdir, arcname="AppDir")

    # Cleanup
    shutil.rmtree(appdir)

    return output_path


def create_packages(
    build_results: dict[str, BuildResult],
    release_dir: Path,
    version: str,
    dry_run: bool = False,
) -> list[PackageResult]:
    """Create distribution packages for all successful builds."""
    log("Creating distribution packages...", "STEP")

    packages = []

    for platform_key, result in build_results.items():
        if not result.success:
            continue
        if result.binary_path is None:
            log(f"Skipping {platform_key}: no binary path available", "WARNING")
            continue

        config = PLATFORMS.get(platform_key, {})

        # Handle macOS architecture-specific packages
        if platform_key in ("macos-arm64", "macos-x64"):
            asset_name = config["asset_name"].format(name=PROJECT_NAME, version=version)
            output_path = release_dir / asset_name

            if dry_run:
                output_path.touch()
                packages.append(
                    PackageResult(
                        platform_key, "tar.gz", output_path, "DRY_RUN_CHECKSUM", "0 B"
                    )
                )
            else:
                create_tarball(result.binary_path, output_path, "fbfsvg-player")
                checksum = sha256_file(output_path)
                size = get_file_size(output_path)
                packages.append(
                    PackageResult(platform_key, "tar.gz", output_path, checksum, size)
                )
                log(f"Created {asset_name} ({size})", "SUCCESS")

        elif platform_key == "linux":
            # Create multiple Linux packages
            formats = config.get("package_formats", ["tar.gz"])
            asset_names = config.get("asset_names", {})

            for fmt in formats:
                asset_name = asset_names.get(
                    fmt, f"{PROJECT_NAME}-{version}-linux.{fmt}"
                )
                asset_name = asset_name.format(name=PROJECT_NAME, version=version)
                output_path = release_dir / asset_name

                if dry_run:
                    output_path.touch()
                    packages.append(
                        PackageResult(
                            platform_key, fmt, output_path, "DRY_RUN_CHECKSUM", "0 B"
                        )
                    )
                else:
                    try:
                        if fmt == "tar.gz":
                            create_tarball(
                                result.binary_path, output_path, "fbfsvg-player"
                            )
                        elif fmt == "deb":
                            output_path = create_deb_package(
                                result.binary_path, release_dir, version
                            )
                        elif fmt == "appimage":
                            output_path = create_appimage(
                                result.binary_path, release_dir, version
                            )

                        if output_path.exists():
                            checksum = sha256_file(output_path)
                            size = get_file_size(output_path)
                            packages.append(
                                PackageResult(
                                    platform_key, fmt, output_path, checksum, size
                                )
                            )
                            log(f"Created {output_path.name} ({size})", "SUCCESS")
                    except Exception as e:
                        log(f"Failed to create {fmt} package: {e}", "ERROR")

        elif platform_key == "windows":
            asset_name = config["asset_name"].format(name=PROJECT_NAME, version=version)
            output_path = release_dir / asset_name

            if dry_run:
                output_path.touch()
                packages.append(
                    PackageResult(
                        platform_key, "zip", output_path, "DRY_RUN_CHECKSUM", "0 B"
                    )
                )
            else:
                create_zip_package(result.binary_path, output_path, "fbfsvg-player.exe")
                checksum = sha256_file(output_path)
                size = get_file_size(output_path)
                packages.append(
                    PackageResult(platform_key, "zip", output_path, checksum, size)
                )
                log(f"Created {asset_name} ({size})", "SUCCESS")

    return packages


def create_checksums_file(packages: list[PackageResult], release_dir: Path) -> Path:
    """Create SHA256SUMS.txt file."""
    checksums_path = release_dir / "SHA256SUMS.txt"
    with open(checksums_path, "w") as f:
        for pkg in packages:
            f.write(f"{pkg.sha256}  {pkg.path.name}\n")
    return checksums_path


# =============================================================================
# GitHub Release Functions
# =============================================================================


def create_draft_release(
    version: str,
    packages: list[PackageResult],
    checksums_path: Path,
    dry_run: bool = False,
) -> Optional[str]:
    """Create a draft release on GitHub with uploaded assets."""
    tag = f"v{version}"
    log(f"Creating draft release {tag}...", "STEP")

    # Generate release notes
    release_notes = f"""## fbfsvg-player {tag}

High-performance animated SVG player for the FBF.SVG vector video format.

### Downloads

| Platform | Download | Size |
|----------|----------|------|
"""
    for pkg in packages:
        display_name = PLATFORMS.get(pkg.platform, {}).get(
            "display_name", pkg.platform.title()
        )
        release_notes += f"| {display_name} | [{pkg.path.name}](https://github.com/{GITHUB_REPO}/releases/download/{tag}/{pkg.path.name}) | {pkg.size} |\n"

    release_notes += """
### Checksums (SHA256)

```
"""
    for pkg in packages:
        release_notes += f"{pkg.sha256}  {pkg.path.name}\n"
    release_notes += "```\n"

    release_notes += """
### Installation

**macOS (Homebrew)**
```bash
brew tap Emasoft/fbfsvg-player
brew install fbfsvg-player
```

**Linux (APT)**
```bash
wget https://github.com/Emasoft/fbfsvg-player/releases/download/{tag}/fbfsvg-player_{version}_amd64.deb
sudo dpkg -i fbfsvg-player_{version}_amd64.deb
```

**Windows (Scoop)**
```powershell
scoop bucket add fbfsvg-player https://github.com/Emasoft/fbfsvg-player
scoop install fbfsvg-player
```
""".format(tag=tag, version=version)

    # Create draft release
    if dry_run:
        log("[DRY-RUN] Would create draft release", "WARNING")
        return "dry-run-release-id"

    # Check if tag exists
    result = run_cmd(
        ["gh", "release", "view", tag],
        capture=True,
        check=False,
        retry=NETWORK_RETRY_COUNT,
    )
    if result.returncode == 0:
        log(f"Release {tag} already exists, updating...", "WARNING")
        run_cmd(["gh", "release", "delete", tag, "--yes"], retry=NETWORK_RETRY_COUNT)

    # Create release
    with tempfile.NamedTemporaryFile(mode="w", suffix=".md", delete=False) as f:
        f.write(release_notes)
        notes_file = f.name

    try:
        cmd = [
            "gh",
            "release",
            "create",
            tag,
            "--draft",
            "--title",
            f"fbfsvg-player {tag}",
            "--notes-file",
            notes_file,
        ]

        # Add all package files
        for pkg in packages:
            cmd.append(str(pkg.path))

        # Add checksums file
        cmd.append(str(checksums_path))

        run_cmd(cmd, retry=NETWORK_RETRY_COUNT)
        log(f"Created draft release {tag}", "SUCCESS")
        return tag

    finally:
        os.unlink(notes_file)


def validate_release(
    tag: str, packages: list[PackageResult], dry_run: bool = False
) -> bool:
    """Validate that all release assets were uploaded correctly."""
    log(f"Validating release {tag}...", "STEP")

    if dry_run:
        log("[DRY-RUN] Would validate release assets", "WARNING")
        return True

    # Get release info
    result = run_cmd(
        ["gh", "release", "view", tag, "--json", "assets"],
        capture=True,
        retry=NETWORK_RETRY_COUNT,
    )

    release_info = json.loads(result.stdout)
    uploaded_assets = {a["name"] for a in release_info.get("assets", [])}

    expected_assets = {pkg.path.name for pkg in packages}
    expected_assets.add("SHA256SUMS.txt")

    missing = expected_assets - uploaded_assets
    if missing:
        log(f"Missing assets: {missing}", "ERROR")
        return False

    log(f"All {len(expected_assets)} assets validated", "SUCCESS")
    return True


def publish_release(tag: str, dry_run: bool = False) -> bool:
    """Publish the draft release."""
    log(f"Publishing release {tag}...", "STEP")

    if dry_run:
        log("[DRY-RUN] Would publish release", "WARNING")
        return True

    run_cmd(["gh", "release", "edit", tag, "--draft=false"], retry=NETWORK_RETRY_COUNT)
    log(f"Release {tag} published successfully", "SUCCESS")
    return True


# =============================================================================
# Package Manifest Update Functions
# =============================================================================


def update_homebrew_formula(
    project_root: Path,
    version: str,
    packages: list[PackageResult],
    dry_run: bool = False,
) -> None:
    """Update Homebrew formula with new version and checksum for both architectures."""
    log("Updating Homebrew formulas...", "STEP")

    # Find macOS packages for both architectures
    macos_arm64_pkg = next(
        (p for p in packages if p.platform == "macos-arm64" and p.format == "tar.gz"),
        None,
    )
    macos_x64_pkg = next(
        (p for p in packages if p.platform == "macos-x64" and p.format == "tar.gz"),
        None,
    )
    linux_pkg = next(
        (p for p in packages if p.platform == "linux" and p.format == "tar.gz"), None
    )

    # Update macOS formula (supports both ARM64 and Intel via on_arm/on_intel blocks)
    formula_path = project_root / "Formula" / "fbfsvg-player.rb"
    if formula_path.exists():
        content = formula_path.read_text()

        # Update version (common field)
        content = re.sub(r'version "[\d.]+"', f'version "{version}"', content)

        # Update on_arm block (ARM64 / Apple Silicon)
        if macos_arm64_pkg:
            # Update ARM64 URL in on_arm block
            content = re.sub(
                r'(on_arm do\s+url )"https://github\.com/Emasoft/fbfsvg-player/releases/download/v[\d.]+/[^"]+\.tar\.gz"',
                f'\\1"https://github.com/Emasoft/fbfsvg-player/releases/download/v{version}/{macos_arm64_pkg.path.name}"',
                content,
            )
            # Update ARM64 SHA256 in on_arm block
            content = re.sub(
                r'(on_arm do\s+url "[^"]+"\s+sha256 )"[a-fA-F0-9]+"',
                f'\\1"{macos_arm64_pkg.sha256}"',
                content,
            )
            log(
                f"Updated on_arm block with ARM64 SHA256: {macos_arm64_pkg.sha256[:16]}...",
                "SUCCESS",
            )

        # Update on_intel block (x86_64 / Intel)
        if macos_x64_pkg:
            # Update x64 URL in on_intel block
            content = re.sub(
                r'(on_intel do\s+url )"https://github\.com/Emasoft/fbfsvg-player/releases/download/v[\d.]+/[^"]+\.tar\.gz"',
                f'\\1"https://github.com/Emasoft/fbfsvg-player/releases/download/v{version}/{macos_x64_pkg.path.name}"',
                content,
            )
            # Update x64 SHA256 in on_intel block
            content = re.sub(
                r'(on_intel do\s+url "[^"]+"\s+sha256 )"[a-fA-F0-9_]+"',
                f'\\1"{macos_x64_pkg.sha256}"',
                content,
            )
            log(
                f"Updated on_intel block with x64 SHA256: {macos_x64_pkg.sha256[:16]}...",
                "SUCCESS",
            )

        if not dry_run:
            formula_path.write_text(content)
        log(f"Updated {formula_path.name}", "SUCCESS")
    else:
        log(f"Homebrew formula not found: {formula_path}", "WARNING")

    # Update Linux formula
    if linux_pkg:
        formula_path = project_root / "Formula" / "fbfsvg-player@linux.rb"
        if formula_path.exists():
            content = formula_path.read_text()

            # Update URL
            content = re.sub(
                r'url "https://github\.com/Emasoft/fbfsvg-player/releases/download/v[\d.]+/.*\.tar\.gz"',
                f'url "https://github.com/Emasoft/fbfsvg-player/releases/download/v{version}/{linux_pkg.path.name}"',
                content,
            )

            # Update SHA256
            content = re.sub(
                r'sha256 "[a-f0-9]+"', f'sha256 "{linux_pkg.sha256}"', content
            )

            # Update version
            content = re.sub(r'version "[\d.]+"', f'version "{version}"', content)

            if not dry_run:
                formula_path.write_text(content)
            log(f"Updated {formula_path.name}", "SUCCESS")


def update_scoop_manifest(
    project_root: Path,
    version: str,
    packages: list[PackageResult],
    dry_run: bool = False,
) -> None:
    """Update Scoop manifest with new version and checksum."""
    log("Updating Scoop manifest...", "STEP")

    # Find Windows package
    win_pkg = next(
        (p for p in packages if p.platform == "windows" and p.format == "zip"), None
    )

    manifest_path = project_root / "bucket" / "fbfsvg-player.json"
    if not manifest_path.exists():
        log(f"Scoop manifest not found: {manifest_path}", "WARNING")
        return

    manifest = json.loads(manifest_path.read_text())

    # Update version
    manifest["version"] = version

    # Update URL and hash
    if win_pkg:
        manifest["architecture"]["64bit"]["url"] = (
            f"https://github.com/{GITHUB_REPO}/releases/download/v{version}/{win_pkg.path.name}"
        )
        manifest["architecture"]["64bit"]["hash"] = win_pkg.sha256
    else:
        manifest["architecture"]["64bit"]["url"] = (
            f"https://github.com/{GITHUB_REPO}/releases/download/v{version}/{PROJECT_NAME}-{version}-windows-x64.zip"
        )
        manifest["architecture"]["64bit"]["hash"] = "PENDING_WINDOWS_BUILD"

    if not dry_run:
        manifest_path.write_text(json.dumps(manifest, indent=4) + "\n")
    log(f"Updated {manifest_path.name}", "SUCCESS")


def commit_manifest_updates(
    project_root: Path, version: str, dry_run: bool = False
) -> None:
    """Commit and push manifest updates."""
    log("Committing manifest updates...", "STEP")

    if dry_run:
        log("[DRY-RUN] Would commit manifest updates", "WARNING")
        return

    run_cmd(["git", "add", "Formula/", "bucket/"], cwd=project_root)

    result = run_cmd(
        ["git", "diff", "--cached", "--quiet"],
        cwd=project_root,
        check=False,
    )

    if result.returncode != 0:  # Changes exist
        run_cmd(
            [
                "git",
                "commit",
                "-m",
                f"Update package manifests for v{version}\n\n- Update Homebrew formulas\n- Update Scoop manifest",
            ],
            cwd=project_root,
        )
        run_cmd(
            ["git", "push", "origin", "main"],
            cwd=project_root,
            retry=NETWORK_RETRY_COUNT,
        )
        log("Manifest updates committed and pushed", "SUCCESS")
    else:
        log("No manifest changes to commit", "INFO")


# =============================================================================
# Interactive Confirmation
# =============================================================================


def confirm_publish(
    version: str, packages: list[PackageResult], no_confirm: bool = False
) -> bool:
    """Ask user to confirm before publishing."""
    if no_confirm:
        return True

    print("\n" + "=" * 60)
    print(f"Ready to publish release v{version}")
    print("=" * 60)
    print("\nPackages to be released:")
    for pkg in packages:
        print(f"  • {pkg.path.name} ({pkg.size})")

    print("\n⚠️  Publishing is IRREVERSIBLE. The release will be visible to everyone.")
    print("   Package manifests will be updated and pushed.")

    try:
        response = input("\nType 'publish' to confirm: ").strip().lower()
        return response == "publish"
    except (KeyboardInterrupt, EOFError):
        print("\nCancelled.")
        return False


# =============================================================================
# CI Summary
# =============================================================================


def write_ci_summary(
    release_dir: Path,
    version: str,
    tag: str,
    success: bool,
    packages: list["PackageResult"],
    errors: list[str],
) -> None:
    """Write JSON summary for CI systems to release/release-summary.json."""
    summary = {
        "version": version,
        "tag": tag,
        "success": success,
        "packages": [
            {"name": pkg.path.name, "sha256": pkg.sha256, "size": pkg.size}
            for pkg in packages
        ],
        "errors": errors,
    }
    summary_path = release_dir / "release-summary.json"
    summary_path.write_text(json.dumps(summary, indent=2) + "\n")
    log(f"CI summary written to {summary_path}", "INFO")


# =============================================================================
# Main Release Workflow
# =============================================================================


def run_release(
    version: str,
    platforms: Optional[list[str]] = None,
    skip_build: bool = False,
    dry_run: bool = False,
    no_confirm: bool = False,
    parallel_macos: bool = True,
    ci_mode: bool = False,
) -> int:
    """Run the complete release workflow.

    Args:
        version: Version string (e.g., "0.2.0")
        platforms: List of platforms to build (default: all)
        skip_build: Skip build step and use existing binaries
        dry_run: Show what would be done without making changes
        no_confirm: Skip interactive publish confirmation
        parallel_macos: Build macOS architectures in parallel
        ci_mode: Enable CI mode (no colors, JSON summary, exit codes)

    Returns:
        Exit code: 0=success, 1=error, 2=partial success
    """
    # Enable CI mode globally if requested
    if ci_mode:
        set_ci_mode(True)
        no_confirm = True  # CI mode implies no interactive confirmation

    project_root = Path(__file__).parent.parent.resolve()
    release_dir = project_root / "release"

    # Track errors for CI summary
    errors: list[str] = []

    # Initialize logging
    init_logging(release_dir)

    log(f"Starting release workflow for v{version}", "STEP")
    log(f"Project root: {project_root}")
    log(f"Release directory: {release_dir}")
    if LOG_FILE:
        log(f"Log file: {LOG_FILE}")

    # Validate version
    if not validate_version(version):
        log(f"Invalid version format: {version}", "ERROR")
        log("Expected format: X.Y.Z or X.Y.Z-suffix", "ERROR")
        errors.append(f"Invalid version format: {version}")
        if ci_mode:
            write_ci_summary(release_dir, version, "", False, [], errors)
        return 1

    # Determine platforms to build
    if platforms is None:
        platforms = ["macos", "linux", "windows"]

    # Expand platform aliases (e.g., "macos" -> ["macos-arm64", "macos-x64"])
    expanded_platforms = []
    for p in platforms:
        if p in PLATFORM_ALIASES:
            expanded_platforms.extend(PLATFORM_ALIASES[p])
        else:
            expanded_platforms.append(p)
    platforms = expanded_platforms

    current_platform = get_current_platform()
    current_arch = get_current_arch()
    log(f"Current platform: {current_platform} ({current_arch})")
    log(f"Target platforms: {', '.join(platforms)}")

    # Pre-flight checks
    log("=" * 60)
    log("PRE-FLIGHT CHECKS", "STEP")
    log("=" * 60)

    if not run_preflight_checks(project_root, platforms, skip_build):
        if not dry_run:
            errors.append("Pre-flight checks failed")
            if ci_mode:
                write_ci_summary(release_dir, version, f"v{version}", False, [], errors)
            return 1
        else:
            log("[DRY-RUN] Continuing despite preflight failures", "WARNING")

    # Clean release directory
    if release_dir.exists() and not dry_run:
        shutil.rmtree(release_dir)
    release_dir.mkdir(parents=True, exist_ok=True)

    # Step 1: Build
    build_results: dict[str, BuildResult] = {}
    if not skip_build:
        log("=" * 60)
        log("STEP 1: Building for all platforms", "STEP")
        log("=" * 60)

        # Build macOS in parallel if both architectures requested
        macos_archs = [p for p in platforms if p.startswith("macos-")]
        if len(macos_archs) == 2 and parallel_macos and current_platform == "macos":
            macos_results = build_macos_parallel(project_root, dry_run)
            build_results.update(macos_results)
        else:
            # Build macOS sequentially
            for plat in macos_archs:
                arch = plat.split("-")[1]
                build_results[plat] = build_macos_arch(project_root, arch, dry_run)

        # Build other platforms
        if "linux" in platforms:
            build_results["linux"] = build_linux(project_root, dry_run)
        if "windows" in platforms:
            build_results["windows"] = build_windows(project_root, dry_run)

        # Report build results
        log("\nBuild Results:")
        total_time = sum(r.duration_seconds for r in build_results.values())
        for plat, result in build_results.items():
            status = "SUCCESS" if result.success else "FAILED"
            time_str = (
                f" ({result.duration_seconds:.1f}s)"
                if result.duration_seconds > 0
                else ""
            )
            log(
                f"  {plat}: {status}{time_str}",
                "SUCCESS" if result.success else "ERROR",
            )
            if result.error:
                log(f"    Error: {result.error}", "ERROR")
        log(f"Total build time: {total_time:.1f}s")
    else:
        log("Skipping build step (--skip-build)", "WARNING")
        # Assume existing binaries
        for plat in platforms:
            binary: Optional[Path] = None
            if plat == "macos-arm64":
                binary = project_root / "build" / "fbfsvg-player-macos-arm64"
            elif plat == "macos-x64":
                binary = project_root / "build" / "fbfsvg-player-macos-x64"
            elif plat == "linux":
                binary = project_root / "build" / "linux" / "fbfsvg-player"
            elif plat == "windows":
                binary = project_root / "build" / "windows" / "fbfsvg-player.exe"

            if binary is not None and (binary.exists() or dry_run):
                build_results[plat] = BuildResult(plat, True, binary)
            else:
                build_results[plat] = BuildResult(plat, False, None, "Binary not found")

    # Check if any builds succeeded
    successful_builds = [r for r in build_results.values() if r.success]
    if not successful_builds and not dry_run:
        log("No successful builds, aborting release", "ERROR")
        errors.append("No successful builds")
        if ci_mode:
            write_ci_summary(release_dir, version, f"v{version}", False, [], errors)
        return 1

    # Step 2: Create packages
    log("=" * 60)
    log("STEP 2: Creating distribution packages", "STEP")
    log("=" * 60)

    packages = create_packages(build_results, release_dir, version, dry_run)

    if not packages and not dry_run:
        log("No packages created, aborting release", "ERROR")
        errors.append("No packages created")
        if ci_mode:
            write_ci_summary(release_dir, version, f"v{version}", False, [], errors)
        return 1

    # Create checksums file
    if packages:
        checksums_path = create_checksums_file(packages, release_dir)
        log(f"Created {checksums_path.name}", "SUCCESS")
    else:
        checksums_path = release_dir / "SHA256SUMS.txt"

    # Step 3: Create draft release
    log("=" * 60)
    log("STEP 3: Creating draft release on GitHub", "STEP")
    log("=" * 60)

    tag = create_draft_release(version, packages, checksums_path, dry_run)
    if not tag:
        log("Failed to create draft release", "ERROR")
        errors.append("Failed to create draft release")
        if ci_mode:
            write_ci_summary(
                release_dir, version, f"v{version}", False, packages, errors
            )
        return 1

    # Step 4: Validate release
    log("=" * 60)
    log("STEP 4: Validating release assets", "STEP")
    log("=" * 60)

    if not validate_release(tag, packages, dry_run):
        log("Release validation failed", "ERROR")
        log("Draft release preserved for manual inspection", "WARNING")
        errors.append("Release validation failed")
        if ci_mode:
            write_ci_summary(release_dir, version, tag, False, packages, errors)
        return 1

    # Step 5: Interactive confirmation
    log("=" * 60)
    log("STEP 5: Publish confirmation", "STEP")
    log("=" * 60)

    if not dry_run and not confirm_publish(version, packages, no_confirm):
        log("Release cancelled by user", "WARNING")
        log(
            f"Draft release preserved: https://github.com/{GITHUB_REPO}/releases/tag/{tag}",
            "INFO",
        )
        # User cancellation is not an error, exit code 0
        if ci_mode:
            write_ci_summary(
                release_dir, version, tag, True, packages, ["Cancelled by user"]
            )
        return 0

    # Step 6: Publish release
    log("=" * 60)
    log("STEP 6: Publishing release", "STEP")
    log("=" * 60)

    if not publish_release(tag, dry_run):
        log("Failed to publish release", "ERROR")
        errors.append("Failed to publish release")
        if ci_mode:
            write_ci_summary(release_dir, version, tag, False, packages, errors)
        return 1

    # Step 7: Update package manifests
    log("=" * 60)
    log("STEP 7: Updating package manifests", "STEP")
    log("=" * 60)

    update_homebrew_formula(project_root, version, packages, dry_run)
    update_scoop_manifest(project_root, version, packages, dry_run)
    commit_manifest_updates(project_root, version, dry_run)

    # Summary
    log("=" * 60)
    log("RELEASE COMPLETE", "SUCCESS")
    log("=" * 60)
    log(f"Version: v{version}")
    log(f"Tag: {tag}")
    log(f"Packages created: {len(packages)}")
    for pkg in packages:
        log(f"  - {pkg.path.name} ({pkg.size})")
    log(f"\nView release: https://github.com/{GITHUB_REPO}/releases/tag/{tag}")
    if LOG_FILE:
        log(f"Full log: {LOG_FILE}")

    # Write CI summary on success
    if ci_mode:
        write_ci_summary(release_dir, version, tag, True, packages, [])

    return 0


# =============================================================================
# CLI Entry Point
# =============================================================================


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Automate fbfsvg-player releases for all platforms",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 scripts/release.py --version 0.2.0
  python3 scripts/release.py --version 0.2.0 --dry-run
  python3 scripts/release.py --version 0.2.0 --skip-build
  python3 scripts/release.py --version 0.2.0 --platform macos linux
  python3 scripts/release.py --version 0.2.0 --no-confirm
  python3 scripts/release.py --version 0.2.0 --ci  # GitHub Actions mode

Features:
  • Pre-flight checks for dependencies, Skia libraries, disk space
  • Parallel macOS builds (ARM64 + x64 simultaneously)
  • Cross-compilation support (x64 on ARM64 via Rosetta 2)
  • Binary architecture verification
  • Network retry logic for GitHub API calls
  • Interactive publish confirmation
  • Detailed logging to file
  • CI mode for GitHub Actions (--ci)
        """,
    )

    parser.add_argument(
        "--version",
        "-v",
        required=True,
        help="Version number (e.g., 0.2.0, 1.0.0-beta)",
    )

    parser.add_argument(
        "--platform",
        "-p",
        nargs="+",
        choices=["macos", "linux", "windows"],
        help="Platforms to build (default: all)",
    )

    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Skip build step (use existing binaries)",
    )

    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Show what would be done without making changes",
    )

    parser.add_argument(
        "--no-confirm",
        action="store_true",
        help="Skip interactive publish confirmation",
    )

    parser.add_argument(
        "--sequential-macos",
        action="store_true",
        help="Build macOS architectures sequentially (default: parallel)",
    )

    parser.add_argument(
        "--ci",
        action="store_true",
        help="CI mode: no colors, JSON summary, exit codes (0=success, 1=error, 2=partial)",
    )

    args = parser.parse_args()

    try:
        exit_code = run_release(
            version=args.version,
            platforms=args.platform,
            skip_build=args.skip_build,
            dry_run=args.dry_run,
            no_confirm=args.no_confirm,
            parallel_macos=not args.sequential_macos,
            ci_mode=args.ci,
        )
        sys.exit(exit_code)
    except KeyboardInterrupt:
        log("\nRelease cancelled by user", "WARNING")
        sys.exit(130)
    except Exception as e:
        log(f"Release failed: {e}", "ERROR")
        import traceback

        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
