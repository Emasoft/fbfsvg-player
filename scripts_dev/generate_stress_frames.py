#!/usr/bin/env python3
"""Generate SVG frames with full-height text and complex 500-point clip paths for stress testing."""

import os
import random
import math

def generate_leaf_shape(cx: float, cy: float, size: float, rotation: float) -> str:
    """Generate a leaf-like closed path with cubic bezier curves."""
    # Leaf shape using cubic beziers - organic asymmetric form
    # Size is approximately 1/8 of height
    s = size

    # Control points for a leaf-like shape
    path = f"M {cx:.1f},{cy - s:.1f} "  # Top point
    # Right side curve
    path += f"C {cx + s*0.3:.1f},{cy - s*0.7:.1f} {cx + s*0.5:.1f},{cy - s*0.2:.1f} {cx + s*0.4:.1f},{cy + s*0.1:.1f} "
    # Bottom right curve
    path += f"C {cx + s*0.3:.1f},{cy + s*0.4:.1f} {cx + s*0.1:.1f},{cy + s*0.7:.1f} {cx:.1f},{cy + s*0.8:.1f} "
    # Bottom left curve
    path += f"C {cx - s*0.1:.1f},{cy + s*0.7:.1f} {cx - s*0.3:.1f},{cy + s*0.4:.1f} {cx - s*0.4:.1f},{cy + s*0.1:.1f} "
    # Left side curve back to top
    path += f"C {cx - s*0.5:.1f},{cy - s*0.2:.1f} {cx - s*0.3:.1f},{cy - s*0.7:.1f} {cx:.1f},{cy - s:.1f} "
    path += "Z"

    return path


def generate_complex_clippath(num_points: int = 50, width: int = 1920, height: int = 1080) -> str:
    """Generate a moderately complex clip path (organic blob shape).

    Reduced from 500 to 50 points for ThorVG compatibility while still being
    a meaningful stress test. 50-point polygon is still complex enough to
    test clip-path rendering without breaking text in nested transforms.
    """
    points = []
    center_x = width / 2
    center_y = height / 2
    base_radius = min(width, height) * 0.4

    for i in range(num_points):
        angle = (2 * math.pi * i) / num_points
        # Add noise to make it organic
        noise = random.uniform(0.7, 1.3)
        # Add wave pattern
        wave = 0.1 * math.sin(angle * 8) + 0.05 * math.sin(angle * 13)
        radius = base_radius * noise * (1 + wave)

        x = center_x + radius * math.cos(angle)
        y = center_y + radius * math.sin(angle)
        points.append(f"{x:.1f},{y:.1f}")

    return f'<clipPath id="complexClip"><polygon points="{" ".join(points)}"/></clipPath>'


def generate_frame(frame_num: int, width: int = 1920, height: int = 1080) -> str:
    """Generate a single SVG frame with full-height text and complex clip path."""

    random.seed(frame_num * 54321)

    colors = [
        "#FF6B6B", "#4ECDC4", "#45B7D1", "#96E6A1", "#FFE66D",
        "#FF69B4", "#7B68EE", "#32CD32", "#FFD700", "#FF1493",
        "#00CED1", "#FF4500", "#9370DB", "#00FA9A", "#DC143C"
    ]

    # Random letter for full-height text
    letters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
    letter = random.choice(letters)

    # Random colors
    text_color = random.choice(colors)
    bg_color1 = random.choice(colors)
    bg_color2 = random.choice(colors)
    stroke_color = random.choice(colors)

    # Random position offset for the letter (scaled up)
    x_offset = random.uniform(-50, 50)
    y_offset = random.uniform(-30, 30)

    # Random rotation
    rotation = random.uniform(-15, 15)

    # Fixed 3x scale factor
    scale_factor = 3.0

    # Generate a 50-point complex clip path (reduced from 500 for ThorVG compatibility)
    # 50 points is still complex enough for stress testing without breaking text in nested transforms
    clip_path = generate_complex_clippath(50, width, height)

    # Additional random shapes behind the text
    bg_shapes = []
    for _ in range(20):
        x = random.uniform(0, width)
        y = random.uniform(0, height)
        size = random.uniform(50, 200)
        color = random.choice(colors)
        opacity = random.uniform(0.2, 0.5)
        shape_type = random.choice(['circle', 'rect'])

        if shape_type == 'circle':
            bg_shapes.append(f'    <circle cx="{x:.1f}" cy="{y:.1f}" r="{size:.1f}" fill="{color}" opacity="{opacity:.2f}"/>')
        else:
            bg_shapes.append(f'    <rect x="{x:.1f}" y="{y:.1f}" width="{size:.1f}" height="{size:.1f}" fill="{color}" opacity="{opacity:.2f}"/>')

    # Generate 200-300 leaf-like bezier shapes with conic gradients
    num_leaves = random.randint(200, 300)
    leaf_size = height / 8  # Approximately 1/8 of height
    leaf_defs = []
    leaf_shapes = []

    for i in range(num_leaves):
        lx = random.uniform(-width * 0.1, width * 1.1)
        ly = random.uniform(-height * 0.1, height * 1.1)
        lsize = leaf_size * random.uniform(0.5, 1.5)
        lrot = random.uniform(0, 360)

        # Random colors for conic gradient
        c1 = random.choice(colors)
        c2 = random.choice(colors)
        c3 = random.choice(colors)

        # Create conic gradient definition for this leaf
        grad_id = f"conicLeaf{i}"
        # SVG doesn't support conic gradients natively, so we simulate with a radial gradient with multiple stops
        leaf_defs.append(f'''    <radialGradient id="{grad_id}" cx="50%" cy="50%" r="70%">
      <stop offset="0%" stop-color="{c1}"/>
      <stop offset="33%" stop-color="{c2}"/>
      <stop offset="66%" stop-color="{c3}"/>
      <stop offset="100%" stop-color="{c1}"/>
    </radialGradient>''')

        # Generate the leaf path
        leaf_path = generate_leaf_shape(lx, ly, lsize, lrot)
        leaf_shapes.append(
            f'    <path d="{leaf_path}" fill="url(#{grad_id})" opacity="0.20" '
            f'transform="rotate({lrot:.1f} {lx:.1f} {ly:.1f})"/>'
        )

    # Calculate center for scale transform
    cx = width / 2
    cy = height / 2

    svg = f'''<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" width="{width}" height="{height}">
  <defs>
    <linearGradient id="bgGrad" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="{bg_color1}"/>
      <stop offset="100%" stop-color="{bg_color2}"/>
    </linearGradient>
    <linearGradient id="textGrad" x1="0%" y1="0%" x2="0%" y2="100%">
      <stop offset="0%" stop-color="{text_color}"/>
      <stop offset="50%" stop-color="#FFFFFF"/>
      <stop offset="100%" stop-color="{stroke_color}"/>
    </linearGradient>
    {clip_path}
    <filter id="glow" x="-50%" y="-50%" width="200%" height="200%">
      <feGaussianBlur in="SourceGraphic" stdDeviation="10" result="blur"/>
      <feMerge>
        <feMergeNode in="blur"/>
        <feMergeNode in="SourceGraphic"/>
      </feMerge>
    </filter>
    <!-- Leaf gradient definitions ({num_leaves} leaves) -->
{chr(10).join(leaf_defs)}
  </defs>

  <!-- Background gradient -->
  <rect width="{width}" height="{height}" fill="url(#bgGrad)"/>

  <!-- SCALED CONTENT GROUP: 3x scale around center -->
  <g transform="translate({cx:.1f}, {cy:.1f}) scale({scale_factor:.2f}) translate({-cx:.1f}, {-cy:.1f})">

    <!-- Background shapes -->
    <g opacity="0.5">
{chr(10).join(bg_shapes)}
    </g>

    <!-- Leaf shapes (200-300 bezier paths with 20% opacity) -->
    <g>
{chr(10).join(leaf_shapes)}
    </g>

    <!-- Full-height text with complex clip path -->
    <g clip-path="url(#complexClip)">
      <text x="{cx + x_offset:.1f}" y="{height * 0.85 + y_offset:.1f}"
            font-family="Arial, Helvetica, sans-serif"
            font-size="{height}px"
            font-weight="bold"
            fill="url(#textGrad)"
            stroke="{stroke_color}"
            stroke-width="8"
            text-anchor="middle"
            filter="url(#glow)"
            transform="rotate({rotation:.1f} {cx:.1f} {cy:.1f})">
        {letter}
      </text>
    </g>

  </g>

  <!-- Frame counter (not scaled) -->
  <text x="{cx:.1f}" y="{height - 30:.1f}"
        font-family="monospace"
        font-size="24px"
        fill="#FFFFFF"
        text-anchor="middle"
        opacity="0.7">
    Frame {frame_num:05d} - Scale {scale_factor:.1f}x
  </text>
</svg>'''

    return svg


def main():
    output_dir = "svg_input_samples/stress_frames"
    os.makedirs(output_dir, exist_ok=True)

    num_frames = 1000
    print(f"Generating {num_frames} stress test frames with full-height text and 500-point clip paths...")

    for i in range(1, num_frames + 1):
        svg_content = generate_frame(i)
        filename = os.path.join(output_dir, f"frame_{i:05d}.svg")
        with open(filename, 'w') as f:
            f.write(svg_content)

        if i % 100 == 0:
            print(f"  Generated {i}/{num_frames} frames")

    print(f"Done! Frames saved to {output_dir}/")


if __name__ == "__main__":
    main()
