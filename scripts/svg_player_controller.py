#!/usr/bin/env python3
"""
svg_player_controller.py - Cross-platform remote control for SVG Player

This module provides a Python interface to control the SVG Player remotely
via TCP/JSON protocol. It enables automated testing, scripted control,
and cross-platform test orchestration.

Usage:
    from svg_player_controller import SVGPlayerController

    # Connect to player
    player = SVGPlayerController(host='localhost', port=9999)
    player.connect()

    # Control playback
    player.play()
    player.pause()
    player.seek(2.5)

    # Query state
    state = player.get_state()
    print(f"Frame: {state['current_frame']}/{state['total_frames']}")

    # Capture screenshot
    player.screenshot('/tmp/frame.png')

    # Disconnect
    player.disconnect()
"""

import socket
import json
import time
import os
import sys
import subprocess
import threading
from typing import Optional, Dict, Any, List, Tuple
from dataclasses import dataclass
from pathlib import Path


@dataclass
class PlayerState:
    """Current state of the SVG Player"""
    playing: bool = False
    paused: bool = False
    fullscreen: bool = False
    maximized: bool = False
    current_frame: int = 0
    total_frames: int = 0
    current_time: float = 0.0
    total_duration: float = 0.0
    playback_speed: float = 1.0
    window_x: int = 0
    window_y: int = 0
    window_width: int = 0
    window_height: int = 0
    loaded_file: str = ""


@dataclass
class PlayerStats:
    """Performance statistics from the player"""
    fps: float = 0.0
    avg_frame_time: float = 0.0
    avg_render_time: float = 0.0
    dropped_frames: int = 0
    memory_usage: int = 0
    elements_rendered: int = 0


class SVGPlayerController:
    """
    Remote controller for SVG Player instances.

    Provides a high-level Python API for controlling the player,
    querying state, and capturing screenshots.
    """

    DEFAULT_PORT = 9999
    TIMEOUT = 5.0
    BUFFER_SIZE = 65536

    def __init__(self, host: str = 'localhost', port: int = DEFAULT_PORT):
        """
        Initialize controller.

        Args:
            host: Hostname or IP address of the player
            port: TCP port the player is listening on
        """
        self.host = host
        self.port = port
        self._socket: Optional[socket.socket] = None
        self._connected = False
        self._lock = threading.Lock()

    def connect(self, timeout: float = TIMEOUT) -> bool:
        """
        Connect to the player.

        Args:
            timeout: Connection timeout in seconds

        Returns:
            True if connected successfully
        """
        if self._connected:
            return True

        try:
            self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._socket.settimeout(timeout)
            self._socket.connect((self.host, self.port))
            self._connected = True
            return True
        except (socket.error, socket.timeout) as e:
            print(f"Connection failed: {e}")
            self._socket = None
            return False

    def disconnect(self):
        """Disconnect from the player."""
        if self._socket:
            try:
                self._socket.close()
            except:
                pass
            self._socket = None
        self._connected = False

    def is_connected(self) -> bool:
        """Check if connected to the player by testing actual socket state."""
        if not self._connected or not self._socket:
            return False

        # Actually test the socket using MSG_PEEK (non-destructive read)
        try:
            import select
            # Check if socket is readable (could indicate data or disconnect)
            readable, _, errored = select.select([self._socket], [], [self._socket], 0)
            if errored:
                # Socket has error condition
                self._connected = False
                return False
            if readable:
                # Try a peek read - if we get 0 bytes, connection is closed
                data = self._socket.recv(1, socket.MSG_PEEK)
                if not data:
                    self._connected = False
                    return False
            return True
        except (socket.error, OSError, BlockingIOError):
            # Socket error indicates disconnection
            self._connected = False
            return False

    def _send_command(self, cmd: str, **params) -> Dict[str, Any]:
        """
        Send a command to the player and receive response.

        Args:
            cmd: Command name
            **params: Command parameters

        Returns:
            Response dictionary
        """
        if not self._connected:
            raise ConnectionError("Not connected to player")

        with self._lock:
            try:
                # Build command JSON
                command = {"cmd": cmd, **params}
                message = json.dumps(command) + "\n"

                # Send command
                self._socket.sendall(message.encode('utf-8'))

                # Receive response
                response = b""
                while True:
                    chunk = self._socket.recv(self.BUFFER_SIZE)
                    if not chunk:
                        raise ConnectionError("Connection closed")
                    response += chunk
                    if b"\n" in response:
                        break

                # Parse response
                response_str = response.decode('utf-8').strip()
                return json.loads(response_str)

            except socket.timeout:
                raise TimeoutError("Command timed out")
            except (socket.error, OSError, BrokenPipeError, ConnectionResetError) as e:
                # Socket error - mark as disconnected
                self._connected = False
                raise ConnectionError(f"Connection lost: {e}")
            except json.JSONDecodeError as e:
                raise ValueError(f"Invalid response: {e}")

    # === Playback Control ===

    def play(self) -> bool:
        """Start or resume playback."""
        result = self._send_command("play")
        return result.get("status") == "ok"

    def pause(self) -> bool:
        """Pause playback."""
        result = self._send_command("pause")
        return result.get("status") == "ok"

    def stop(self) -> bool:
        """Stop playback and reset to beginning."""
        result = self._send_command("stop")
        return result.get("status") == "ok"

    def toggle_play(self) -> bool:
        """Toggle between play and pause."""
        result = self._send_command("toggle_play")
        return result.get("status") == "ok"

    def seek(self, time_seconds: float) -> bool:
        """
        Seek to a specific time.

        Args:
            time_seconds: Target time in seconds
        """
        result = self._send_command("seek", time=time_seconds)
        return result.get("status") == "ok"

    def set_speed(self, speed: float) -> bool:
        """
        Set playback speed.

        Args:
            speed: Speed multiplier (1.0 = normal, 2.0 = 2x, 0.5 = half)
        """
        result = self._send_command("set_speed", speed=speed)
        return result.get("status") == "ok"

    # === Window Control ===

    def set_fullscreen(self, enable: bool) -> bool:
        """Enter or exit fullscreen mode."""
        result = self._send_command("fullscreen", enable=enable)
        return result.get("status") == "ok"

    def set_maximized(self, enable: bool) -> bool:
        """Maximize or restore window."""
        result = self._send_command("maximize", enable=enable)
        return result.get("status") == "ok"

    def set_position(self, x: int, y: int) -> bool:
        """Set window position."""
        result = self._send_command("set_position", x=x, y=y)
        return result.get("status") == "ok"

    def set_size(self, width: int, height: int) -> bool:
        """Set window size."""
        result = self._send_command("set_size", width=width, height=height)
        return result.get("status") == "ok"

    # === State Queries ===

    def get_state(self) -> PlayerState:
        """Get current player state."""
        result = self._send_command("get_state")
        if result.get("status") != "ok":
            raise RuntimeError(result.get("message", "Unknown error"))

        state_data = result.get("state", {})
        return PlayerState(
            playing=state_data.get("playing", False),
            paused=state_data.get("paused", False),
            fullscreen=state_data.get("fullscreen", False),
            maximized=state_data.get("maximized", False),
            current_frame=state_data.get("current_frame", 0),
            total_frames=state_data.get("total_frames", 0),
            current_time=state_data.get("current_time", 0.0),
            total_duration=state_data.get("total_duration", 0.0),
            playback_speed=state_data.get("playback_speed", 1.0),
            window_x=state_data.get("window_x", 0),
            window_y=state_data.get("window_y", 0),
            window_width=state_data.get("window_width", 0),
            window_height=state_data.get("window_height", 0),
            loaded_file=state_data.get("loaded_file", ""),
        )

    def get_stats(self) -> PlayerStats:
        """Get performance statistics."""
        result = self._send_command("get_stats")
        if result.get("status") != "ok":
            raise RuntimeError(result.get("message", "Unknown error"))

        stats_data = result.get("stats", {})
        return PlayerStats(
            fps=stats_data.get("fps", 0.0),
            avg_frame_time=stats_data.get("avg_frame_time", 0.0),
            avg_render_time=stats_data.get("avg_render_time", 0.0),
            dropped_frames=stats_data.get("dropped_frames", 0),
            memory_usage=stats_data.get("memory_usage", 0),
            elements_rendered=stats_data.get("elements_rendered", 0),
        )

    def get_info(self) -> Dict[str, Any]:
        """Get SVG file information."""
        result = self._send_command("get_info")
        if result.get("status") != "ok":
            raise RuntimeError(result.get("message", "Unknown error"))
        return result.get("info", {})

    # === Capture ===

    def screenshot(self, path: Optional[str] = None) -> str:
        """
        Capture screenshot to file.

        Args:
            path: Output file path (optional - server generates timestamped name if not provided)

        Returns:
            Path to saved screenshot file, or empty string on failure
        """
        if path:
            result = self._send_command("screenshot", path=path)
        else:
            result = self._send_command("screenshot")

        if result.get("status") == "ok":
            # Server returns path in result field (quoted string)
            return result.get("result", "").strip('"')
        return ""

    # === File Operations ===

    def load_file(self, path: str) -> bool:
        """
        Load a new SVG file.

        Args:
            path: Path to SVG file
        """
        result = self._send_command("load_file", path=path)
        return result.get("status") == "ok"

    # === System ===

    def ping(self) -> bool:
        """Check if player is responsive."""
        try:
            result = self._send_command("ping")
            return result.get("status") == "ok"
        except:
            return False

    def quit(self) -> bool:
        """Quit the player."""
        try:
            result = self._send_command("quit")
            self.disconnect()
            return result.get("status") == "ok"
        except:
            self.disconnect()
            return False

    # === Context Manager ===

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.disconnect()


class SVGPlayerLauncher:
    """
    Helper class to launch and manage SVG Player processes.

    Handles cross-platform process launching with appropriate arguments.
    """

    def __init__(self, player_path: str):
        """
        Initialize launcher.

        Args:
            player_path: Path to the player executable
        """
        self.player_path = player_path
        self._process: Optional[subprocess.Popen] = None
        self._output_thread: Optional[threading.Thread] = None
        self._output_lines: List[str] = []

    def launch(
        self,
        svg_file: str,
        port: int = SVGPlayerController.DEFAULT_PORT,
        windowed: bool = True,
        position: Optional[Tuple[int, int]] = None,
        size: Optional[Tuple[int, int]] = None,
        extra_args: Optional[List[str]] = None,
    ) -> bool:
        """
        Launch the player process.

        Args:
            svg_file: Path to SVG file to play
            port: Remote control port
            windowed: Start in windowed mode
            position: Initial window position (x, y)
            size: Initial window size (width, height)
            extra_args: Additional command-line arguments

        Returns:
            True if launched successfully
        """
        if self._process is not None:
            return False

        args = [self.player_path, svg_file]

        # Add remote control flag
        args.extend(["--remote-control", str(port)])

        if windowed:
            args.append("--windowed")

        if position:
            args.append(f"--pos={position[0]},{position[1]}")

        if size:
            args.append(f"--size={size[0]}x{size[1]}")

        if extra_args:
            args.extend(extra_args)

        try:
            self._process = subprocess.Popen(
                args,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
            )

            # Start output capture thread
            self._output_thread = threading.Thread(target=self._capture_output)
            self._output_thread.daemon = True
            self._output_thread.start()

            return True

        except Exception as e:
            print(f"Failed to launch player: {e}")
            return False

    def _capture_output(self):
        """Capture stdout/stderr from the player process."""
        if self._process and self._process.stdout:
            for line in self._process.stdout:
                self._output_lines.append(line.rstrip())

    def wait_for_ready(self, timeout: float = 10.0) -> bool:
        """
        Wait for player to be ready (remote control server started).

        Args:
            timeout: Maximum time to wait

        Returns:
            True if player is ready
        """
        start_time = time.time()
        while time.time() - start_time < timeout:
            # Check if "RemoteControl: Server started" appears in output
            for line in self._output_lines:
                if "RemoteControl: Server started" in line:
                    return True
            time.sleep(0.1)
        return False

    def get_output(self) -> List[str]:
        """Get captured output lines."""
        return self._output_lines.copy()

    def is_running(self) -> bool:
        """Check if player process is running."""
        if self._process is None:
            return False
        return self._process.poll() is None

    def terminate(self, timeout: float = 5.0) -> bool:
        """
        Terminate the player process.

        Args:
            timeout: Maximum time to wait for graceful termination

        Returns:
            True if terminated successfully
        """
        if self._process is None:
            return True

        try:
            self._process.terminate()
            self._process.wait(timeout=timeout)
            return True
        except subprocess.TimeoutExpired:
            self._process.kill()
            return False
        finally:
            self._process = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.terminate()


# === Utility Functions ===

def compare_screenshots(path1: str, path2: str, threshold: float = 0.01) -> Tuple[bool, float]:
    """
    Compare two screenshots for similarity.

    Args:
        path1: Path to first screenshot
        path2: Path to second screenshot
        threshold: Maximum allowed difference (0.0-1.0)

    Returns:
        Tuple of (match, difference_score)
    """
    try:
        from PIL import Image
        import numpy as np

        img1 = np.array(Image.open(path1).convert('RGB'))
        img2 = np.array(Image.open(path2).convert('RGB'))

        if img1.shape != img2.shape:
            return False, 1.0

        diff = np.abs(img1.astype(float) - img2.astype(float))
        score = np.mean(diff) / 255.0

        return score <= threshold, score

    except ImportError:
        print("Warning: PIL/numpy not available for image comparison")
        # Fall back to file size comparison
        size1 = os.path.getsize(path1)
        size2 = os.path.getsize(path2)
        return size1 == size2, abs(size1 - size2) / max(size1, size2)


def wait_for_port(host: str, port: int, timeout: float = 10.0) -> bool:
    """
    Wait for a TCP port to become available.

    Args:
        host: Hostname to check
        port: Port number
        timeout: Maximum time to wait

    Returns:
        True if port is available
    """
    start_time = time.time()
    while time.time() - start_time < timeout:
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(1.0)
            sock.connect((host, port))
            sock.close()
            return True
        except:
            time.sleep(0.1)
    return False


# === CLI Interface ===

def main():
    """Command-line interface for the controller."""
    import argparse

    parser = argparse.ArgumentParser(
        description='SVG Player Remote Controller',
        epilog='Commands: play, pause, stop, seek <time>, screenshot <path>, '
               'maximize [on|off], fullscreen [on|off], state'
    )
    parser.add_argument('--host', default='localhost', help='Player hostname')
    parser.add_argument('--port', type=int, default=9999, help='Player port')
    parser.add_argument('command', nargs='?',
                        help='Command to send (omit for interactive mode)')
    parser.add_argument('args', nargs='*', help='Command arguments')

    args = parser.parse_args()

    with SVGPlayerController(args.host, args.port) as player:
        if not player.is_connected():
            print(f"Failed to connect to {args.host}:{args.port}")
            sys.exit(1)

        if args.command is None:
            # Interactive mode
            print(f"Connected to {args.host}:{args.port}")
            print("Commands: play, pause, stop, seek <time>, screenshot <path>")
            print("          maximize [on|off], fullscreen [on|off], state, stats, quit")

            while True:
                try:
                    line = input("> ").strip()
                    if not line:
                        continue

                    parts = line.split()
                    cmd = parts[0].lower()

                    if cmd == "play":
                        print("OK" if player.play() else "FAILED")
                    elif cmd == "pause":
                        print("OK" if player.pause() else "FAILED")
                    elif cmd == "stop":
                        print("OK" if player.stop() else "FAILED")
                    elif cmd == "seek" and len(parts) > 1:
                        print("OK" if player.seek(float(parts[1])) else "FAILED")
                    elif cmd == "screenshot" and len(parts) > 1:
                        print("OK" if player.screenshot(parts[1]) else "FAILED")
                    elif cmd == "maximize":
                        if len(parts) > 1:
                            enable = parts[1].lower() in ("on", "true", "1", "yes")
                            print("OK" if player.set_maximized(enable) else "FAILED")
                        else:
                            # Toggle - send without enable parameter
                            result = player._send_command("maximize")
                            print("OK" if result.get("status") == "ok" else "FAILED")
                    elif cmd == "fullscreen":
                        if len(parts) > 1:
                            enable = parts[1].lower() in ("on", "true", "1", "yes")
                            print("OK" if player.set_fullscreen(enable) else "FAILED")
                        else:
                            # Toggle - send without enable parameter
                            result = player._send_command("fullscreen")
                            print("OK" if result.get("status") == "ok" else "FAILED")
                    elif cmd == "state":
                        state = player.get_state()
                        print(f"Playing: {state.playing}, Frame: {state.current_frame}/{state.total_frames}")
                    elif cmd == "stats":
                        stats = player.get_stats()
                        print(f"FPS: {stats.fps:.1f}, Dropped: {stats.dropped_frames}")
                    elif cmd in ("quit", "exit"):
                        player.quit()
                        break
                    else:
                        print(f"Unknown command: {cmd}")

                except EOFError:
                    break
                except KeyboardInterrupt:
                    break

        else:
            # Single command mode
            cmd = args.command.lower()
            if cmd == "play":
                sys.exit(0 if player.play() else 1)
            elif cmd == "pause":
                sys.exit(0 if player.pause() else 1)
            elif cmd == "screenshot" and args.args:
                sys.exit(0 if player.screenshot(args.args[0]) else 1)
            elif cmd == "state":
                state = player.get_state()
                print(json.dumps({
                    "playing": state.playing,
                    "frame": state.current_frame,
                    "total_frames": state.total_frames,
                    "time": state.current_time,
                    "maximized": state.maximized,
                    "fullscreen": state.fullscreen,
                }))
            elif cmd == "maximize":
                if args.args:
                    enable = args.args[0].lower() in ("on", "true", "1", "yes")
                    sys.exit(0 if player.set_maximized(enable) else 1)
                else:
                    # Toggle maximize
                    result = player._send_command("maximize")
                    sys.exit(0 if result.get("status") == "ok" else 1)
            elif cmd == "fullscreen":
                if args.args:
                    enable = args.args[0].lower() in ("on", "true", "1", "yes")
                    sys.exit(0 if player.set_fullscreen(enable) else 1)
                else:
                    # Toggle fullscreen
                    result = player._send_command("fullscreen")
                    sys.exit(0 if result.get("status") == "ok" else 1)
            elif cmd == "seek" and args.args:
                sys.exit(0 if player.seek(float(args.args[0])) else 1)
            elif cmd == "stop":
                sys.exit(0 if player.stop() else 1)
            else:
                print(f"Unknown command: {cmd}")
                sys.exit(1)


if __name__ == "__main__":
    main()
