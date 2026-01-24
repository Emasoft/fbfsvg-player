# SVG Player Benchmark Suite

Comparative benchmarking between **ThorVG** and **fbfsvg-player** for FBF.SVG animated vector graphics.

## Overview

This Docker-based benchmark suite runs both players in fullscreen mode with real GPU acceleration, measuring frame rates and rendering performance on animated SVG files.

## Requirements

### Hardware
- **Linux host** with X11 display server
- **GPU**: NVIDIA (recommended), Intel, or AMD
- At least 20GB free disk space
- At least 8GB RAM

### Software
- Docker 20.10+ with Docker Compose v2
- For NVIDIA GPUs: [NVIDIA Container Toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/install-guide.html)
- X11 display server running

## Quick Start

```bash
# 1. Navigate to benchmark directory
cd docker/benchmark

# 2. Run the setup script (builds everything)
./setup-benchmark.sh

# 3. Run a quick comparison
docker-compose run --rm benchmark quick-compare /workspace/svg_input_samples/seagull.fbf.svg 10

# 4. Analyze results
./analyze-results.py results/
```

## Detailed Usage

### Setup

The setup script performs the following steps:

1. Builds the Docker image with ThorVG
2. Builds Skia for Linux (if not already built)
3. Builds fbfsvg-player for Linux
4. Installs both players in the container

```bash
./setup-benchmark.sh
```

### Interactive Shell

Open an interactive shell in the benchmark container:

```bash
docker-compose run --rm benchmark
```

Inside the container:
```bash
# Run ThorVG player
thorvg_player /workspace/svg_input_samples/seagull.fbf.svg

# Run fbfsvg-player
svg_player_animated_linux /workspace/svg_input_samples/seagull.fbf.svg --fullscreen
```

### Quick Comparison

Compare both players on a single file:

```bash
docker-compose run --rm benchmark quick-compare /workspace/svg_input_samples/seagull.fbf.svg 10
```

Arguments:
- `<svg_file>`: Path to the SVG file
- `<duration>`: Seconds to run each player (default: 10)

### Full Benchmark Suite

Run comprehensive benchmarks on multiple files:

```bash
docker-compose run --rm benchmark run-benchmark \
    --duration 30 \
    --fullscreen \
    /workspace/svg_input_samples/*.svg
```

Options:
- `--duration <seconds>`: How long to run each player per file
- `--fullscreen`: Enable fullscreen rendering
- `--output <dir>`: Results output directory (default: /results)

### Analyze Results

After running benchmarks, analyze the results:

```bash
# Terminal summary
./analyze-results.py results/

# Generate markdown report
./analyze-results.py results/ --output report.md

# JSON output
./analyze-results.py results/ --json > comparison.json
```

## GPU Configuration

### NVIDIA GPUs

The default configuration uses NVIDIA Container Toolkit:

```yaml
deploy:
  resources:
    reservations:
      devices:
        - driver: nvidia
          count: all
          capabilities: [gpu, video, compute, utility]
```

Verify NVIDIA toolkit is configured:
```bash
docker info | grep nvidia
nvidia-smi  # Should show your GPU
```

### Intel/AMD GPUs

For Intel or AMD GPUs, modify `docker-compose.yml`:

1. Comment out the `deploy:` section
2. Uncomment the `devices:` section:

```yaml
devices:
  - /dev/dri:/dev/dri
```

## Output Format

### Benchmark JSON

Each benchmark run produces JSON with:

```json
{
  "player": "fbfsvg-player",
  "file": "/workspace/svg_input_samples/seagull.fbf.svg",
  "duration_seconds": 30,
  "total_frames": 1847,
  "avg_fps": 61.5,
  "avg_frame_time_ms": 16.26,
  "min_fps": 58.2,
  "max_fps": 63.1,
  "timestamp": "2024-01-15T10:30:45Z"
}
```

### Analysis Report

The analysis script produces comparison tables:

```
Performance Comparison: ThorVG vs fbfsvg-player

┌────────────────────────────────┬─────────────────┬─────────────────┬──────────────┬────────────┐
│ SVG File                       │     ThorVG FPS │    fbfsvg FPS │       Ratio │   Winner   │
├────────────────────────────────┼─────────────────┼─────────────────┼──────────────┼────────────┤
│ seagull.fbf.svg                │           58.3 │           61.5 │       1.05x │   fbfsvg   │
│ walk_cycle.fbf.svg             │           54.1 │           59.8 │       1.11x │   fbfsvg   │
└────────────────────────────────┴─────────────────┴─────────────────┴──────────────┴────────────┘
```

## Directory Structure

```
docker/benchmark/
├── Dockerfile           # Container with ThorVG + build tools
├── docker-compose.yml   # GPU passthrough configuration
├── setup-benchmark.sh   # Automated setup script
├── analyze-results.py   # Results analysis tool
├── README.md           # This documentation
└── results/            # Benchmark output (created on first run)
    ├── benchmark_20240115_103045.json
    └── ...
```

## Troubleshooting

### X11 Display Issues

```bash
# Allow Docker to access X11
xhost +local:docker

# Verify DISPLAY variable
echo $DISPLAY  # Should be :0 or similar
```

### NVIDIA Toolkit Not Found

```bash
# Install NVIDIA Container Toolkit
distribution=$(. /etc/os-release;echo $ID$VERSION_ID)
curl -s -L https://nvidia.github.io/nvidia-docker/gpgkey | sudo apt-key add -
curl -s -L https://nvidia.github.io/nvidia-docker/$distribution/nvidia-docker.list | \
    sudo tee /etc/apt/sources.list.d/nvidia-docker.list
sudo apt-get update && sudo apt-get install -y nvidia-container-toolkit
sudo systemctl restart docker
```

### Container Fails to Start

```bash
# Check Docker logs
docker-compose logs

# Verify GPU access
docker run --rm --gpus all nvidia/cuda:11.0-base nvidia-smi
```

### Skia Build Issues

```bash
# Inside container, verify Skia is built
ls -la /workspace/fbfsvg-player/skia-build/src/skia/out/release-linux/

# Rebuild if needed
cd /workspace/fbfsvg-player/skia-build
./build-linux.sh -y
```

## Performance Notes

1. **Disable VSync**: The container sets `vblank_mode=0` and `__GL_SYNC_TO_VBLANK=0` for accurate measurements

2. **Fullscreen Mode**: Always use `--fullscreen` for consistent results; windowed mode may have compositor overhead

3. **Warm-up**: The benchmark scripts run a short warm-up period before measuring

4. **Multiple Runs**: For statistical significance, run each benchmark multiple times

## Contributing

To add new benchmark metrics:

1. Modify the player's stats output format
2. Update `run-benchmark` script to parse new fields
3. Update `analyze-results.py` to include new metrics in reports
