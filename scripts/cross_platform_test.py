#!/usr/bin/env python3
"""
cross_platform_test.py - Cross-platform automated test orchestrator

This script runs the same test sequences across multiple platforms
(macOS, Linux, Windows) simultaneously, collecting results and
comparing outputs.

Features:
- Parallel test execution across platforms
- Screenshot capture and comparison
- State verification at each step
- Detailed JSON reports
- CI/CD integration support

Usage:
    # Test all platforms defined in config
    python3 cross_platform_test.py

    # Test specific platforms
    python3 cross_platform_test.py --platforms macos,linux

    # Run specific test suite
    python3 cross_platform_test.py --suite playback

    # Generate HTML report
    python3 cross_platform_test.py --html-report results.html
"""

import os
import sys
import json
import time
import argparse
import tempfile
import threading
import subprocess
from pathlib import Path
from datetime import datetime
from dataclasses import dataclass, field, asdict
from typing import List, Dict, Any, Optional, Callable
from concurrent.futures import ThreadPoolExecutor, as_completed

# Add parent directory to path for imports
sys.path.insert(0, str(Path(__file__).parent))

from svg_player_controller import (
    SVGPlayerController,
    SVGPlayerLauncher,
    PlayerState,
    compare_screenshots,
    wait_for_port,
)


# === Configuration ===

@dataclass
class PlatformConfig:
    """Configuration for a test platform"""
    name: str
    player_path: str
    host: str = "localhost"
    port: int = 9999
    ssh_host: Optional[str] = None  # For remote execution
    docker_container: Optional[str] = None  # For Docker execution
    svg_samples_path: str = "./svg_input_samples"
    enabled: bool = True


@dataclass
class TestResult:
    """Result of a single test"""
    test_name: str
    platform: str
    passed: bool
    duration: float = 0.0
    error: Optional[str] = None
    screenshots: List[str] = field(default_factory=list)
    state_before: Optional[Dict] = None
    state_after: Optional[Dict] = None
    details: Dict[str, Any] = field(default_factory=dict)


@dataclass
class TestReport:
    """Complete test report"""
    timestamp: str
    platforms: List[str]
    total_tests: int = 0
    passed: int = 0
    failed: int = 0
    skipped: int = 0
    duration: float = 0.0
    results: List[TestResult] = field(default_factory=list)
    comparisons: List[Dict] = field(default_factory=list)


# === Test Definitions ===

class TestSuite:
    """Collection of tests to run"""

    def __init__(self, name: str):
        self.name = name
        self.tests: List[Callable] = []

    def add_test(self, func: Callable):
        """Decorator to add a test to the suite"""
        self.tests.append(func)
        return func

    def run(
        self,
        controller: SVGPlayerController,
        platform: str,
        output_dir: Path,
    ) -> List[TestResult]:
        """Run all tests in the suite"""
        results = []
        for test_func in self.tests:
            result = self._run_single_test(test_func, controller, platform, output_dir)
            results.append(result)
        return results

    def _run_single_test(
        self,
        test_func: Callable,
        controller: SVGPlayerController,
        platform: str,
        output_dir: Path,
    ) -> TestResult:
        """Run a single test"""
        test_name = test_func.__name__
        start_time = time.time()

        result = TestResult(
            test_name=test_name,
            platform=platform,
            passed=False,
        )

        try:
            # Get state before test
            result.state_before = asdict(controller.get_state())

            # Run test
            test_output_dir = output_dir / platform / test_name
            test_output_dir.mkdir(parents=True, exist_ok=True)

            test_func(controller, test_output_dir, result)

            # Get state after test
            result.state_after = asdict(controller.get_state())

            result.passed = True

        except AssertionError as e:
            result.error = str(e)
            result.passed = False
        except Exception as e:
            result.error = f"Exception: {type(e).__name__}: {e}"
            result.passed = False

        result.duration = time.time() - start_time
        return result


# === Standard Test Suites ===

playback_suite = TestSuite("playback")
window_suite = TestSuite("window")
screenshot_suite = TestSuite("screenshot")


@playback_suite.add_test
def test_play_pause(controller: SVGPlayerController, output_dir: Path, result: TestResult):
    """Test play/pause functionality"""
    # Start playing
    assert controller.play(), "Failed to start playback"
    time.sleep(0.5)

    state = controller.get_state()
    assert state.playing, "Player should be playing"
    result.details["playing_after_play"] = state.playing

    # Pause
    assert controller.pause(), "Failed to pause"
    time.sleep(0.2)

    state = controller.get_state()
    assert state.paused, "Player should be paused"
    result.details["paused_after_pause"] = state.paused


@playback_suite.add_test
def test_seek(controller: SVGPlayerController, output_dir: Path, result: TestResult):
    """Test seeking functionality"""
    # Seek to middle
    state = controller.get_state()
    mid_time = state.total_duration / 2

    assert controller.seek(mid_time), "Failed to seek"
    time.sleep(0.3)

    state = controller.get_state()
    # Allow some tolerance for seek accuracy
    assert abs(state.current_time - mid_time) < 0.5, f"Seek inaccurate: {state.current_time} vs {mid_time}"
    result.details["seek_target"] = mid_time
    result.details["seek_actual"] = state.current_time


@playback_suite.add_test
def test_speed_change(controller: SVGPlayerController, output_dir: Path, result: TestResult):
    """Test playback speed changes"""
    speeds = [0.5, 1.0, 2.0]

    for speed in speeds:
        assert controller.set_speed(speed), f"Failed to set speed {speed}"
        time.sleep(0.2)

        state = controller.get_state()
        assert abs(state.playback_speed - speed) < 0.01, f"Speed mismatch: {state.playback_speed} vs {speed}"

    # Reset to normal speed
    controller.set_speed(1.0)
    result.details["tested_speeds"] = speeds


@window_suite.add_test
def test_fullscreen_toggle(controller: SVGPlayerController, output_dir: Path, result: TestResult):
    """Test fullscreen toggle"""
    # Enter fullscreen
    assert controller.set_fullscreen(True), "Failed to enter fullscreen"
    time.sleep(0.5)

    state = controller.get_state()
    assert state.fullscreen, "Should be fullscreen"
    result.details["fullscreen_on"] = state.fullscreen

    # Capture screenshot in fullscreen
    screenshot_path = str(output_dir / "fullscreen.png")
    controller.screenshot(screenshot_path)
    result.screenshots.append(screenshot_path)

    # Exit fullscreen
    assert controller.set_fullscreen(False), "Failed to exit fullscreen"
    time.sleep(0.5)

    state = controller.get_state()
    assert not state.fullscreen, "Should not be fullscreen"
    result.details["fullscreen_off"] = state.fullscreen


@window_suite.add_test
def test_maximize_toggle(controller: SVGPlayerController, output_dir: Path, result: TestResult):
    """Test maximize/restore"""
    # Get initial size
    initial_state = controller.get_state()
    initial_size = (initial_state.window_width, initial_state.window_height)

    # Maximize
    assert controller.set_maximized(True), "Failed to maximize"
    time.sleep(0.5)

    state = controller.get_state()
    assert state.maximized, "Should be maximized"
    maximized_size = (state.window_width, state.window_height)
    result.details["maximized_size"] = maximized_size

    # Restore
    assert controller.set_maximized(False), "Failed to restore"
    time.sleep(0.5)

    state = controller.get_state()
    assert not state.maximized, "Should not be maximized"
    restored_size = (state.window_width, state.window_height)
    result.details["restored_size"] = restored_size


@window_suite.add_test
def test_window_position(controller: SVGPlayerController, output_dir: Path, result: TestResult):
    """Test window positioning"""
    positions = [(100, 100), (200, 150), (50, 50)]

    for x, y in positions:
        assert controller.set_position(x, y), f"Failed to set position ({x}, {y})"
        time.sleep(0.3)

        state = controller.get_state()
        # Allow some tolerance for window manager adjustments
        assert abs(state.window_x - x) < 50, f"X position mismatch: {state.window_x} vs {x}"
        assert abs(state.window_y - y) < 50, f"Y position mismatch: {state.window_y} vs {y}"

    result.details["tested_positions"] = positions


@screenshot_suite.add_test
def test_screenshot_capture(controller: SVGPlayerController, output_dir: Path, result: TestResult):
    """Test screenshot capture at different frames"""
    screenshots = []

    # Pause playback for consistent screenshots
    controller.pause()
    time.sleep(0.2)

    # Capture at different times
    times = [0.0, 0.25, 0.5, 0.75]
    state = controller.get_state()

    for t in times:
        target_time = t * state.total_duration
        controller.seek(target_time)
        time.sleep(0.3)

        path = str(output_dir / f"frame_{int(t*100):03d}.png")
        assert controller.screenshot(path), f"Failed to capture screenshot at {t}"
        screenshots.append(path)

    result.screenshots = screenshots
    result.details["capture_times"] = times

    # Verify files exist
    for path in screenshots:
        assert os.path.exists(path), f"Screenshot not saved: {path}"


# === Test Orchestrator ===

class CrossPlatformTestOrchestrator:
    """
    Orchestrates test execution across multiple platforms.
    """

    ALL_SUITES = {
        "playback": playback_suite,
        "window": window_suite,
        "screenshot": screenshot_suite,
    }

    def __init__(
        self,
        platforms: List[PlatformConfig],
        output_dir: Path,
        suites: Optional[List[str]] = None,
    ):
        self.platforms = [p for p in platforms if p.enabled]
        self.output_dir = output_dir
        self.suites = suites or list(self.ALL_SUITES.keys())
        self.report = TestReport(
            timestamp=datetime.now().isoformat(),
            platforms=[p.name for p in self.platforms],
        )

    def run_all(self, parallel: bool = True) -> TestReport:
        """
        Run all tests on all platforms.

        Args:
            parallel: Run platforms in parallel

        Returns:
            Complete test report
        """
        start_time = time.time()

        if parallel and len(self.platforms) > 1:
            self._run_parallel()
        else:
            self._run_sequential()

        self.report.duration = time.time() - start_time
        self._compare_cross_platform_results()

        return self.report

    def _run_sequential(self):
        """Run tests on each platform sequentially"""
        for platform in self.platforms:
            results = self._run_platform_tests(platform)
            self.report.results.extend(results)
            self._update_counts(results)

    def _run_parallel(self):
        """Run tests on all platforms in parallel"""
        with ThreadPoolExecutor(max_workers=len(self.platforms)) as executor:
            futures = {
                executor.submit(self._run_platform_tests, platform): platform
                for platform in self.platforms
            }

            for future in as_completed(futures):
                platform = futures[future]
                try:
                    results = future.result()
                    self.report.results.extend(results)
                    self._update_counts(results)
                except Exception as e:
                    print(f"Error running tests on {platform.name}: {e}")
                    # Add failed result for the platform
                    self.report.results.append(TestResult(
                        test_name="platform_setup",
                        platform=platform.name,
                        passed=False,
                        error=str(e),
                    ))
                    self.report.failed += 1

    def _run_platform_tests(self, platform: PlatformConfig) -> List[TestResult]:
        """Run all test suites on a single platform"""
        results = []
        print(f"\n{'='*60}")
        print(f"Testing platform: {platform.name}")
        print(f"{'='*60}")

        # Create platform output directory
        platform_dir = self.output_dir / platform.name
        platform_dir.mkdir(parents=True, exist_ok=True)

        # Connect to player
        try:
            controller = SVGPlayerController(platform.host, platform.port)
            if not controller.connect(timeout=10.0):
                raise ConnectionError(f"Cannot connect to {platform.host}:{platform.port}")

            # Run each suite
            for suite_name in self.suites:
                if suite_name not in self.ALL_SUITES:
                    print(f"  Warning: Unknown suite '{suite_name}'")
                    continue

                suite = self.ALL_SUITES[suite_name]
                print(f"  Running suite: {suite_name}")

                suite_results = suite.run(controller, platform.name, self.output_dir)
                results.extend(suite_results)

                # Print results
                for r in suite_results:
                    status = "✓" if r.passed else "✗"
                    print(f"    {status} {r.test_name} ({r.duration:.2f}s)")
                    if r.error:
                        print(f"      Error: {r.error}")

            controller.disconnect()

        except Exception as e:
            print(f"  Error: {e}")
            results.append(TestResult(
                test_name="connection",
                platform=platform.name,
                passed=False,
                error=str(e),
            ))

        return results

    def _update_counts(self, results: List[TestResult]):
        """Update report counts from results"""
        for r in results:
            self.report.total_tests += 1
            if r.passed:
                self.report.passed += 1
            else:
                self.report.failed += 1

    def _compare_cross_platform_results(self):
        """Compare results across platforms"""
        # Group results by test name
        by_test: Dict[str, List[TestResult]] = {}
        for r in self.report.results:
            if r.test_name not in by_test:
                by_test[r.test_name] = []
            by_test[r.test_name].append(r)

        # Compare screenshots across platforms
        for test_name, results in by_test.items():
            platforms_with_screenshots = [
                r for r in results if r.screenshots
            ]

            if len(platforms_with_screenshots) < 2:
                continue

            # Compare first screenshot from each platform
            base_platform = platforms_with_screenshots[0]
            base_screenshot = base_platform.screenshots[0]

            for other in platforms_with_screenshots[1:]:
                if not other.screenshots:
                    continue

                other_screenshot = other.screenshots[0]

                try:
                    match, score = compare_screenshots(base_screenshot, other_screenshot)
                    self.report.comparisons.append({
                        "test": test_name,
                        "platform_a": base_platform.platform,
                        "platform_b": other.platform,
                        "screenshot_a": base_screenshot,
                        "screenshot_b": other_screenshot,
                        "match": match,
                        "difference": score,
                    })
                except Exception as e:
                    self.report.comparisons.append({
                        "test": test_name,
                        "platform_a": base_platform.platform,
                        "platform_b": other.platform,
                        "error": str(e),
                    })

    def save_report(self, path: Path):
        """Save report to JSON file"""
        report_dict = asdict(self.report)
        with open(path, 'w') as f:
            json.dump(report_dict, f, indent=2, default=str)

    def generate_html_report(self, path: Path):
        """Generate HTML report"""
        html = f"""<!DOCTYPE html>
<html>
<head>
    <title>Cross-Platform Test Report</title>
    <style>
        body {{ font-family: -apple-system, BlinkMacSystemFont, sans-serif; margin: 20px; }}
        h1 {{ color: #333; }}
        .summary {{ background: #f5f5f5; padding: 15px; border-radius: 8px; margin-bottom: 20px; }}
        .pass {{ color: #28a745; }}
        .fail {{ color: #dc3545; }}
        table {{ border-collapse: collapse; width: 100%; margin-bottom: 20px; }}
        th, td {{ border: 1px solid #ddd; padding: 8px; text-align: left; }}
        th {{ background: #f0f0f0; }}
        tr:nth-child(even) {{ background: #f9f9f9; }}
        .screenshot {{ max-width: 200px; margin: 5px; }}
        .comparison {{ background: #fff3cd; padding: 10px; margin: 10px 0; border-radius: 4px; }}
    </style>
</head>
<body>
    <h1>Cross-Platform Test Report</h1>
    <div class="summary">
        <p><strong>Timestamp:</strong> {self.report.timestamp}</p>
        <p><strong>Platforms:</strong> {', '.join(self.report.platforms)}</p>
        <p><strong>Duration:</strong> {self.report.duration:.2f}s</p>
        <p><strong>Results:</strong>
            <span class="pass">{self.report.passed} passed</span> /
            <span class="fail">{self.report.failed} failed</span> /
            {self.report.total_tests} total
        </p>
    </div>

    <h2>Test Results</h2>
    <table>
        <tr>
            <th>Test</th>
            <th>Platform</th>
            <th>Status</th>
            <th>Duration</th>
            <th>Details</th>
        </tr>
"""
        for r in self.report.results:
            status_class = "pass" if r.passed else "fail"
            status_text = "PASS" if r.passed else "FAIL"
            error_text = f"<br><small>{r.error}</small>" if r.error else ""
            html += f"""        <tr>
            <td>{r.test_name}</td>
            <td>{r.platform}</td>
            <td class="{status_class}">{status_text}{error_text}</td>
            <td>{r.duration:.2f}s</td>
            <td>{json.dumps(r.details) if r.details else ''}</td>
        </tr>
"""

        html += """    </table>

    <h2>Cross-Platform Comparisons</h2>
"""
        for comp in self.report.comparisons:
            match_status = "✓ Match" if comp.get("match") else "✗ Mismatch"
            diff = comp.get("difference", "N/A")
            html += f"""    <div class="comparison">
        <strong>{comp['test']}</strong>: {comp.get('platform_a')} vs {comp.get('platform_b')}<br>
        Status: {match_status} (difference: {diff})
    </div>
"""

        html += """</body>
</html>"""

        with open(path, 'w') as f:
            f.write(html)


# === Default Platform Configurations ===

def get_default_platforms() -> List[PlatformConfig]:
    """Get default platform configurations"""
    project_root = Path(__file__).parent.parent

    return [
        PlatformConfig(
            name="macos",
            player_path=str(project_root / "build" / "fbfsvg-player"),
            host="localhost",
            port=9999,
        ),
        PlatformConfig(
            name="linux",
            player_path="/workspace/build/linux/svg_player",
            host="localhost",
            port=9998,
            docker_container="svg-player-dev-arm64",
            enabled=False,  # Enable when Docker container is running
        ),
        PlatformConfig(
            name="windows",
            player_path="C:\\svg-player\\svg_player.exe",
            host="localhost",
            port=9997,
            enabled=False,  # Enable when Windows VM/machine is available
        ),
    ]


# === CLI Entry Point ===

def main():
    parser = argparse.ArgumentParser(
        description='Cross-platform SVG Player test orchestrator'
    )
    parser.add_argument(
        '--platforms',
        help='Comma-separated list of platforms to test (default: all enabled)',
    )
    parser.add_argument(
        '--suites',
        help='Comma-separated list of test suites to run (default: all)',
    )
    parser.add_argument(
        '--output',
        default='./test_results',
        help='Output directory for results',
    )
    parser.add_argument(
        '--json-report',
        help='Path for JSON report output',
    )
    parser.add_argument(
        '--html-report',
        help='Path for HTML report output',
    )
    parser.add_argument(
        '--sequential',
        action='store_true',
        help='Run platforms sequentially instead of parallel',
    )
    parser.add_argument(
        '--port',
        type=int,
        default=9999,
        help='Port for localhost testing',
    )

    args = parser.parse_args()

    # Setup platforms
    platforms = get_default_platforms()

    if args.platforms:
        requested = set(args.platforms.split(','))
        platforms = [p for p in platforms if p.name in requested]
        for p in platforms:
            p.enabled = True

    # Override port for local testing
    for p in platforms:
        if p.host == "localhost" and p.name == "macos":
            p.port = args.port

    # Setup suites
    suites = None
    if args.suites:
        suites = args.suites.split(',')

    # Create output directory
    output_dir = Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)

    # Run tests
    orchestrator = CrossPlatformTestOrchestrator(
        platforms=platforms,
        output_dir=output_dir,
        suites=suites,
    )

    print(f"Running cross-platform tests")
    print(f"Platforms: {[p.name for p in platforms if p.enabled]}")
    print(f"Suites: {orchestrator.suites}")
    print(f"Output: {output_dir}")

    report = orchestrator.run_all(parallel=not args.sequential)

    # Save reports
    if args.json_report:
        orchestrator.save_report(Path(args.json_report))
        print(f"\nJSON report saved to: {args.json_report}")

    if args.html_report:
        orchestrator.generate_html_report(Path(args.html_report))
        print(f"HTML report saved to: {args.html_report}")

    # Print summary
    print(f"\n{'='*60}")
    print("SUMMARY")
    print(f"{'='*60}")
    print(f"Total: {report.total_tests}")
    print(f"Passed: {report.passed}")
    print(f"Failed: {report.failed}")
    print(f"Duration: {report.duration:.2f}s")

    # Exit with appropriate code
    sys.exit(0 if report.failed == 0 else 1)


if __name__ == "__main__":
    main()
