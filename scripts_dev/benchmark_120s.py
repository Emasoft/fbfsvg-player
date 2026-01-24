#!/usr/bin/env python3
# Force unbuffered output for real-time progress display
import sys
sys.stdout.reconfigure(line_buffering=True)
sys.stderr.reconfigure(line_buffering=True)
"""
benchmark_120s.py - 120-second benchmark automation for SVG Player

Runs the player with:
- Maximized windowed mode
- VSync disabled (for max framerate)
- Parallel rendering enabled (PreBuffer mode)
- Ping-pong loop animation

Collects:
- Frame rate samples every second
- Render timing statistics
- Memory usage
- Final pipeline breakdown

Generates:
- JSON log with all data
- Performance graphs (PNG)
- Summary report
"""

import time
import json
import subprocess
import socket
import os
from pathlib import Path
from datetime import datetime
from dataclasses import dataclass, field, asdict
from typing import List, Dict, Any, Optional

# Add scripts directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent / "scripts"))

from svg_player_controller import SVGPlayerController


@dataclass
class FrameSample:
    """Single frame rate sample"""
    timestamp: float
    elapsed: float
    fps: float
    avg_frame_time: float
    avg_render_time: float
    dropped_frames: int
    memory_usage: int
    elements_rendered: int
    current_frame: int
    total_frames: int


@dataclass
class BenchmarkResult:
    """Complete benchmark results"""
    start_time: str
    end_time: str
    duration_seconds: float
    svg_file: str
    samples: List[Dict] = field(default_factory=list)
    final_stats: Dict[str, Any] = field(default_factory=dict)
    player_output: str = ""

    # Computed metrics
    avg_fps: float = 0.0
    min_fps: float = 0.0
    max_fps: float = 0.0
    avg_frame_time_ms: float = 0.0
    min_frame_time_ms: float = 0.0
    max_frame_time_ms: float = 0.0
    total_frames_rendered: int = 0
    total_dropped_frames: int = 0


def wait_for_server(port: int, timeout: float = 15.0) -> bool:
    """Wait for remote control server to be ready."""
    start = time.time()
    while time.time() - start < timeout:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(0.5)
            result = sock.connect_ex(("localhost", port))
            sock.close()
            if result == 0:
                return True
        except:
            pass
        time.sleep(0.2)
    return False


def send_keystroke(key: str):
    """Send keystroke to frontmost app using osascript (macOS)."""
    script = f'''
    tell application "System Events"
        keystroke "{key}"
    end tell
    '''
    try:
        subprocess.run(["osascript", "-e", script], capture_output=True, timeout=2)
        return True
    except:
        return False


def parse_final_stats(output: str) -> Dict[str, Any]:
    """Parse the final statistics from player output."""
    stats = {}
    lines = output.split('\n')

    in_stats = False
    in_pipeline = False
    in_dirty = False

    for line in lines:
        line = line.strip()

        if "=== Final Statistics ===" in line:
            in_stats = True
            continue

        if "--- Pipeline Timing" in line:
            in_pipeline = True
            stats["pipeline"] = {}
            continue

        if "--- Dirty Region Tracking" in line:
            in_dirty = True
            in_pipeline = False
            stats["dirty_tracking"] = {}
            continue

        if in_stats and ":" in line:
            parts = line.split(":")
            if len(parts) >= 2:
                key = parts[0].strip().lower().replace(" ", "_")
                value_str = parts[1].strip()

                # Parse numeric values
                if in_pipeline:
                    # Format: "Event:      0.00ms (0.1%)"
                    if "ms" in value_str:
                        ms_val = value_str.split("ms")[0].strip()
                        try:
                            stats["pipeline"][key] = float(ms_val)
                        except:
                            stats["pipeline"][key] = value_str
                elif in_dirty:
                    # Format: "Partial renders: 0 (0.0%)"
                    try:
                        num_val = value_str.split()[0]
                        stats["dirty_tracking"][key] = int(num_val)
                    except:
                        stats["dirty_tracking"][key] = value_str
                else:
                    # Regular stats
                    try:
                        if "%" in value_str:
                            stats[key] = float(value_str.replace("%", "").strip())
                        elif "s" in value_str and "ms" not in value_str:
                            stats[key] = float(value_str.replace("s", "").strip())
                        else:
                            # Try to extract first number
                            import re
                            match = re.search(r'[\d.]+', value_str)
                            if match:
                                val = match.group()
                                if "." in val:
                                    stats[key] = float(val)
                                else:
                                    stats[key] = int(val)
                            else:
                                stats[key] = value_str
                    except:
                        stats[key] = value_str

    return stats


def generate_graphs(result: BenchmarkResult, output_dir: Path):
    """Generate performance graphs using matplotlib."""
    try:
        import matplotlib
        matplotlib.use('Agg')  # Non-interactive backend
        import matplotlib.pyplot as plt
        import numpy as np
    except ImportError:
        print("WARNING: matplotlib not available, skipping graphs")
        return

    samples = result.samples
    if not samples:
        print("WARNING: No samples to graph")
        return

    elapsed = [s["elapsed"] for s in samples]
    fps = [s["fps"] for s in samples]
    frame_time = [s["avg_frame_time"] for s in samples]
    render_time = [s["avg_render_time"] for s in samples]
    memory = [s["memory_usage"] / (1024*1024) for s in samples]  # Convert to MB
    dropped = [s["dropped_frames"] for s in samples]

    # Create figure with subplots
    fig, axes = plt.subplots(3, 2, figsize=(14, 12))
    fig.suptitle(f'SVG Player Benchmark - {result.duration_seconds:.1f}s\n{Path(result.svg_file).name}',
                 fontsize=14, fontweight='bold')

    # 1. FPS over time
    ax = axes[0, 0]
    ax.plot(elapsed, fps, 'b-', linewidth=1, alpha=0.7)
    ax.axhline(y=result.avg_fps, color='r', linestyle='--', label=f'Avg: {result.avg_fps:.1f}')
    ax.fill_between(elapsed, fps, alpha=0.3)
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('FPS')
    ax.set_title('Frame Rate Over Time')
    ax.legend()
    ax.grid(True, alpha=0.3)
    ax.set_ylim(bottom=0)

    # 2. Frame time over time
    ax = axes[0, 1]
    ax.plot(elapsed, frame_time, 'g-', linewidth=1, alpha=0.7, label='Frame Time')
    ax.plot(elapsed, render_time, 'orange', linewidth=1, alpha=0.7, label='Render Time')
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Time (ms)')
    ax.set_title('Frame & Render Time')
    ax.legend()
    ax.grid(True, alpha=0.3)

    # 3. FPS histogram
    ax = axes[1, 0]
    ax.hist(fps, bins=30, color='blue', alpha=0.7, edgecolor='black')
    ax.axvline(x=result.avg_fps, color='r', linestyle='--', label=f'Avg: {result.avg_fps:.1f}')
    ax.axvline(x=result.min_fps, color='orange', linestyle=':', label=f'Min: {result.min_fps:.1f}')
    ax.axvline(x=result.max_fps, color='green', linestyle=':', label=f'Max: {result.max_fps:.1f}')
    ax.set_xlabel('FPS')
    ax.set_ylabel('Frequency')
    ax.set_title('FPS Distribution')
    ax.legend()
    ax.grid(True, alpha=0.3)

    # 4. Memory usage
    ax = axes[1, 1]
    ax.plot(elapsed, memory, 'm-', linewidth=1)
    ax.fill_between(elapsed, memory, alpha=0.3, color='magenta')
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Memory (MB)')
    ax.set_title('Memory Usage')
    ax.grid(True, alpha=0.3)

    # 5. Dropped frames cumulative
    ax = axes[2, 0]
    ax.plot(elapsed, dropped, 'r-', linewidth=2)
    ax.set_xlabel('Time (s)')
    ax.set_ylabel('Dropped Frames (cumulative)')
    ax.set_title('Dropped Frames')
    ax.grid(True, alpha=0.3)

    # 6. Pipeline timing breakdown (from final stats)
    ax = axes[2, 1]
    if "pipeline" in result.final_stats:
        pipeline = result.final_stats["pipeline"]
        # Filter only timing values
        timing_keys = ["event", "anim", "fetch", "wait_skia", "overlay", "copy", "present", "active"]
        labels = []
        values = []
        for key in timing_keys:
            if key in pipeline and isinstance(pipeline[key], (int, float)):
                labels.append(key.replace("_", " ").title())
                values.append(pipeline[key])

        if values:
            colors = plt.cm.Set3(np.linspace(0, 1, len(values)))
            bars = ax.barh(labels, values, color=colors)
            ax.set_xlabel('Time (ms)')
            ax.set_title('Pipeline Timing Breakdown')
            ax.grid(True, alpha=0.3, axis='x')

            # Add value labels
            for bar, val in zip(bars, values):
                ax.text(bar.get_width() + 0.1, bar.get_y() + bar.get_height()/2,
                       f'{val:.2f}ms', va='center', fontsize=9)
    else:
        ax.text(0.5, 0.5, 'Pipeline data not available',
                ha='center', va='center', transform=ax.transAxes)
        ax.set_title('Pipeline Timing Breakdown')

    plt.tight_layout()

    graph_path = output_dir / "benchmark_graphs.png"
    plt.savefig(graph_path, dpi=150, bbox_inches='tight')
    plt.close()

    print(f"Graphs saved to: {graph_path}")


def run_benchmark(
    player_path: Path,
    svg_file: Path,
    duration_seconds: int = 120,
    output_dir: Path = None,
    sample_interval: float = 1.0,
    use_metal: bool = False,
) -> BenchmarkResult:
    """Run the benchmark and collect data."""

    if output_dir is None:
        output_dir = Path(__file__).parent.parent / "docs_dev"
    output_dir.mkdir(parents=True, exist_ok=True)

    result = BenchmarkResult(
        start_time=datetime.now().isoformat(),
        end_time="",
        duration_seconds=0,
        svg_file=str(svg_file),
    )

    print(f"=== SVG Player Benchmark ===")
    print(f"File: {svg_file.name}")
    print(f"Duration: {duration_seconds} seconds")
    print(f"Sample interval: {sample_interval}s")
    print()

    # Launch player with remote control
    cmd = [
        str(player_path),
        str(svg_file),
        "--windowed",
        "--maximize",
        "--remote-control=9999"
    ]
    if use_metal:
        cmd.append("--metal")
        print("Mode: Metal GPU backend")
    else:
        print("Mode: CPU rendering (SDL)")

    print("Launching player...")
    process = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )

    # Wait for remote control server
    print("Waiting for remote control server...")
    if not wait_for_server(9999, timeout=15.0):
        print("ERROR: Remote control server did not start")
        process.terminate()
        return result

    time.sleep(1.0)  # Let player fully initialize

    # Connect to remote control
    print("Connecting to remote control...")
    controller = SVGPlayerController(host="localhost", port=9999)
    if not controller.connect(timeout=5.0):
        print("ERROR: Failed to connect to player")
        process.terminate()
        return result

    print("Connected!")

    # Toggle optimizations using keystrokes
    # The player window should be frontmost after launch
    print("Configuring optimizations...")
    time.sleep(0.5)

    # Bring player window to front by using its window controls
    # Note: We need the window to receive keystrokes

    # V = Toggle VSync OFF (for maximum framerate)
    print("  - Disabling VSync (V key)...")
    send_keystroke("v")
    time.sleep(0.3)

    # P = Toggle to PreBuffer mode (parallel rendering)
    print("  - Enabling parallel rendering (P key)...")
    send_keystroke("p")
    time.sleep(0.3)

    # Start playback
    print("Starting playback...")
    controller.play()
    time.sleep(0.5)

    # Collect samples
    print(f"\nCollecting data for {duration_seconds} seconds...")
    print("-" * 60)

    start_time = time.time()
    sample_count = 0
    consecutive_errors = 0
    max_consecutive_errors = 5

    try:
        while True:
            elapsed = time.time() - start_time
            if elapsed >= duration_seconds:
                break

            # Get current stats with reconnection logic
            try:
                # First check if player process is still alive
                if process.poll() is not None:
                    print(f"  [{elapsed:6.1f}s] Player process died (exit code: {process.returncode}), aborting...")
                    break

                # Check if connected, reconnect if needed
                if not controller.is_connected():
                    print(f"  [{elapsed:6.1f}s] Reconnecting...")
                    if controller.connect(timeout=3.0):
                        print(f"  [{elapsed:6.1f}s] Reconnected!")
                        consecutive_errors = 0
                    else:
                        consecutive_errors += 1
                        if consecutive_errors >= max_consecutive_errors:
                            print(f"  [{elapsed:6.1f}s] Too many errors, aborting...")
                            break
                        time.sleep(sample_interval)
                        continue

                stats = controller.get_stats()
                state = controller.get_state()
                consecutive_errors = 0  # Reset on success

                sample = FrameSample(
                    timestamp=time.time(),
                    elapsed=elapsed,
                    fps=stats.fps,
                    avg_frame_time=stats.avg_frame_time,
                    avg_render_time=stats.avg_render_time,
                    dropped_frames=stats.dropped_frames,
                    memory_usage=stats.memory_usage,
                    elements_rendered=stats.elements_rendered,
                    current_frame=state.current_frame,
                    total_frames=state.total_frames,
                )

                result.samples.append(asdict(sample))
                sample_count += 1

                # Progress output every 10 seconds
                if sample_count % 10 == 0:
                    print(f"  [{elapsed:6.1f}s] FPS: {stats.fps:6.1f} | "
                          f"Frame: {state.current_frame:3d}/{state.total_frames} | "
                          f"Dropped: {stats.dropped_frames}")

            except Exception as e:
                consecutive_errors += 1
                if consecutive_errors <= 3:
                    print(f"  Warning: Failed to get stats at {elapsed:.1f}s: {e}")
                if consecutive_errors >= max_consecutive_errors:
                    # Check if player process is still alive before reconnecting
                    if process.poll() is not None:
                        print(f"  [{elapsed:6.1f}s] Player process died (exit code: {process.returncode}), aborting...")
                        break
                    print(f"  [{elapsed:6.1f}s] Too many consecutive errors, trying to reconnect...")
                    controller.disconnect()
                    time.sleep(1.0)
                    if controller.connect(timeout=3.0):
                        print(f"  [{elapsed:6.1f}s] Reconnected!")
                        consecutive_errors = 0
                    else:
                        print(f"  [{elapsed:6.1f}s] Failed to reconnect")

            # Wait for next sample
            time.sleep(sample_interval)

    except KeyboardInterrupt:
        print("\n\nBenchmark interrupted by user")

    actual_duration = time.time() - start_time
    result.duration_seconds = actual_duration
    print("-" * 60)
    print(f"Collected {len(result.samples)} samples over {actual_duration:.1f}s")

    # Stop playback and quit
    print("\nStopping player...")
    try:
        controller.quit()
    except Exception as e:
        print(f"  Warning: quit command failed: {e}")
    controller.disconnect()

    # Wait briefly then terminate if still running
    time.sleep(1.0)
    if process.poll() is None:
        print("  Terminating player process...")
        process.terminate()

    # Wait for process to exit and capture output
    try:
        stdout, _ = process.communicate(timeout=5)
        result.player_output = stdout
        result.final_stats = parse_final_stats(stdout)
    except subprocess.TimeoutExpired:
        print("  Force killing player...")
        process.kill()
        stdout, _ = process.communicate()
        result.player_output = stdout

    result.end_time = datetime.now().isoformat()

    # Compute summary statistics
    if result.samples:
        fps_values = [s["fps"] for s in result.samples if s["fps"] > 0]
        frame_times = [s["avg_frame_time"] for s in result.samples if s["avg_frame_time"] > 0]

        if fps_values:
            result.avg_fps = sum(fps_values) / len(fps_values)
            result.min_fps = min(fps_values)
            result.max_fps = max(fps_values)

        if frame_times:
            result.avg_frame_time_ms = sum(frame_times) / len(frame_times)
            result.min_frame_time_ms = min(frame_times)
            result.max_frame_time_ms = max(frame_times)

        if result.samples:
            result.total_dropped_frames = result.samples[-1]["dropped_frames"]

    # Get total frames from final stats
    if "frames_delivered" in result.final_stats:
        result.total_frames_rendered = result.final_stats["frames_delivered"]

    return result


def main():
    # Paths
    project_root = Path(__file__).parent.parent
    player_path = project_root / "build" / "svg_player_animated-macos-arm64"
    svg_file = project_root / "svg_input_samples" / "girl_hair.fbf.svg"
    output_dir = project_root / "docs_dev"

    # Verify files exist
    if not player_path.exists():
        print(f"ERROR: Player not found at {player_path}")
        sys.exit(1)
    if not svg_file.exists():
        print(f"ERROR: SVG file not found at {svg_file}")
        sys.exit(1)

    # Run benchmark (can be modified via command line: python benchmark_120s.py [duration] [--metal])
    import argparse
    parser = argparse.ArgumentParser(description='SVG Player Benchmark')
    parser.add_argument('duration', nargs='?', type=int, default=120,
                       help='Benchmark duration in seconds (default: 120)')
    parser.add_argument('--metal', action='store_true',
                       help='Enable Metal GPU backend (default: CPU rendering)')
    args = parser.parse_args()

    result = run_benchmark(
        player_path=player_path,
        svg_file=svg_file,
        duration_seconds=args.duration,
        output_dir=output_dir,
        sample_interval=1.0,
        use_metal=args.metal,
    )

    # Save JSON log
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    json_path = output_dir / f"benchmark_{timestamp}.json"

    with open(json_path, 'w') as f:
        json.dump(asdict(result), f, indent=2)
    print(f"\nJSON log saved to: {json_path}")

    # Generate graphs
    generate_graphs(result, output_dir)

    # Print summary
    print("\n" + "=" * 60)
    print("BENCHMARK SUMMARY")
    print("=" * 60)
    print(f"Duration:        {result.duration_seconds:.1f} seconds")
    print(f"Samples:         {len(result.samples)}")
    print()
    print("FPS Statistics:")
    print(f"  Average:       {result.avg_fps:.2f}")
    print(f"  Minimum:       {result.min_fps:.2f}")
    print(f"  Maximum:       {result.max_fps:.2f}")
    print()
    print("Frame Time Statistics:")
    print(f"  Average:       {result.avg_frame_time_ms:.2f} ms")
    print(f"  Minimum:       {result.min_frame_time_ms:.2f} ms")
    print(f"  Maximum:       {result.max_frame_time_ms:.2f} ms")
    print()
    print(f"Dropped Frames:  {result.total_dropped_frames}")

    if result.final_stats:
        print()
        print("Final Player Stats:")
        for key, value in result.final_stats.items():
            if key not in ("pipeline", "dirty_tracking"):
                print(f"  {key}: {value}")

        if "pipeline" in result.final_stats:
            print()
            print("Pipeline Timing (avg):")
            for key, value in result.final_stats["pipeline"].items():
                if isinstance(value, (int, float)):
                    print(f"  {key}: {value:.2f} ms")

    print("=" * 60)
    print("Done!")


if __name__ == "__main__":
    main()
