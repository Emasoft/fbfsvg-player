#!/usr/bin/env python3
"""Generate numbered SVG frames for benchmark testing.

Creates a folder of SVG files named frame_00001.svg, frame_00002.svg, etc.
Each frame contains complex shapes with gradients (ThorVG-compatible).
"""

import os
import sys
import math

def generate_svg_frame(frame_num: int, total_frames: int, num_elements: int = 50) -> str:
    """Generate a single SVG frame with animated element positions.

    Args:
        frame_num: Current frame number (0-indexed)
        total_frames: Total number of frames in sequence
        num_elements: Number of complex elements to render

    Returns:
        SVG content as string
    """
    # Progress through animation cycle (0.0 to 1.0)
    t = frame_num / max(total_frames - 1, 1)

    # Colors for gradients
    colors = [
        ("#FF6B6B", "#4ECDC4", "#45B7D1"),  # Coral to teal
        ("#96E6A1", "#DDA0DD", "#FFB6C1"),  # Green to pink
        ("#FFE66D", "#FF6B6B", "#C44D58"),  # Yellow to red
        ("#4169E1", "#7B68EE", "#9370DB"),  # Blue to purple
    ]

    svg_parts = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1920 1080" width="1920" height="1080">',
        '  <defs>',
    ]

    # Define gradients
    for i, (c1, c2, c3) in enumerate(colors):
        svg_parts.append(f'''    <linearGradient id="grad{i}" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="{c1}"/>
      <stop offset="50%" stop-color="{c2}"/>
      <stop offset="100%" stop-color="{c3}"/>
    </linearGradient>''')
        svg_parts.append(f'''    <radialGradient id="rgrad{i}" cx="50%" cy="50%" r="50%">
      <stop offset="0%" stop-color="#FFFFFF"/>
      <stop offset="100%" stop-color="{c2}"/>
    </radialGradient>''')

    svg_parts.append('  </defs>')
    svg_parts.append('  <rect width="1920" height="1080" fill="#1a1a2e"/>')

    # Generate elements with positions that change per frame
    grid_cols = int(math.sqrt(num_elements * (1920/1080)))
    grid_rows = int(num_elements / grid_cols)

    cell_w = 1920 / (grid_cols + 1)
    cell_h = 1080 / (grid_rows + 1)

    element_id = 0
    for row in range(grid_rows):
        for col in range(grid_cols):
            if element_id >= num_elements:
                break

            # Base position
            base_x = (col + 0.5) * cell_w
            base_y = (row + 0.5) * cell_h

            # Add animation offset based on frame
            offset_x = math.sin(t * 2 * math.pi + element_id * 0.3) * 20
            offset_y = math.cos(t * 2 * math.pi + element_id * 0.5) * 15

            x = base_x + offset_x
            y = base_y + offset_y

            # Element size varies slightly per frame
            size = 60 + math.sin(t * math.pi + element_id) * 10

            grad_idx = element_id % len(colors)
            opacity = 0.7 + 0.3 * math.sin(t * math.pi + element_id * 0.2)

            # Alternate between shapes
            shape_type = element_id % 3

            if shape_type == 0:
                # Rounded rect with inner circle
                svg_parts.append(f'''  <g transform="translate({x:.1f}, {y:.1f})">
    <rect x="{-size/2:.1f}" y="{-size/2:.1f}" width="{size:.1f}" height="{size:.1f}" rx="{size/5:.1f}" fill="url(#grad{grad_idx})" opacity="{opacity:.2f}"/>
    <circle cx="0" cy="0" r="{size/3:.1f}" fill="url(#rgrad{grad_idx})" opacity="{opacity*0.8:.2f}"/>
  </g>''')
            elif shape_type == 1:
                # Circle with inner ellipse
                svg_parts.append(f'''  <g transform="translate({x:.1f}, {y:.1f})">
    <circle cx="0" cy="0" r="{size/2:.1f}" fill="url(#grad{grad_idx})" opacity="{opacity:.2f}"/>
    <ellipse cx="0" cy="0" rx="{size/3:.1f}" ry="{size/5:.1f}" fill="#FFFFFF" opacity="{opacity*0.5:.2f}"/>
  </g>''')
            else:
                # Triangle with inner circle
                s = size / 2
                svg_parts.append(f'''  <g transform="translate({x:.1f}, {y:.1f})">
    <polygon points="0,{-s:.1f} {s*0.866:.1f},{s*0.5:.1f} {-s*0.866:.1f},{s*0.5:.1f}" fill="url(#grad{grad_idx})" opacity="{opacity:.2f}"/>
    <circle cx="0" cy="{s*0.15:.1f}" r="{s*0.35:.1f}" fill="url(#rgrad{grad_idx})" opacity="{opacity*0.7:.2f}"/>
  </g>''')

            element_id += 1

    svg_parts.append('</svg>')
    return '\n'.join(svg_parts)


def main():
    if len(sys.argv) < 2:
        print("Usage: python generate_test_frames.py <output_folder> [num_frames] [num_elements]")
        print("  num_frames: Number of frames to generate (default: 100)")
        print("  num_elements: Number of complex elements per frame (default: 50)")
        sys.exit(1)

    output_folder = sys.argv[1]
    num_frames = int(sys.argv[2]) if len(sys.argv) > 2 else 100
    num_elements = int(sys.argv[3]) if len(sys.argv) > 3 else 50

    os.makedirs(output_folder, exist_ok=True)

    print(f"Generating {num_frames} frames with {num_elements} elements each...")

    for i in range(num_frames):
        svg_content = generate_svg_frame(i, num_frames, num_elements)
        filename = f"frame_{i+1:05d}.svg"
        filepath = os.path.join(output_folder, filename)
        with open(filepath, 'w') as f:
            f.write(svg_content)

        if (i + 1) % 10 == 0:
            print(f"  Generated {i + 1}/{num_frames} frames")

    print(f"Done! Frames saved to: {output_folder}")


if __name__ == "__main__":
    main()
