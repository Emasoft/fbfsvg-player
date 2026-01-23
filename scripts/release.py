#!/usr/bin/env python3
"""
fbfsvg-player Release Automation Script

Automates the complete release process for all platforms:
1. Version validation and tagging
2. Building for macOS, Linux, Windows
3. Creating distribution packages
4. Draft release creation on GitHub
5. Asset upload and checksum generation
6. Validation of all assets
7. Publishing the release
8. Updating package manifests (Homebrew, Scoop)

Usage:
    python3 scripts/release.py --version 0.2.0
    python3 scripts/release.py --version 0.2.0 --dry-run
    python3 scripts/release.py --version 0.2.0 --skip-build
    python3 scripts/release.py --version 0.2.0 --platform macos
"""

import argparse
import hashlib
import json
import os
import platform
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Optional


# =============================================================================
# Configuration
# =============================================================================

PROJECT_NAME = "fbfsvg-player"
GITHUB_REPO = "Emasoft/fbfsvg-player"
LICENSE = "BSD-3-Clause"

# Platform configurations
PLATFORMS = {
    "macos": {
        "os": "Darwin",
        "arch": "arm64",
        "build_script": "scripts/build-macos.sh",
        "binary_name": "fbfsvg-player",
        "package_format": "tar.gz",
        "asset_name": "{name}-{version}-macos-arm64.tar.gz",
    },
    "linux": {
        "os": "Linux",
        "arch": "x86_64",
        "build_script": "scripts/build-linux.sh",
        "binary_name": "fbfsvg-player",
        "package_formats": ["tar.gz", "deb", "appimage"],
        "asset_names": {
            "tar.gz": "{name}-{version}-linux-x64.tar.gz",
            "deb": "{name}_{version}_amd64.deb",
            "appimage": "{name}-{version}-x86_64.AppImage",
        },
    },
    "windows": {
        "os": "Windows",
        "arch": "x86_64",
        "build_script": "scripts/build-windows.bat",
        "binary_name": "fbfsvg-player.exe",
        "package_format": "zip",
        "asset_name": "{name}-{version}-windows-x64.zip",
    },
}


# =============================================================================
# Utility Functions
# =============================================================================

def log(msg: str, level: str = "INFO"):
    """Print a log message with timestamp and level."""
    timestamp = datetime.now().strftime("%H:%M:%S")
    colors = {
        "INFO": "\033[0;36m",    # Cyan
        "SUCCESS": "\033[0;32m", # Green
        "WARNING": "\033[0;33m", # Yellow
        "ERROR": "\033[0;31m",   # Red
        "STEP": "\033[1;35m",    # Bold Magenta
    }
    reset = "\033[0m"
    color = colors.get(level, "")
    print(f"{color}[{timestamp}] [{level}] {msg}{reset}")


def run_cmd(cmd: list[str], cwd: Optional[Path] = None, capture: bool = False,
            check: bool = True, dry_run: bool = False) -> subprocess.CompletedProcess:
    """Run a shell command with logging."""
    cmd_str = " ".join(str(c) for c in cmd)
    if dry_run:
        log(f"[DRY-RUN] Would execute: {cmd_str}", "WARNING")
        return subprocess.CompletedProcess(cmd, 0, stdout="", stderr="")

    log(f"Executing: {cmd_str}")
    result = subprocess.run(
        cmd,
        cwd=cwd,
        capture_output=capture,
        text=True,
        check=check,
    )
    return result


def sha256_file(filepath: Path) -> str:
    """Calculate SHA256 hash of a file."""
    sha256 = hashlib.sha256()
    with open(filepath, "rb") as f:
        for chunk in iter(lambda: f.read(8192), b""):
            sha256.update(chunk)
    return sha256.hexdigest()


def get_file_size(filepath: Path) -> str:
    """Get human-readable file size."""
    size = filepath.stat().st_size
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


def build_macos(project_root: Path, dry_run: bool = False) -> BuildResult:
    """Build for macOS."""
    log("Building for macOS (ARM64)...", "STEP")

    if get_current_platform() != "macos":
        log("macOS builds require a macOS host", "WARNING")
        return BuildResult("macos", False, None, "Requires macOS host")

    build_script = project_root / "scripts" / "build-macos.sh"
    if not build_script.exists():
        return BuildResult("macos", False, None, f"Build script not found: {build_script}")

    try:
        run_cmd(["bash", str(build_script)], cwd=project_root, dry_run=dry_run)
        binary_path = project_root / "build" / "fbfsvg-player"

        if dry_run or binary_path.exists():
            log("macOS build successful", "SUCCESS")
            return BuildResult("macos", True, binary_path)
        else:
            return BuildResult("macos", False, None, "Binary not found after build")
    except subprocess.CalledProcessError as e:
        return BuildResult("macos", False, None, str(e))


def build_linux(project_root: Path, dry_run: bool = False) -> BuildResult:
    """Build for Linux using Docker."""
    log("Building for Linux (x64) via Docker...", "STEP")

    docker_dir = project_root / "docker"

    # Check if Docker is available
    try:
        run_cmd(["docker", "info"], capture=True, dry_run=dry_run)
    except (subprocess.CalledProcessError, FileNotFoundError):
        return BuildResult("linux", False, None, "Docker not available")

    # Build using docker-compose
    try:
        # Ensure container is running
        run_cmd(
            ["docker-compose", "up", "-d"],
            cwd=docker_dir,
            dry_run=dry_run,
        )

        # Execute build inside container
        run_cmd(
            ["docker-compose", "exec", "-T", "dev", "bash", "-c",
             "cd /workspace && ./scripts/build-linux.sh"],
            cwd=docker_dir,
            dry_run=dry_run,
        )

        binary_path = project_root / "build" / "linux" / "fbfsvg-player"

        if dry_run or binary_path.exists():
            log("Linux build successful", "SUCCESS")
            return BuildResult("linux", True, binary_path)
        else:
            return BuildResult("linux", False, None, "Binary not found after build")
    except subprocess.CalledProcessError as e:
        return BuildResult("linux", False, None, str(e))


def build_windows(project_root: Path, dry_run: bool = False) -> BuildResult:
    """Build for Windows."""
    log("Building for Windows (x64)...", "STEP")

    if get_current_platform() != "windows":
        log("Windows builds require a Windows host or CI", "WARNING")
        return BuildResult("windows", False, None, "Requires Windows host")

    build_script = project_root / "scripts" / "build-windows.bat"
    if not build_script.exists():
        return BuildResult("windows", False, None, f"Build script not found: {build_script}")

    try:
        run_cmd(["cmd", "/c", str(build_script)], cwd=project_root, dry_run=dry_run)
        binary_path = project_root / "build" / "windows" / "fbfsvg-player.exe"

        if dry_run or binary_path.exists():
            log("Windows build successful", "SUCCESS")
            return BuildResult("windows", True, binary_path)
        else:
            return BuildResult("windows", False, None, "Binary not found after build")
    except subprocess.CalledProcessError as e:
        return BuildResult("windows", False, None, str(e))


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
    import tarfile

    with tarfile.open(output_path, "w:gz") as tar:
        tar.add(binary_path, arcname=name)

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

    # Create AppRun
    apprun_content = """#!/bin/bash
SELF=$(readlink -f "$0")
HERE=${SELF%/*}
exec "${HERE}/usr/bin/fbfsvg-player" "$@"
"""
    apprun_path = appdir / "AppRun"
    apprun_path.write_text(apprun_content)
    os.chmod(apprun_path, 0o755)

    # Create .desktop file
    desktop_content = """[Desktop Entry]
Name=FBF.SVG Player
Exec=fbfsvg-player
Icon=fbfsvg-player
Type=Application
Categories=Graphics;Viewer;
"""
    (appdir / "fbfsvg-player.desktop").write_text(desktop_content)

    # Create minimal icon (1x1 PNG)
    icon_path = appdir / "fbfsvg-player.png"
    # Minimal valid PNG (1x1 transparent)
    png_data = bytes([
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A,  # PNG signature
        0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,  # IHDR chunk
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,  # 1x1
        0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4,  # RGBA
        0x89, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41,  # IDAT chunk
        0x54, 0x78, 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00,  # compressed data
        0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, 0xB4, 0x00,  #
        0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE,  # IEND chunk
        0x42, 0x60, 0x82,
    ])
    icon_path.write_bytes(png_data)

    # Download appimagetool if not present
    appimagetool = output_dir / "appimagetool-x86_64.AppImage"
    if not appimagetool.exists():
        log("Downloading appimagetool...")
        run_cmd([
            "curl", "-L", "-o", str(appimagetool),
            "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"
        ])
        os.chmod(appimagetool, 0o755)

    # Create AppImage
    output_path = output_dir / f"fbfsvg-player-{version}-x86_64.AppImage"
    env = os.environ.copy()
    env["ARCH"] = "x86_64"

    run_cmd([str(appimagetool), str(appdir), str(output_path)])

    # Cleanup
    shutil.rmtree(appdir)

    return output_path


def create_zip(binary_path: Path, output_path: Path, name: str) -> Path:
    """Create a minimal .zip package."""
    import zipfile

    with zipfile.ZipFile(output_path, "w", zipfile.ZIP_DEFLATED) as zf:
        zf.write(binary_path, name)

    return output_path


def create_packages(build_results: dict[str, BuildResult], output_dir: Path,
                   version: str, dry_run: bool = False) -> list[PackageResult]:
    """Create distribution packages for all successful builds."""
    log("Creating distribution packages...", "STEP")

    packages = []
    output_dir.mkdir(parents=True, exist_ok=True)

    for plat, result in build_results.items():
        if not result.success or result.binary_path is None:
            continue

        if plat == "macos":
            # Create tarball
            asset_name = f"{PROJECT_NAME}-{version}-macos-arm64.tar.gz"
            output_path = output_dir / asset_name

            if not dry_run:
                create_tarball(result.binary_path, output_path, "fbfsvg-player")
                sha = sha256_file(output_path)
                size = get_file_size(output_path)
            else:
                sha, size = "DRY_RUN", "0 B"

            packages.append(PackageResult("macos", "tar.gz", output_path, sha, size))
            log(f"Created {asset_name} ({size})", "SUCCESS")

        elif plat == "linux":
            # Create tarball
            tar_name = f"{PROJECT_NAME}-{version}-linux-x64.tar.gz"
            tar_path = output_dir / tar_name

            if not dry_run:
                create_tarball(result.binary_path, tar_path, "fbfsvg-player")
                sha = sha256_file(tar_path)
                size = get_file_size(tar_path)
            else:
                sha, size = "DRY_RUN", "0 B"

            packages.append(PackageResult("linux", "tar.gz", tar_path, sha, size))
            log(f"Created {tar_name} ({size})", "SUCCESS")

            # Create .deb
            if not dry_run:
                try:
                    deb_path = create_deb_package(result.binary_path, output_dir, version)
                    sha = sha256_file(deb_path)
                    size = get_file_size(deb_path)
                    packages.append(PackageResult("linux", "deb", deb_path, sha, size))
                    log(f"Created {deb_path.name} ({size})", "SUCCESS")
                except Exception as e:
                    log(f"Failed to create .deb: {e}", "WARNING")

            # Create AppImage
            if not dry_run:
                try:
                    appimage_path = create_appimage(result.binary_path, output_dir, version)
                    sha = sha256_file(appimage_path)
                    size = get_file_size(appimage_path)
                    packages.append(PackageResult("linux", "appimage", appimage_path, sha, size))
                    log(f"Created {appimage_path.name} ({size})", "SUCCESS")
                except Exception as e:
                    log(f"Failed to create AppImage: {e}", "WARNING")

        elif plat == "windows":
            # Create zip
            asset_name = f"{PROJECT_NAME}-{version}-windows-x64.zip"
            output_path = output_dir / asset_name

            if not dry_run:
                create_zip(result.binary_path, output_path, "fbfsvg-player.exe")
                sha = sha256_file(output_path)
                size = get_file_size(output_path)
            else:
                sha, size = "DRY_RUN", "0 B"

            packages.append(PackageResult("windows", "zip", output_path, sha, size))
            log(f"Created {asset_name} ({size})", "SUCCESS")

    return packages


# =============================================================================
# GitHub Release Functions
# =============================================================================

def create_checksums_file(packages: list[PackageResult], output_dir: Path) -> Path:
    """Create SHA256SUMS.txt file."""
    checksums_path = output_dir / "SHA256SUMS.txt"
    lines = [f"{pkg.sha256}  {pkg.path.name}" for pkg in packages]
    checksums_path.write_text("\n".join(lines) + "\n")
    return checksums_path


def create_draft_release(version: str, packages: list[PackageResult],
                        checksums_path: Path, dry_run: bool = False) -> Optional[str]:
    """Create a draft release on GitHub and upload assets."""
    log(f"Creating draft release v{version}...", "STEP")

    tag = f"v{version}"
    release_notes = f"""## FBF.SVG Player v{version}

### Downloads

| Platform | Package | Size |
|----------|---------|------|
"""
    for pkg in packages:
        release_notes += f"| {pkg.platform.title()} | [{pkg.path.name}](https://github.com/{GITHUB_REPO}/releases/download/{tag}/{pkg.path.name}) | {pkg.size} |\n"

    release_notes += f"""
### Checksums (SHA256)

```
"""
    for pkg in packages:
        release_notes += f"{pkg.sha256}  {pkg.path.name}\n"
    release_notes += "```\n"

    # Create draft release
    if dry_run:
        log("[DRY-RUN] Would create draft release", "WARNING")
        return "dry-run-release-id"

    # Check if tag exists
    result = run_cmd(
        ["gh", "release", "view", tag],
        capture=True, check=False,
    )

    if result.returncode == 0:
        log(f"Release {tag} already exists, deleting...", "WARNING")
        run_cmd(["gh", "release", "delete", tag, "--yes"])
        run_cmd(["git", "tag", "-d", tag], check=False)
        run_cmd(["git", "push", "origin", f":refs/tags/{tag}"], check=False)

    # Create tag
    run_cmd(["git", "tag", "-a", tag, "-m", f"Release {version}"])
    run_cmd(["git", "push", "origin", tag])

    # Create draft release
    asset_args = []
    for pkg in packages:
        asset_args.extend([str(pkg.path)])
    asset_args.append(str(checksums_path))

    run_cmd([
        "gh", "release", "create", tag,
        "--title", f"FBF.SVG Player {version}",
        "--notes", release_notes,
        "--draft",
        *asset_args,
    ])

    log(f"Draft release {tag} created successfully", "SUCCESS")
    return tag


def validate_release(tag: str, packages: list[PackageResult], dry_run: bool = False) -> bool:
    """Validate release assets by downloading and checking checksums."""
    log(f"Validating release {tag}...", "STEP")

    if dry_run:
        log("[DRY-RUN] Would validate release assets", "WARNING")
        return True

    with tempfile.TemporaryDirectory() as tmpdir:
        tmppath = Path(tmpdir)

        # Download all assets
        run_cmd([
            "gh", "release", "download", tag,
            "--dir", str(tmppath),
        ])

        # Verify checksums
        for pkg in packages:
            downloaded = tmppath / pkg.path.name
            if not downloaded.exists():
                log(f"Asset not found: {pkg.path.name}", "ERROR")
                return False

            actual_sha = sha256_file(downloaded)
            if actual_sha != pkg.sha256:
                log(f"Checksum mismatch for {pkg.path.name}", "ERROR")
                log(f"  Expected: {pkg.sha256}", "ERROR")
                log(f"  Actual:   {actual_sha}", "ERROR")
                return False

            log(f"Verified: {pkg.path.name}", "SUCCESS")

        # Verify archives can be extracted
        for pkg in packages:
            downloaded = tmppath / pkg.path.name
            if pkg.format == "tar.gz":
                run_cmd(["tar", "-tzf", str(downloaded)], capture=True)
            elif pkg.format == "zip":
                run_cmd(["unzip", "-t", str(downloaded)], capture=True)
            elif pkg.format == "deb":
                run_cmd(["dpkg", "--contents", str(downloaded)], capture=True)

    log("All assets validated successfully", "SUCCESS")
    return True


def publish_release(tag: str, dry_run: bool = False) -> bool:
    """Convert draft release to public release."""
    log(f"Publishing release {tag}...", "STEP")

    if dry_run:
        log("[DRY-RUN] Would publish release", "WARNING")
        return True

    run_cmd(["gh", "release", "edit", tag, "--draft=false"])
    log(f"Release {tag} published successfully", "SUCCESS")
    return True


# =============================================================================
# Package Manifest Update Functions
# =============================================================================

def update_homebrew_formula(project_root: Path, version: str,
                           packages: list[PackageResult], dry_run: bool = False):
    """Update Homebrew formula with new version and checksum."""
    log("Updating Homebrew formulas...", "STEP")

    # Find macOS package
    macos_pkg = next((p for p in packages if p.platform == "macos" and p.format == "tar.gz"), None)
    linux_pkg = next((p for p in packages if p.platform == "linux" and p.format == "tar.gz"), None)

    # Update macOS formula
    if macos_pkg:
        formula_path = project_root / "Formula" / "fbfsvg-player.rb"
        if formula_path.exists():
            content = formula_path.read_text()

            # Update URL
            content = re.sub(
                r'url "https://github\.com/Emasoft/fbfsvg-player/releases/download/v[\d.]+/.*\.tar\.gz"',
                f'url "https://github.com/Emasoft/fbfsvg-player/releases/download/v{version}/{macos_pkg.path.name}"',
                content
            )

            # Update SHA256
            content = re.sub(
                r'sha256 "[a-f0-9]+"',
                f'sha256 "{macos_pkg.sha256}"',
                content
            )

            # Update version
            content = re.sub(
                r'version "[\d.]+"',
                f'version "{version}"',
                content
            )

            if not dry_run:
                formula_path.write_text(content)
            log(f"Updated {formula_path.name}", "SUCCESS")

    # Update Linux formula
    if linux_pkg:
        formula_path = project_root / "Formula" / "fbfsvg-player@linux.rb"
        if formula_path.exists():
            content = formula_path.read_text()

            # Update URL
            content = re.sub(
                r'url "https://github\.com/Emasoft/fbfsvg-player/releases/download/v[\d.]+/.*\.tar\.gz"',
                f'url "https://github.com/Emasoft/fbfsvg-player/releases/download/v{version}/{linux_pkg.path.name}"',
                content
            )

            # Update SHA256
            content = re.sub(
                r'sha256 "[a-f0-9]+"',
                f'sha256 "{linux_pkg.sha256}"',
                content
            )

            # Update version
            content = re.sub(
                r'version "[\d.]+"',
                f'version "{version}"',
                content
            )

            if not dry_run:
                formula_path.write_text(content)
            log(f"Updated {formula_path.name}", "SUCCESS")


def update_scoop_manifest(project_root: Path, version: str,
                         packages: list[PackageResult], dry_run: bool = False):
    """Update Scoop manifest with new version and checksum."""
    log("Updating Scoop manifest...", "STEP")

    # Find Windows package
    win_pkg = next((p for p in packages if p.platform == "windows" and p.format == "zip"), None)

    manifest_path = project_root / "bucket" / "fbfsvg-player.json"
    if not manifest_path.exists():
        log(f"Scoop manifest not found: {manifest_path}", "WARNING")
        return

    manifest = json.loads(manifest_path.read_text())

    # Update version
    manifest["version"] = version

    # Update URL and hash
    if win_pkg:
        manifest["architecture"]["64bit"]["url"] = \
            f"https://github.com/{GITHUB_REPO}/releases/download/v{version}/{win_pkg.path.name}"
        manifest["architecture"]["64bit"]["hash"] = win_pkg.sha256
    else:
        manifest["architecture"]["64bit"]["url"] = \
            f"https://github.com/{GITHUB_REPO}/releases/download/v{version}/{PROJECT_NAME}-{version}-windows-x64.zip"
        manifest["architecture"]["64bit"]["hash"] = "PENDING_WINDOWS_BUILD"

    if not dry_run:
        manifest_path.write_text(json.dumps(manifest, indent=4) + "\n")
    log(f"Updated {manifest_path.name}", "SUCCESS")


def commit_manifest_updates(project_root: Path, version: str, dry_run: bool = False):
    """Commit and push manifest updates."""
    log("Committing manifest updates...", "STEP")

    if dry_run:
        log("[DRY-RUN] Would commit manifest updates", "WARNING")
        return

    run_cmd(["git", "add", "Formula/", "bucket/"], cwd=project_root)

    result = run_cmd(
        ["git", "diff", "--cached", "--quiet"],
        cwd=project_root, check=False,
    )

    if result.returncode != 0:  # Changes exist
        run_cmd([
            "git", "commit", "-m",
            f"Update package manifests for v{version}\n\n- Update Homebrew formulas\n- Update Scoop manifest"
        ], cwd=project_root)
        run_cmd(["git", "push", "origin", "main"], cwd=project_root)
        log("Manifest updates committed and pushed", "SUCCESS")
    else:
        log("No manifest changes to commit", "INFO")


# =============================================================================
# Main Release Workflow
# =============================================================================

def run_release(version: str, platforms: Optional[list[str]] = None,
               skip_build: bool = False, dry_run: bool = False):
    """Run the complete release workflow."""
    project_root = Path(__file__).parent.parent.resolve()
    release_dir = project_root / "release"

    log(f"Starting release workflow for v{version}", "STEP")
    log(f"Project root: {project_root}")
    log(f"Release directory: {release_dir}")

    # Validate version
    if not validate_version(version):
        log(f"Invalid version format: {version}", "ERROR")
        log("Expected format: X.Y.Z or X.Y.Z-suffix", "ERROR")
        sys.exit(1)

    # Determine platforms to build
    if platforms is None:
        platforms = ["macos", "linux", "windows"]

    current_platform = get_current_platform()
    log(f"Current platform: {current_platform}")
    log(f"Target platforms: {', '.join(platforms)}")

    # Clean release directory
    if release_dir.exists() and not dry_run:
        shutil.rmtree(release_dir)
    release_dir.mkdir(parents=True, exist_ok=True)

    # Step 1: Build
    build_results = {}
    if not skip_build:
        log("=" * 60)
        log("STEP 1: Building for all platforms", "STEP")
        log("=" * 60)

        for plat in platforms:
            if plat == "macos":
                build_results["macos"] = build_macos(project_root, dry_run)
            elif plat == "linux":
                build_results["linux"] = build_linux(project_root, dry_run)
            elif plat == "windows":
                build_results["windows"] = build_windows(project_root, dry_run)

        # Report build results
        log("\nBuild Results:")
        for plat, result in build_results.items():
            status = "SUCCESS" if result.success else "FAILED"
            log(f"  {plat}: {status}", "SUCCESS" if result.success else "ERROR")
            if result.error:
                log(f"    Error: {result.error}", "ERROR")
    else:
        log("Skipping build step (--skip-build)", "WARNING")
        # Assume existing binaries
        for plat in platforms:
            binary: Optional[Path] = None
            if plat == "macos":
                binary = project_root / "build" / "fbfsvg-player"
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
        sys.exit(1)

    # Step 2: Create packages
    log("=" * 60)
    log("STEP 2: Creating distribution packages", "STEP")
    log("=" * 60)

    packages = create_packages(build_results, release_dir, version, dry_run)

    if not packages and not dry_run:
        log("No packages created, aborting release", "ERROR")
        sys.exit(1)

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
        sys.exit(1)

    # Step 4: Validate release
    log("=" * 60)
    log("STEP 4: Validating release assets", "STEP")
    log("=" * 60)

    if not validate_release(tag, packages, dry_run):
        log("Release validation failed", "ERROR")
        log("Draft release preserved for manual inspection", "WARNING")
        sys.exit(1)

    # Step 5: Publish release
    log("=" * 60)
    log("STEP 5: Publishing release", "STEP")
    log("=" * 60)

    if not publish_release(tag, dry_run):
        log("Failed to publish release", "ERROR")
        sys.exit(1)

    # Step 6: Update package manifests
    log("=" * 60)
    log("STEP 6: Updating package manifests", "STEP")
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


# =============================================================================
# CLI Entry Point
# =============================================================================

def main():
    parser = argparse.ArgumentParser(
        description="Automate fbfsvg-player releases for all platforms",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python3 scripts/release.py --version 0.2.0
  python3 scripts/release.py --version 0.2.0 --dry-run
  python3 scripts/release.py --version 0.2.0 --skip-build
  python3 scripts/release.py --version 0.2.0 --platform macos linux
        """
    )

    parser.add_argument(
        "--version", "-v",
        required=True,
        help="Version number (e.g., 0.2.0, 1.0.0-beta)",
    )

    parser.add_argument(
        "--platform", "-p",
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

    args = parser.parse_args()

    try:
        run_release(
            version=args.version,
            platforms=args.platform,
            skip_build=args.skip_build,
            dry_run=args.dry_run,
        )
    except KeyboardInterrupt:
        log("\nRelease aborted by user", "WARNING")
        sys.exit(1)
    except subprocess.CalledProcessError as e:
        log(f"Command failed: {e}", "ERROR")
        sys.exit(1)
    except Exception as e:
        log(f"Release failed: {e}", "ERROR")
        raise


if __name__ == "__main__":
    main()
