# Docker Environment Audit Report
Generated: 2026-01-24

## Executive Summary

The `docker/` directory provides a comprehensive Linux development environment for building Skia and the fbfsvg-player SDK. It supports **multi-architecture** (ARM64 and x86_64) builds, **GPU acceleration** for benchmarking, and includes all necessary dependencies pre-installed.

**Key Features:**
- Ubuntu 24.04 base with Clang as the default compiler
- Multi-architecture support (linux/arm64, linux/amd64)
- Persistent development setup with volume mounts
- Dedicated benchmark suite with ThorVG comparison
- Test runner services for CI/CD integration

---

## 1. Dockerfile Analysis

**Location:** `docker/Dockerfile`  
**Base Image:** `ubuntu:24.04`  
**Build Syntax:** `docker/dockerfile:1` (BuildKit syntax)

### Installed Components

| Category | Packages | Purpose |
|----------|----------|---------|
| **Core Build Tools** | build-essential, clang, lld, cmake, ninja, python3, git | Compilation and build orchestration |
| **Graphics Libraries** | EGL, GLES2, OpenGL, X11, Wayland, Mesa | GPU rendering support |
| **Font Libraries** | FreeType, FontConfig, HarfBuzz, DejaVu fonts | Text rendering |
| **Image Libraries** | libpng, libjpeg-turbo, libwebp, libgif | Image format support |
| **Compression** | zlib, lz4, zstd | Data compression |
| **Other** | expat (XML), ICU (Unicode), SDL2 (multimedia) | Framework dependencies |
| **Dev Tools** | gdb, valgrind, strace, clang-format, clang-tidy, doxygen | Debugging and quality assurance |

### Security Considerations

✓ **VERIFIED:**
- Runs as non-root user `developer` (UID 1000)
- User has sudo access via `/etc/sudoers.d/` (NOPASSWD for dev convenience)
- Uses `--no-install-recommends` to minimize attack surface
- Cleans up apt cache after each install (removes package lists)

⚠️ **POTENTIAL RISKS:**
- NOPASSWD sudo allows privilege escalation inside container (acceptable for dev environment, NOT for production)
- No AppArmor/SELinux profiles defined
- Git safe.directory set to `*` (allows any repo, prevents ownership errors with volume mounts)

### Environment Configuration

```bash
CC=clang
CXX=clang++
DEBIAN_FRONTEND=noninteractive
TZ=UTC
LANG=C.UTF-8
LC_ALL=C.UTF-8
PATH=/workspace/skia-build/depot_tools:${PATH}
```

**Rationale for Clang:**
- Skia developers recommend Clang for better performance and compatibility
- Set via `update-alternatives` to make Clang the system default

### User Setup

- **Username:** `developer` (configurable via build arg)
- **UID/GID:** 1000 (matches typical Linux host user)
- **Home:** `/home/developer`
- **Workdir:** `/workspace`
- **Persistent Directories:**
  - `/home/developer/.cache`
  - `/home/developer/.config`

**Smart User Creation Logic:**
- Checks if UID 1000 already exists (Ubuntu 24.04 ships with `ubuntu` user)
- If exists, renames to `developer` to avoid conflicts
- If not exists, creates new user with specified UID/GID

### Health Check Script

**Included:** `healthcheck.sh` (installed to `/usr/local/bin/`)  
**Purpose:** Verifies all build dependencies are correctly installed

**Checks:**
- Compilers: clang, gcc, clang++, g++
- Build tools: make, ninja, cmake, pkg-config, python3, git
- Graphics libraries: EGL, OpenGL, GLES2
- Font libraries: FreeType, FontConfig, HarfBuzz
- Image libraries: libpng, libjpeg, libwebp
- Other libraries: zlib, expat, ICU

**Exit Codes:**
- 0 = All checks passed
- 1 = One or more errors

---

## 2. docker-compose.yml Analysis

**Location:** `docker/docker-compose.yml`  
**Compose Version:** v2 (implied by syntax)

### Service Architecture

```
┌─────────────────────────────────────────────────────────┐
│                       Services                          │
├──────────────┬──────────────┬──────────────┬───────────┤
│  dev-arm64   │  dev-x64     │  dev (alias) │  test-*   │
│  (native on  │  (QEMU       │  (points to  │  (CI/CD   │
│  Apple       │  emulation)  │  dev-arm64)  │  runners) │
│  Silicon)    │              │              │           │
└──────────────┴──────────────┴──────────────┴───────────┘
```

### Multi-Architecture Strategy

**ARM64 Service (`dev-arm64`):**
- Platform: `linux/arm64`
- Image: `svgplayer-linux-dev:arm64`
- Container: `svgplayer-dev-arm64`
- Use Case: Native builds on Apple Silicon Macs

**x86_64 Service (`dev-x64`):**
- Platform: `linux/amd64`
- Image: `svgplayer-linux-dev:x64`
- Container: `svgplayer-dev-x64`
- Use Case: x86_64 builds via QEMU emulation (slower but cross-platform)

**Legacy Alias (`dev`):**
- Extends: `dev-arm64`
- Purpose: Backward compatibility with older documentation

### Volume Mounts

| Host Path | Container Path | Mode | Purpose |
|-----------|----------------|------|---------|
| `..` (project root) | `/workspace` | `cached` | Source code (read-write) |
| `~/.gitconfig` | `/home/developer/.gitconfig` | `ro` | Git identity (read-only) |
| Named volume | `/home/developer/.bash_history_dir` | `rw` | Command history persistence |

**Volume Caching Mode:** `cached`  
- Optimizes for host-to-container read performance
- Acceptable write delay for better speed on macOS

**Security Note:**
- `.gitconfig` mounted read-only to prevent accidental modification
- Project files are read-write (necessary for builds)

### Resource Limits

| Resource | Limit | Reservation | Notes |
|----------|-------|-------------|-------|
| CPUs | 8 cores | 4 cores | Max parallelism for builds |
| Memory | 12GB | 6GB | Skia builds are memory-intensive |
| Shared Memory | 2GB | N/A | For parallel compilation |

**Why These Limits:**
- Skia build can use 30-60 minutes with 8 cores
- Memory limit prevents host thrashing during ninja builds
- Shared memory (`shm_size: 2gb`) required for parallel jobs

### Environment Variables

```bash
TERM=xterm-256color          # Terminal color support
CC=clang                     # C compiler
CXX=clang++                  # C++ compiler
SKIA_BUILD_DIR=/workspace/skia-build
SKIA_OUT_DIR=release-linux-{arm64|x64}  # Architecture-specific
TARGET_ARCH={arm64|x64}
PATH=/workspace/skia-build/depot_tools:/workspace/skia-build/src/skia/bin:...
```

### Restart Policy

`restart: unless-stopped`

- Container restarts automatically on Docker daemon restart
- Does NOT restart if manually stopped via `docker-compose down`
- Ideal for persistent development environments

### Test Runner Services

**Purpose:** CI/CD test execution (ephemeral containers)

**Key Differences from Dev Services:**
- No TTY/stdin (non-interactive)
- Lower resource limits (4 cores, 8GB RAM)
- No restart policy
- Explicit test command instead of shell

---

## 3. entrypoint.sh Analysis

**Location:** `docker/entrypoint.sh`  
**Permissions:** `0755` (executable)  
**Purpose:** Container initialization and user guidance

### Initialization Flow

```
1. Display welcome banner
2. Check /workspace mount exists
3. Detect and configure depot_tools
4. Add Skia bin to PATH
5. Display environment info
6. Show quick start help
7. Execute user command (default: /bin/bash)
```

### Safety Checks

✓ **VERIFIED:**
- `set -e` enabled (fail-fast on errors)
- Verifies `/workspace` exists before proceeding
- Checks for depot_tools in multiple locations:
  - `/workspace/skia-build/depot_tools` (preferred)
  - `/opt/depot_tools` (fallback)

---

## 4. healthcheck.sh Analysis

**Location:** `docker/healthcheck.sh`  
**Permissions:** `0755` (executable)  
**Purpose:** Environment verification with systematic dependency checking

---

## 5. CI/CD Integration

### Current State

⚠️ **PROJECT HAS .github/workflows/ DIRECTORY**

The project has GitHub Actions at `.github/workflows/` with existing workflow configurations.

### Test Runner Integration

The `test-arm64` and `test-x64` services are **already configured for CI/CD**.

**Missing Component:** `/workspace/scripts/run-tests-linux.sh` does not exist yet.

---

## 6. Volume Mounts and Persistence

### Named Volumes

```yaml
volumes:
  svgplayer-bash-history-arm64:
    driver: local
  svgplayer-bash-history-x64:
    driver: local
```

**Purpose:** Preserve bash command history across container restarts

**Location:** `/home/developer/.bash_history_dir`

**Why Separate Volumes:**
- ARM64 and x86_64 containers may have different command histories
- Prevents bash history corruption if switching architectures

### Bind Mounts

| Source | Destination | Data Flow | Persistence |
|--------|-------------|-----------|-------------|
| Project root (`..`) | `/workspace` | Bidirectional | Host filesystem |
| `~/.gitconfig` | `/home/developer/.gitconfig` | Host → Container | Host filesystem (read-only) |

**Shared State:**
- Source code changes sync immediately (cached mode)
- Build artifacts (`build/`, `skia-build/src/skia/out/`) persist on host
- Git commits made inside container appear on host

**Lost on Container Deletion:**
- Nothing (all data is on host or named volumes)

---

## 7. Security Considerations

### ✓ Good Security Practices

1. **Non-root User:** Runs as `developer` (UID 1000), not root
2. **Minimal Image:** `--no-install-recommends` reduces package count
3. **Read-only Mounts:** `.gitconfig` mounted read-only
4. **Explicit Paths:** All package names explicitly listed

### ⚠️ Security Risks (Acceptable for Dev Environment)

1. **NOPASSWD Sudo:** Development environment only, not for production
2. **Git Safe Directory Wildcard:** Necessary for volume mounts with UID mismatches
3. **No Network Isolation:** Container has full network access
4. **Shared Docker Socket:** Currently NOT mounted (good)

---

## 8. Benchmark Suite Integration

**Location:** `docker/benchmark/`

### Purpose

Comparative performance testing between **ThorVG** and **fbfsvg-player** with real GPU acceleration.

### GPU Passthrough

**NVIDIA Configuration:** Uses NVIDIA Container Toolkit with full GPU access

**Intel/AMD Configuration:** Device passthrough via `/dev/dri`

### Security Implications

⚠️ **GPU Access = Host Kernel Exposure**

**Mitigation:**
- Keep GPU drivers updated
- Run benchmarks in isolated environment

---

## 9. Performance Optimization

### Build Time Optimization

- Parallel builds with 8 cores and 2GB shared memory
- Volume caching for host-to-container reads
- Persistent Skia build output on host

---

## 10. Troubleshooting Integration

### Built-in Diagnostics

1. **Health Check:** `healthcheck.sh`
2. **Entrypoint Banner:** Shows environment info on startup
3. **Git Safe Directory:** Prevents ownership errors

---

## 11. Future Enhancements

### Recommendations

1. **Multi-stage Dockerfile:** Smaller final image
2. **BuildKit Cache Mounts:** Faster rebuilds
3. **GitHub Actions Workflow:** Use existing test services
4. **Pre-built Skia Layer:** Skip 30-60 minute Skia builds in CI/CD
5. **Test Script Implementation:** Create `/workspace/scripts/run-tests-linux.sh`

---

## Conclusion

The Docker environment is **well-designed** for development with:
- ✓ Multi-architecture support (ARM64/x86_64)
- ✓ Comprehensive dependency management
- ✓ Persistent development setup
- ✓ GPU acceleration for benchmarks
- ✓ Ready for CI/CD integration

**Remaining Work:**
- Implement `/workspace/scripts/run-tests-linux.sh`
- Add GitHub Actions workflows
- Consider pre-built Skia images for faster CI/CD

**Security Posture:**
- Appropriate for **development environments**
- NOT suitable for production without hardening
- GPU passthrough increases attack surface (necessary trade-off for benchmarks)
