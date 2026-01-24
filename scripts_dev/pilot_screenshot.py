#!/usr/bin/env python3
"""
Pilot the SVG Player to capture first frame screenshot.

Uses the remote control system to:
1. Launch player windowed+maximized with an FBF file
2. Start animation
3. Capture screenshot of first frame
4. Exit cleanly
"""

import sys
import time
from pathlib import Path

# Add scripts directory to path for imports
sys.path.insert(0, str(Path(__file__).parent.parent / "scripts"))

from svg_player_controller import SVGPlayerController


def main():
    import subprocess
    import socket

    # Paths
    project_root = Path(__file__).parent.parent
    player_path = project_root / "build" / "svg_player_animated-macos-arm64"
    svg_file = project_root / "svg_input_samples" / "girl_hair.fbf.svg"
    screenshot_path = project_root / "docs_dev" / "first_frame_screenshot.ppm"

    # Verify files exist
    if not player_path.exists():
        print(f"ERROR: Player not found at {player_path}")
        sys.exit(1)
    if not svg_file.exists():
        print(f"ERROR: SVG file not found at {svg_file}")
        sys.exit(1)

    # Ensure output directory exists
    screenshot_path.parent.mkdir(parents=True, exist_ok=True)

    print(f"Launching player with: {svg_file.name}")

    # Launch player directly with subprocess (more reliable than wrapper)
    # Note: --remote-control uses = syntax for port: --remote-control=9999
    cmd = [
        str(player_path),
        str(svg_file),
        "--windowed",
        "--maximize",
        "--remote-control=9999"
    ]

    process = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )

    print("Waiting for remote control server...")

    # Wait for server to be ready by polling the port
    def wait_for_server(port, timeout=10.0):
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

    if not wait_for_server(9999, timeout=15.0):
        print("ERROR: Remote control server did not start")
        process.terminate()
        sys.exit(1)

    # Give it a moment to fully initialize
    time.sleep(0.5)

    print("Connecting to remote control...")

    # Connect to the player
    controller = SVGPlayerController(host="localhost", port=9999)
    if not controller.connect(timeout=5.0):
        print("ERROR: Failed to connect to player")
        process.terminate()
        sys.exit(1)

    print("Connected! Starting animation...")

    # Start the animation
    if not controller.play():
        print("WARNING: Play command may have failed")

    # Wait a moment for first frame to render
    time.sleep(0.3)

    # Get current state to verify
    state = controller.get_state()
    print(f"State: Frame {state.current_frame}/{state.total_frames}, Playing: {state.playing}")

    # Take screenshot
    print(f"Capturing screenshot to: {screenshot_path}")
    result = controller.screenshot(str(screenshot_path))

    if result:
        print(f"Screenshot saved: {result}")
    else:
        print("WARNING: Screenshot may have failed")

    # Wait a moment to ensure screenshot is written
    time.sleep(0.2)

    # Quit the player
    print("Quitting player...")
    controller.quit()
    controller.disconnect()

    # Wait for clean exit
    time.sleep(0.5)

    # Verify screenshot was saved
    if screenshot_path.exists():
        size = screenshot_path.stat().st_size
        print(f"SUCCESS: Screenshot saved ({size} bytes)")
    else:
        print("WARNING: Screenshot file not found at expected path")
        # Check if it was saved with default name (the player might save with its own naming)
        ppm_files = list(project_root.glob("*.ppm"))
        if ppm_files:
            print(f"Found PPM files: {[f.name for f in ppm_files]}")

    # Clean up
    if process.poll() is None:
        process.terminate()
        process.wait(timeout=3)

    print("Done!")


if __name__ == "__main__":
    main()
