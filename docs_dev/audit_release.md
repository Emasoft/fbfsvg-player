# Release Workflow Audit Report
**Generated:** 2026-01-24  
**Scope:** `scripts/release.py`, `.github/workflows/release.yml`, package manifests

---

## Executive Summary

The project has **two independent release systems**:
1. **Local script**: `scripts/release.py` (Python, runs on developer machine)
2. **GitHub Actions**: `.github/workflows/release.yml` (YAML, runs on GitHub runners)

Both implement similar workflows but with **different platform coverage** and **no shared code**.

**Status**: ✅ Core workflow functional, ⚠️ Gaps in platform coverage and error handling

---

## 1. Release Workflow Steps

### Local Script (`release.py`)

```
Step 1:  Pre-flight checks (deps, Skia, disk space, git status, Rosetta)
Step 2:  Version validation (semver format)
Step 3:  Build all platforms (parallel macOS builds, Docker Linux, Windows)
Step 4:  Create packages (tar.gz, deb, AppImage, zip)
Step 5:  Generate SHA256 checksums
Step 6:  Create DRAFT GitHub release
Step 7:  Upload all assets + checksums
Step 8:  Validate uploaded assets
Step 9:  Interactive confirmation (type "publish" to confirm)
Step 10: Publish release (remove draft status)
Step 11: Update package manifests (Homebrew, Scoop)
Step 12: Commit + push manifest changes
```

**Key Features:**
- ✅ Parallel macOS builds (ARM64 + x64 simultaneously via ThreadPoolExecutor)
- ✅ Cross-compilation support (x64 on ARM64 via Rosetta 2)
- ✅ Binary architecture verification (`lipo -info` on macOS, `file` on Linux)
- ✅ Network retry logic (3 retries with 5s delay for GitHub API calls)
- ✅ Dry-run mode (`--dry-run` flag)
- ✅ Interactive safety confirmation before publish
- ✅ Detailed logging to timestamped file (`release/release_YYYYMMDD_HHMMSS.log`)

### GitHub Actions Workflow

```
Stage 1: Build (parallel matrix: macOS-14 ARM64, Ubuntu-latest, Windows-latest)
Stage 2: Package + create DRAFT release
Stage 3: Validate (download assets, verify checksums, test Linux binary)
Stage 4: Publish (remove draft status, update release notes)
Stage 5: Update package manifests (Homebrew, Scoop)
```

**Key Features:**
- ✅ Skia caching (speeds up builds by ~30 min)
- ✅ Checksum verification in CI
- ✅ Archive content validation (tar.gz, deb, zip)
- ✅ Linux binary execution test (smoke test)
- ✅ Automatic manifest updates via GitHub Actions bot

---

## 2. Platform Coverage

| Platform | Local Script | GitHub Actions | Package Formats |
|----------|--------------|----------------|-----------------|
| **macOS ARM64** | ✅ Parallel build | ✅ Native (macos-14) | tar.gz |
| **macOS x64** | ✅ Parallel build (Rosetta) | ❌ Not built | tar.gz |
| **Linux x64** | ✅ Via Docker | ✅ Native (ubuntu-latest) | tar.gz, deb, AppImage |
| **Windows x64** | ⚠️ Requires Windows host | ✅ Native (windows-latest) | zip |
| **iOS XCFramework** | ❌ Not packaged | ❌ Not packaged | N/A |
| **Linux SDK (.so)** | ❌ Not packaged | ❌ Not packaged | N/A |

### Issues:

1. **macOS x64 missing from GitHub Actions**  
   - Workflow only builds ARM64 (`macos-14` is ARM64-only runner)
   - No `macos-13` (Intel) runner in matrix
   - Local script handles x64 via Rosetta, but CI doesn't

2. **iOS/SDK artifacts excluded**  
   - iOS XCFramework built by `make ios-framework` but not released
   - Linux SDK (.so) built by `make linux-sdk` but not released
   - No packaging logic for frameworks

3. **Windows cross-compile not supported**  
   - Local script requires Windows host (no MSVC cross-compile)
   - GitHub Actions builds natively on Windows runner

4. **AppImage generation fragile**  
   - Requires `appimagetool` binary (not installed by default)
   - Fallback creates tarball instead of actual AppImage
   - No verification if fallback occurred

---

## 3. Package Creation

### Implemented Formats

| Format | Platforms | Creation Function | Notes |
|--------|-----------|-------------------|-------|
| **tar.gz** | macOS, Linux | `create_tarball()` | ✅ Minimal, single binary |
| **.deb** | Linux | `create_deb_package()` | ✅ Debian control file, dpkg-deb |
| **AppImage** | Linux | `create_appimage()` | ⚠️ Fallback to tarball if appimagetool missing |
| **.zip** | Windows | `create_zip_package()` | ✅ Single binary |

### Package Contents

**macOS/Linux tar.gz:**
```
fbfsvg-player-0.2.0-macos-arm64.tar.gz
└── fbfsvg-player  (binary)
```

**Linux .deb:**
```
fbfsvg-player_0.2.0_amd64.deb
├── DEBIAN/control  (metadata)
└── usr/bin/fbfsvg-player  (binary with 0755 perms)
```

**AppImage (when appimagetool available):**
```
fbfsvg-player-0.2.0-x86_64.AppImage
├── AppRun  (launcher script)
├── fbfsvg-player.desktop  (desktop entry)
├── fbfsvg-player.png  (1x1 placeholder icon)
└── usr/bin/fbfsvg-player  (binary)
```

**Windows .zip:**
```
fbfsvg-player-0.2.0-windows-x64.zip
└── fbfsvg-player.exe  (binary)
```

### Missing Package Features

❌ No man pages  
❌ No desktop integration files (Linux .desktop, macOS .app bundle)  
❌ No license files in packages  
❌ No README/CHANGELOG in packages  
❌ No debug symbol packages (.dSYM, .pdb)  
❌ No code signing (macOS notarization, Windows Authenticode)  
❌ No installer packages (.dmg, .pkg, .msi)  

---

## 4. GitHub Release Creation

### Draft Release Workflow

```python
# 1. Check if tag exists (retry 3x)
gh release view v0.2.0

# 2. Delete existing if found
gh release delete v0.2.0 --yes

# 3. Create new draft release
gh release create v0.2.0 \
  --draft \
  --title "fbfsvg-player v0.2.0" \
  --notes-file /tmp/release_notes.md \
  package1.tar.gz \
  package2.deb \
  SHA256SUMS.txt
```

### Release Notes Generation

**Template:**
```markdown
## fbfsvg-player v0.2.0

High-performance animated SVG player for the FBF.SVG vector video format.

### Downloads

| Platform | Download | Size |
|----------|----------|------|
| macOS (Apple Silicon) | [fbfsvg-player-v0.2.0-macos-arm64.tar.gz] | 12.5 MB |
| Linux (x64) | [fbfsvg-player-v0.2.0-linux-x64.tar.gz] | 8.3 MB |

### Checksums (SHA256)
<checksums here>

### Installation
<Homebrew/APT/Scoop instructions>
```

**Generated Dynamically From:**
- Platform display names from `PLATFORMS` config
- Asset names and sizes from `PackageResult` objects
- SHA256 hashes from `sha256_file()` function

### Validation Steps

✅ **Local script validation:**
```python
# Download release JSON via gh CLI
gh release view v0.2.0 --json assets

# Verify all expected assets present
expected = {pkg.path.name for pkg in packages}
expected.add("SHA256SUMS.txt")
uploaded = {asset["name"] for asset in release["assets"]}
assert expected == uploaded
```

✅ **GitHub Actions validation:**
```bash
# Download all assets
curl -fsSL -o "$asset" "https://github.com/.../releases/download/$VERSION/$asset"

# Verify checksums
sha256sum -c SHA256SUMS.txt

# Validate archive contents
tar -tzf macos.tar.gz | grep -q "fbfsvg-player"
dpkg-deb -c linux.deb | grep -q "usr/bin/fbfsvg-player"
unzip -l windows.zip | grep -q ".exe"

# Test Linux binary execution
./fbfsvg-player --help  # Smoke test
```

---

## 5. Manifest Updates

### Homebrew Formula (macOS)

**File:** `Formula/fbfsvg-player.rb`

**Update Strategy:**
```ruby
# BEFORE (release.py uses on_arm/on_intel blocks):
class FbfsvgPlayer < Formula
  version "0.1.0"
  
  on_arm do
    url "https://.../v0.1.0/fbfsvg-player-0.1.0-macos-arm64.tar.gz"
    sha256 "abc123..."
  end
  
  on_intel do
    url "https://.../v0.1.0/fbfsvg-player-0.1.0-macos-x64.tar.gz"
    sha256 "def456..."
  end
end

# AFTER (GitHub Actions overwrites entire file, ARM64 only):
class FbfsvgPlayer < Formula
  version "0.2.0"
  url "https://.../v0.2.0/fbfsvg-player-0.2.0-macos-arm64.tar.gz"
  sha256 "xyz789..."
  depends_on arch: :arm64  # Restricts to ARM64 only!
end
```

**⚠️ CONFLICT DETECTED:**
- **Local script** preserves dual-arch support (on_arm/on_intel)
- **GitHub Actions** overwrites with ARM64-only formula
- **Result:** Intel Macs lose Homebrew support after CI release

### Linuxbrew Formula

**File:** `Formula/fbfsvg-player@linux.rb`

**Update Strategy:**
```ruby
class FbfsvgPlayerAtLinux < Formula
  version "0.2.0"
  url "https://.../fbfsvg-player-0.2.0-linux-x64.tar.gz"
  sha256 "linux_sha256_here"
  
  depends_on :linux
  depends_on arch: :x86_64
end
```

### Scoop Manifest (Windows)

**File:** `bucket/fbfsvg-player.json`

**Update Strategy:**
```json
{
  "version": "0.2.0",
  "architecture": {
    "64bit": {
      "url": "https://.../fbfsvg-player-0.2.0-windows-x64.zip",
      "hash": "windows_sha256_here"
    }
  },
  "autoupdate": {
    "architecture": {
      "64bit": {
        "url": "https://.../fbfsvg-player-v$version-windows-x64.zip"
      }
    }
  }
}
```

### Commit Strategy

**Local script:**
```bash
git add Formula/ bucket/
git commit -m "Update package manifests for v0.2.0

- Update Homebrew formulas
- Update Scoop manifest"
git push origin main
```

**GitHub Actions:**
```bash
git config user.name "GitHub Actions"
git config user.email "actions@github.com"
git add Formula/ bucket/
git commit -m "Update package manifests to v0.2.0" || echo "No changes"
git push
```

---

## 6. Error Handling and Rollback

### Error Handling Mechanisms

| Failure Point | Local Script | GitHub Actions | Rollback |
|---------------|--------------|----------------|----------|
| **Pre-flight check fails** | ❌ Exit immediately | ⚠️ fail-fast: false (continues) | N/A |
| **Build fails** | ✅ Continue with successful builds | ✅ Matrix continues | ❌ No cleanup |
| **Package creation fails** | ✅ Log error, skip platform | ⚠️ Job fails, halts workflow | ❌ No cleanup |
| **Draft release creation fails** | ❌ Exit, no cleanup | ❌ Job fails | ❌ Orphaned artifacts |
| **Asset upload fails** | ✅ Retry 3x with 5s delay | ❌ Single attempt | ⚠️ Draft preserved |
| **Validation fails** | ⚠️ Draft preserved, exit | ⚠️ Draft preserved, exit | ✅ Manual inspection |
| **User cancels publish** | ✅ Draft preserved, exit | N/A | ✅ Draft preserved |
| **Publish fails** | ❌ No rollback, release broken | ❌ No rollback | ❌ No rollback |
| **Manifest update fails** | ❌ Release public, manifests stale | ❌ Job fails, stale manifests | ❌ No rollback |

### Rollback Gaps

❌ **No rollback if publish succeeds but manifest update fails**  
   - Release is public, but Homebrew/Scoop point to old version
   - Manual intervention required

❌ **No transaction mechanism**  
   - Operations are not atomic
   - Half-published releases possible

❌ **No cleanup of failed builds**  
   - Failed build artifacts remain in `build/` directory
   - No `release/` cleanup on error

❌ **No tag cleanup on failure**  
   - Git tag remains even if release fails
   - GitHub release draft remains orphaned

### Retry Logic

✅ **Network operations** (3 retries, 5s delay):
```python
run_cmd(["gh", "release", "view", tag], retry=NETWORK_RETRY_COUNT)
```

❌ **Build operations** (no retry):
- If Skia build fails, script exits
- No retry for transient failures

❌ **Packaging operations** (no retry):
- AppImage fallback silently creates tarball instead

---

## 7. Dry-Run Capabilities

### Local Script (`--dry-run`)

✅ **What is simulated:**
```bash
python3 scripts/release.py --version 0.2.0 --dry-run
```

- ✅ All bash commands logged but not executed
- ✅ File paths generated but files not created (empty touch)
- ✅ GitHub API calls skipped (fake release ID returned)
- ✅ Checksums set to `"DRY_RUN_CHECKSUM"`
- ✅ Size set to `"0 B"`
- ✅ Full workflow steps logged

**Output:**
```
[INFO] Starting release workflow for v0.2.0
[WARNING] [DRY-RUN] Would execute: bash build-macos-arch.sh arm64
[WARNING] [DRY-RUN] Would create draft release
[WARNING] [DRY-RUN] Would validate release assets
[WARNING] [DRY-RUN] Would publish release
[WARNING] [DRY-RUN] Would commit manifest updates
```

### GitHub Actions

❌ **No dry-run mode**  
- Workflow always executes fully when triggered
- Only way to test: create test tag on fork

### Missing Dry-Run Features

❌ **No partial dry-run**  
   - Can't dry-run only packaging without builds
   - Can't dry-run only manifest updates

❌ **No diff preview**  
   - Doesn't show what would change in manifests
   - No preview of release notes

---

## 8. Missing Features

### Critical Gaps

1. **iOS XCFramework not released**
   - Built via `make ios-framework` → `build/SVGPlayer.xcframework/`
   - No packaging logic in release.py
   - Developers must manually download from GitHub repo

2. **Linux SDK not released**
   - Built via `make linux-sdk` → `build/linux/libsvgplayer.so`
   - No .tar.gz or .deb package for SDK
   - C headers not included in release

3. **macOS x64 missing from CI**
   - GitHub Actions only builds ARM64
   - Intel Mac users must use local script
   - Homebrew formula gets overwritten to ARM64-only

4. **No version bump automation**
   - Manual version string updates in:
     - `CLAUDE.md`
     - `README.md`
     - `shared/svg_player_api.h` (VERSION constant)
   - No `bump-version.sh` script

5. **No changelog generation**
   - Release notes are template-based
   - No git log parsing for changes since last tag
   - No categorization (Features, Fixes, Breaking Changes)

6. **No code signing**
   - macOS binaries not signed/notarized
   - Windows binaries not Authenticode signed
   - Users get security warnings

7. **No security scanning**
   - No virus scanning of binaries
   - No dependency vulnerability checks
   - No supply chain verification

8. **No artifact retention policy**
   - GitHub Actions artifacts kept 5 days
   - No long-term storage strategy
   - Old releases not pruned

9. **No release announcement automation**
   - No Discord/Slack notifications
   - No Twitter/social media posts
   - No blog post generation

10. **No rollback mechanism**
    - Can't unpublish a broken release atomically
    - Must manually delete release + fix manifests

### Nice-to-Have Features

- Multi-architecture Linux builds (ARM64)
- Snap/Flatpak packages
- Chocolatey manifest (Windows)
- AUR package (Arch Linux)
- Binary size optimization (strip symbols for release)
- Release notes from CHANGELOG.md
- Automated smoke tests before publish
- Performance regression checks
- Documentation updates in release
- Example SVGs included in release packages

---

## 9. Recommendations

### Immediate Fixes (P0)

1. **Add macOS x64 to GitHub Actions**
   ```yaml
   matrix:
     include:
       - os: macos-14  # ARM64
       - os: macos-13  # Intel x64
   ```

2. **Fix Homebrew formula conflict**
   - GitHub Actions should preserve on_arm/on_intel blocks
   - Use regex to update both sections independently

3. **Package iOS XCFramework**
   ```bash
   tar -czvf SVGPlayer-xcframework-0.2.0.tar.gz \
     build/SVGPlayer.xcframework/
   ```

4. **Add rollback script**
   ```bash
   scripts/rollback-release.sh --version 0.2.0
   # Deletes release, reverts manifest commits, deletes tag
   ```

### High Priority (P1)

5. **Add version bump script**
   ```bash
   scripts/bump-version.sh 0.3.0
   # Updates all version strings, commits changes
   ```

6. **Generate changelog from git**
   ```python
   # Parse commits since last tag
   git log v0.1.0..HEAD --oneline --no-merges
   # Categorize by prefix: feat:, fix:, docs:, etc.
   ```

7. **Add code signing**
   - macOS: `codesign` + `notarytool`
   - Windows: `signtool` (requires cert)

### Medium Priority (P2)

8. **Security scanning**
   ```yaml
   - name: Scan binaries
     uses: aquasecurity/trivy-action@master
   ```

9. **Smoke tests in CI**
   ```bash
   # Test each binary before release
   ./fbfsvg-player --version
   ./fbfsvg-player test.svg --frames 1
   ```

10. **Release announcement automation**
    - Post to GitHub Discussions
    - Update project website
    - Send email to mailing list

### Low Priority (P3)

11. Multi-architecture Linux (ARM64)
12. Additional package formats (Snap, Flatpak)
13. Binary size optimization (UPX compression)
14. Delta updates (binary diff patches)

---

## 10. Comparison: Local vs CI

| Feature | Local Script | GitHub Actions | Winner |
|---------|--------------|----------------|--------|
| **Platform coverage** | ⚠️ macOS, Linux (Docker), Windows (manual) | ✅ macOS ARM64, Linux, Windows (native) | CI |
| **Parallel builds** | ✅ macOS ARM64+x64 simultaneous | ✅ All platforms simultaneous | Tie |
| **Pre-flight checks** | ✅ Comprehensive (Skia, Docker, Rosetta) | ❌ None | Local |
| **Dry-run** | ✅ Full simulation | ❌ Not supported | Local |
| **Interactive confirmation** | ✅ Type "publish" to confirm | ❌ Auto-publish after validation | Local |
| **Network retry** | ✅ 3 retries on gh CLI failures | ❌ Single attempt | Local |
| **Error handling** | ✅ Continues on partial failure | ⚠️ fail-fast: false (messy) | Local |
| **Logging** | ✅ Timestamped log file + console | ⚠️ GitHub Actions logs (hard to search) | Local |
| **Validation** | ✅ Asset upload verification | ✅ Checksum + content + smoke test | CI |
| **Manifest updates** | ✅ Preserves dual-arch Homebrew | ❌ Overwrites to ARM64-only | Local |
| **Caching** | ❌ No Skia cache | ✅ Skia cached (30 min savings) | CI |
| **Reproducibility** | ⚠️ Depends on host env | ✅ Hermetic GitHub runners | CI |

**Recommendation:** Use **local script for releases** (more control), use **CI for validation** (hermetic testing)

---

## Conclusion

The release infrastructure is **functional but incomplete**:

✅ **Strengths:**
- Comprehensive local automation (release.py)
- Draft-first approach prevents accidents
- Good validation (checksums, content, smoke tests)
- Parallel builds optimize time

⚠️ **Weaknesses:**
- iOS/SDK not packaged
- macOS x64 missing from CI
- Homebrew formula conflict between local/CI
- No rollback mechanism
- No code signing
- No changelog automation

**Overall Grade:** B- (functional core, missing polish)

**Priority Actions:**
1. Add macOS x64 to CI matrix
2. Fix Homebrew formula update logic
3. Package iOS XCFramework
4. Add rollback script
5. Implement version bump automation
