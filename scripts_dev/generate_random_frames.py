#!/usr/bin/env python3
"""Generate SVG frames with dramatically randomized object positions for visual testing."""

import os
import random
import math

def generate_frame(frame_num: int, width: int = 1920, height: int = 1080, num_objects: int = 50) -> str:
    """Generate a single SVG frame with random objects."""

    random.seed(frame_num * 12345)  # Different seed per frame for variety

    colors = [
        "#FF6B6B", "#4ECDC4", "#45B7D1", "#96E6A1", "#FFE66D",
        "#FF69B4", "#7B68EE", "#32CD32", "#FFD700", "#FF1493",
        "#00CED1", "#FF4500", "#9370DB", "#00FA9A", "#DC143C"
    ]

    shapes = []

    for i in range(num_objects):
        # Random position across the entire canvas
        x = random.uniform(50, width - 50)
        y = random.uniform(50, height - 50)

        # Random size
        size = random.uniform(20, 100)

        # Random rotation
        rotation = random.uniform(0, 360)

        # Random color
        color = random.choice(colors)

        # Random opacity
        opacity = random.uniform(0.4, 1.0)

        # Random shape type
        shape_type = random.choice(['circle', 'rect', 'star', 'hexagon', 'ellipse'])

        if shape_type == 'circle':
            shapes.append(
                f'  <circle cx="{x:.1f}" cy="{y:.1f}" r="{size:.1f}" '
                f'fill="{color}" opacity="{opacity:.2f}" '
                f'transform="rotate({rotation:.1f} {x:.1f} {y:.1f})"/>'
            )
        elif shape_type == 'rect':
            w = size * random.uniform(0.8, 1.5)
            h = size * random.uniform(0.8, 1.5)
            shapes.append(
                f'  <rect x="{x - w/2:.1f}" y="{y - h/2:.1f}" width="{w:.1f}" height="{h:.1f}" '
                f'rx="{size * 0.1:.1f}" fill="{color}" opacity="{opacity:.2f}" '
                f'transform="rotate({rotation:.1f} {x:.1f} {y:.1f})"/>'
            )
        elif shape_type == 'star':
            points = []
            for j in range(10):
                angle = math.radians(j * 36 - 90 + rotation)
                r = size if j % 2 == 0 else size * 0.5
                px = x + r * math.cos(angle)
                py = y + r * math.sin(angle)
                points.append(f"{px:.1f},{py:.1f}")
            shapes.append(
                f'  <polygon points="{" ".join(points)}" '
                f'fill="{color}" opacity="{opacity:.2f}"/>'
            )
        elif shape_type == 'hexagon':
            points = []
            for j in range(6):
                angle = math.radians(j * 60 - 90 + rotation)
                px = x + size * math.cos(angle)
                py = y + size * math.sin(angle)
                points.append(f"{px:.1f},{py:.1f}")
            shapes.append(
                f'  <polygon points="{" ".join(points)}" '
                f'fill="{color}" opacity="{opacity:.2f}"/>'
            )
        elif shape_type == 'ellipse':
            rx = size * random.uniform(0.5, 1.0)
            ry = size * random.uniform(0.5, 1.0)
            shapes.append(
                f'  <ellipse cx="{x:.1f}" cy="{y:.1f}" rx="{rx:.1f}" ry="{ry:.1f}" '
                f'fill="{color}" opacity="{opacity:.2f}" '
                f'transform="rotate({rotation:.1f} {x:.1f} {y:.1f})"/>'
            )

    # Background gradient
    bg_color1 = random.choice(colors)
    bg_color2 = random.choice(colors)

    svg = f'''<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" width="{width}" height="{height}">
  <defs>
    <linearGradient id="bg" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="{bg_color1}"/>
      <stop offset="100%" stop-color="{bg_color2}"/>
    </linearGradient>
  </defs>
  <rect width="{width}" height="{height}" fill="url(#bg)" opacity="0.3"/>
{chr(10).join(shapes)}
</svg>'''

    return svg


def main():
    output_dir = "svg_input_samples/random_frames"
    os.makedirs(output_dir, exist_ok=True)

    num_frames = 1000
    print(f"Generating {num_frames} random frames...")

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
