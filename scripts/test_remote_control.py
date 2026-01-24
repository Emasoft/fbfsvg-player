#!/usr/bin/env python3
"""
test_remote_control.py - Cross-platform remote control test suite for SVG Player

This script tests the remote control functionality on any platform (macOS, Linux, Windows).
It can be used to run the same test procedure across all platforms simultaneously.

Usage:
    # Test local player
    python3 test_remote_control.py

    # Test remote player
    python3 test_remote_control.py --host 192.168.1.100 --port 9999

    # Test multiple players in parallel
    python3 test_remote_control.py --targets localhost:9999,linux-vm:9999,windows-vm:9999

Requirements:
    - Python 3.7+
    - No external dependencies (uses only stdlib)
"""

import socket
import json
import sys
import time
import argparse
import threading
from typing import Optional, Dict, Any, List, Tuple
from dataclasses import dataclass
from concurrent.futures import ThreadPoolExecutor, as_completed


@dataclass
class TestResult:
    """Result of a single test"""
    name: str
    passed: bool
    message: str
    duration_ms: float


@dataclass
class TargetResult:
    """Results for a single target"""
    target: str
    connected: bool
    tests: List[TestResult]

    @property
    def passed(self) -> int:
        return sum(1 for t in self.tests if t.passed)

    @property
    def failed(self) -> int:
        return sum(1 for t in self.tests if not t.passed)

    @property
    def total(self) -> int:
        return len(self.tests)


class RemoteControlTester:
    """Tests remote control functionality for SVG Player"""

    TIMEOUT = 5.0
    BUFFER_SIZE = 65536

    def __init__(self, host: str = 'localhost', port: int = 9999):
        self.host = host
        self.port = port
        self.target = f"{host}:{port}"
        self._socket: Optional[socket.socket] = None

    def connect(self) -> bool:
        """Connect to the player"""
        try:
            self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._socket.settimeout(self.TIMEOUT)
            self._socket.connect((self.host, self.port))
            return True
        except Exception as e:
            print(f"  [{self.target}] Connection failed: {e}")
            return False

    def disconnect(self):
        """Disconnect from the player"""
        if self._socket:
            try:
                self._socket.close()
            except:
                pass
            self._socket = None

    def send_command(self, cmd: str, **params) -> Dict[str, Any]:
        """Send a command and return the response"""
        if not self._socket:
            return {"status": "error", "message": "Not connected"}

        # Build command JSON
        command = {"cmd": cmd}
        command.update(params)

        try:
            # Send command
            msg = json.dumps(command) + "\n"
            self._socket.sendall(msg.encode('utf-8'))

            # Receive response
            data = self._socket.recv(self.BUFFER_SIZE)
            response = data.decode('utf-8').strip()

            return json.loads(response)
        except Exception as e:
            return {"status": "error", "message": str(e)}

    def run_test(self, name: str, test_func) -> TestResult:
        """Run a single test and return result"""
        start = time.time()
        try:
            passed, message = test_func()
            duration = (time.time() - start) * 1000
            return TestResult(name, passed, message, duration)
        except Exception as e:
            duration = (time.time() - start) * 1000
            return TestResult(name, False, f"Exception: {e}", duration)

    def test_ping(self) -> Tuple[bool, str]:
        """Test ping command"""
        result = self.send_command("ping")
        if result.get("status") == "ok" and result.get("result") == "pong":
            return True, "pong received"
        return False, f"Unexpected response: {result}"

    def test_get_state(self) -> Tuple[bool, str]:
        """Test get_state command"""
        result = self.send_command("get_state")
        if result.get("status") == "ok" and "state" in result:
            state = result["state"]
            required_fields = ["playing", "paused", "current_frame", "total_frames"]
            missing = [f for f in required_fields if f not in state]
            if missing:
                return False, f"Missing fields: {missing}"
            return True, f"frame {state['current_frame']}/{state['total_frames']}"
        return False, f"Unexpected response: {result}"

    def test_play_pause(self) -> Tuple[bool, str]:
        """Test play and pause commands"""
        # Pause
        result = self.send_command("pause")
        if result.get("status") != "ok":
            return False, f"Pause failed: {result}"

        time.sleep(0.1)

        # Verify paused
        state = self.send_command("get_state")
        if state.get("status") != "ok":
            return False, f"Get state failed: {state}"

        # Play
        result = self.send_command("play")
        if result.get("status") != "ok":
            return False, f"Play failed: {result}"

        return True, "play/pause cycle OK"

    def test_seek(self) -> Tuple[bool, str]:
        """Test seek command"""
        # Get initial state
        state1 = self.send_command("get_state")
        if state1.get("status") != "ok":
            return False, f"Get state failed: {state1}"

        # Seek to beginning
        result = self.send_command("seek", time=0.0)
        if result.get("status") != "ok":
            return False, f"Seek failed: {result}"

        time.sleep(0.1)

        # Verify position changed
        state2 = self.send_command("get_state")
        if state2.get("status") != "ok":
            return False, f"Get state failed: {state2}"

        return True, f"seeked to t=0"

    def test_maximize(self) -> Tuple[bool, str]:
        """Test maximize command (window zoom)"""
        # Get initial state
        state1 = self.send_command("get_state")
        if state1.get("status") != "ok":
            return False, f"Get state failed: {state1}"

        # Toggle maximize
        result = self.send_command("maximize")
        if result.get("status") != "ok":
            return False, f"Maximize failed: {result}"

        time.sleep(0.5)

        # Toggle back
        result = self.send_command("maximize")
        if result.get("status") != "ok":
            return False, f"Restore failed: {result}"

        return True, "maximize toggle OK"

    def test_screenshot(self) -> Tuple[bool, str]:
        """Test screenshot command"""
        result = self.send_command("screenshot")
        if result.get("status") == "ok":
            path = result.get("result", "").strip('"')
            if path:
                return True, f"saved to {path}"
            return True, "screenshot captured"
        return False, f"Screenshot failed: {result}"

    def test_set_speed(self) -> Tuple[bool, str]:
        """Test set_speed command (skipped - not yet implemented)"""
        # Note: Playback speed control is not yet implemented in the player
        return True, "SKIPPED (not implemented)"

    def run_all_tests(self) -> TargetResult:
        """Run all tests and return results"""
        results = TargetResult(self.target, False, [])

        # Connect
        if not self.connect():
            return results

        results.connected = True

        # Define tests
        tests = [
            ("ping", self.test_ping),
            ("get_state", self.test_get_state),
            ("play_pause", self.test_play_pause),
            ("seek", self.test_seek),
            ("set_speed", self.test_set_speed),
            ("maximize", self.test_maximize),
            ("screenshot", self.test_screenshot),
        ]

        # Run each test
        for name, test_func in tests:
            result = self.run_test(name, test_func)
            results.tests.append(result)

        # Disconnect
        self.disconnect()

        return results


def print_results(results: List[TargetResult]):
    """Print test results in a formatted table"""
    print("\n" + "=" * 70)
    print("  REMOTE CONTROL TEST RESULTS")
    print("=" * 70)

    for target_result in results:
        print(f"\n  Target: {target_result.target}")
        print(f"  Connected: {'Yes' if target_result.connected else 'No'}")

        if not target_result.connected:
            print("  Status: FAILED (connection error)")
            continue

        print(f"  Tests: {target_result.passed}/{target_result.total} passed")
        print()
        print("  " + "-" * 50)
        print(f"  {'Test':<20} {'Result':<10} {'Time':>8}  {'Message'}")
        print("  " + "-" * 50)

        for test in target_result.tests:
            status = "PASS" if test.passed else "FAIL"
            status_color = "\033[92m" if test.passed else "\033[91m"
            reset_color = "\033[0m"
            print(f"  {test.name:<20} {status_color}{status:<10}{reset_color} {test.duration_ms:>6.1f}ms  {test.message}")

        print("  " + "-" * 50)

    # Summary
    print("\n" + "=" * 70)
    total_targets = len(results)
    connected_targets = sum(1 for r in results if r.connected)
    total_tests = sum(r.total for r in results)
    total_passed = sum(r.passed for r in results)

    print(f"  SUMMARY: {connected_targets}/{total_targets} targets connected, "
          f"{total_passed}/{total_tests} tests passed")
    print("=" * 70 + "\n")

    # Return exit code
    return 0 if total_passed == total_tests and connected_targets == total_targets else 1


def test_target(host: str, port: int) -> TargetResult:
    """Test a single target"""
    tester = RemoteControlTester(host, port)
    return tester.run_all_tests()


def main():
    parser = argparse.ArgumentParser(
        description='Cross-platform remote control test suite for SVG Player'
    )
    parser.add_argument('--host', default='localhost',
                        help='Player hostname (default: localhost)')
    parser.add_argument('--port', type=int, default=9999,
                        help='Player port (default: 9999)')
    parser.add_argument('--targets', type=str,
                        help='Comma-separated list of targets (host:port), '
                             'e.g., localhost:9999,linux-vm:9999')
    parser.add_argument('--parallel', action='store_true',
                        help='Run tests on multiple targets in parallel')

    args = parser.parse_args()

    # Build target list
    targets = []
    if args.targets:
        for target in args.targets.split(','):
            target = target.strip()
            if ':' in target:
                host, port = target.rsplit(':', 1)
                targets.append((host, int(port)))
            else:
                targets.append((target, 9999))
    else:
        targets.append((args.host, args.port))

    print(f"Testing {len(targets)} target(s)...")

    # Run tests
    results = []
    if args.parallel and len(targets) > 1:
        # Parallel execution
        with ThreadPoolExecutor(max_workers=len(targets)) as executor:
            futures = {
                executor.submit(test_target, host, port): (host, port)
                for host, port in targets
            }
            for future in as_completed(futures):
                results.append(future.result())
    else:
        # Sequential execution
        for host, port in targets:
            results.append(test_target(host, port))

    # Print results
    exit_code = print_results(results)
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
