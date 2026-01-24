#!/usr/bin/env python3
"""
Generate complex static SVG for benchmarking ThorVG vs fbfsvg-player.

Features used (ThorVG-compatible only):
- Basic shapes: rect, circle, ellipse, path
- Linear and radial gradients
- Gaussian blur filter (feGaussianBlur only)
- Nested groups with transforms
- Many overlapping semi-transparent elements

The goal is to create enough visual complexity to stress-test both renderers
and drop FPS below 20 for accurate throughput comparison.
"""

import random
import math
import argparse
from pathlib import Path

def generate_gradient_defs(num_linear=20, num_radial=20):
    """Generate gradient definitions."""
    defs = []

    for i in range(num_linear):
        angle = random.uniform(0, 360)
        colors = [
            f"#{random.randint(0, 255):02x}{random.randint(0, 255):02x}{random.randint(0, 255):02x}"
            for _ in range(random.randint(2, 4))
        ]
        stops = "\n      ".join([
            f'<stop offset="{j/(len(colors)-1)*100}%" stop-color="{c}" stop-opacity="{random.uniform(0.3, 1.0):.2f}"/>'
            for j, c in enumerate(colors)
        ])
        defs.append(f'''    <linearGradient id="lg{i}" gradientTransform="rotate({angle:.1f})">
      {stops}
    </linearGradient>''')

    for i in range(num_radial):
        cx = random.uniform(0.2, 0.8)
        cy = random.uniform(0.2, 0.8)
        colors = [
            f"#{random.randint(0, 255):02x}{random.randint(0, 255):02x}{random.randint(0, 255):02x}"
            for _ in range(random.randint(2, 4))
        ]
        stops = "\n      ".join([
            f'<stop offset="{j/(len(colors)-1)*100}%" stop-color="{c}" stop-opacity="{random.uniform(0.3, 1.0):.2f}"/>'
            for j, c in enumerate(colors)
        ])
        defs.append(f'''    <radialGradient id="rg{i}" cx="{cx:.2f}" cy="{cy:.2f}" r="0.5">
      {stops}
    </radialGradient>''')

    return defs


def generate_blur_filters(num_filters=10):
    """Generate gaussian blur filter definitions."""
    filters = []
    for i in range(num_filters):
        blur = random.uniform(1, 8)
        filters.append(f'''    <filter id="blur{i}" x="-50%" y="-50%" width="200%" height="200%">
      <feGaussianBlur in="SourceGraphic" stdDeviation="{blur:.1f}"/>
    </filter>''')
    return filters


def generate_complex_path():
    """Generate a complex path with many curve segments."""
    # Start point
    x, y = random.uniform(100, 1700), random.uniform(100, 900)
    d = f"M{x:.1f},{y:.1f}"

    # Add 10-20 curve segments
    for _ in range(random.randint(10, 20)):
        cmd = random.choice(['C', 'Q', 'L'])
        if cmd == 'C':  # Cubic bezier
            cp1x = x + random.uniform(-200, 200)
            cp1y = y + random.uniform(-200, 200)
            cp2x = x + random.uniform(-200, 200)
            cp2y = y + random.uniform(-200, 200)
            x = max(50, min(1750, x + random.uniform(-150, 150)))
            y = max(50, min(950, y + random.uniform(-150, 150)))
            d += f" C{cp1x:.1f},{cp1y:.1f} {cp2x:.1f},{cp2y:.1f} {x:.1f},{y:.1f}"
        elif cmd == 'Q':  # Quadratic bezier
            cpx = x + random.uniform(-150, 150)
            cpy = y + random.uniform(-150, 150)
            x = max(50, min(1750, x + random.uniform(-100, 100)))
            y = max(50, min(950, y + random.uniform(-100, 100)))
            d += f" Q{cpx:.1f},{cpy:.1f} {x:.1f},{y:.1f}"
        else:  # Line
            x = max(50, min(1750, x + random.uniform(-100, 100)))
            y = max(50, min(950, y + random.uniform(-100, 100)))
            d += f" L{x:.1f},{y:.1f}"

    d += " Z"
    return d


def generate_elements(num_elements, num_gradients, num_filters):
    """Generate diverse SVG elements."""
    elements = []

    for i in range(num_elements):
        elem_type = random.choice(['rect', 'circle', 'ellipse', 'path', 'polygon'])

        # Random fill - gradient, solid color, or none
        fill_type = random.choice(['gradient', 'solid', 'none'])
        if fill_type == 'gradient':
            grad_type = random.choice(['lg', 'rg'])
            grad_idx = random.randint(0, num_gradients - 1)
            fill = f"url(#{grad_type}{grad_idx})"
        elif fill_type == 'solid':
            fill = f"#{random.randint(0, 255):02x}{random.randint(0, 255):02x}{random.randint(0, 255):02x}"
        else:
            fill = "none"

        # Random stroke
        stroke_type = random.choice(['gradient', 'solid', 'none'])
        if stroke_type == 'gradient':
            grad_type = random.choice(['lg', 'rg'])
            grad_idx = random.randint(0, num_gradients - 1)
            stroke = f"url(#{grad_type}{grad_idx})"
        elif stroke_type == 'solid':
            stroke = f"#{random.randint(0, 255):02x}{random.randint(0, 255):02x}{random.randint(0, 255):02x}"
        else:
            stroke = "none"

        stroke_width = random.uniform(0.5, 5)
        opacity = random.uniform(0.2, 0.9)

        # Random filter (20% chance)
        filter_attr = ""
        if random.random() < 0.2:
            filter_attr = f' filter="url(#blur{random.randint(0, num_filters - 1)})"'

        # Random transform
        tx = random.uniform(-50, 50)
        ty = random.uniform(-50, 50)
        rot = random.uniform(-30, 30)
        scale = random.uniform(0.8, 1.2)
        transform = f'transform="translate({tx:.1f},{ty:.1f}) rotate({rot:.1f}) scale({scale:.2f})"'

        if elem_type == 'rect':
            x = random.uniform(0, 1600)
            y = random.uniform(0, 800)
            w = random.uniform(50, 300)
            h = random.uniform(50, 200)
            rx = random.uniform(0, 20)
            elements.append(
                f'    <rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" '
                f'rx="{rx:.1f}" fill="{fill}" stroke="{stroke}" stroke-width="{stroke_width:.1f}" '
                f'opacity="{opacity:.2f}" {transform}{filter_attr}/>'
            )

        elif elem_type == 'circle':
            cx = random.uniform(100, 1700)
            cy = random.uniform(100, 900)
            r = random.uniform(20, 150)
            elements.append(
                f'    <circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r:.1f}" '
                f'fill="{fill}" stroke="{stroke}" stroke-width="{stroke_width:.1f}" '
                f'opacity="{opacity:.2f}" {transform}{filter_attr}/>'
            )

        elif elem_type == 'ellipse':
            cx = random.uniform(100, 1700)
            cy = random.uniform(100, 900)
            rx = random.uniform(30, 200)
            ry = random.uniform(20, 150)
            elements.append(
                f'    <ellipse cx="{cx:.1f}" cy="{cy:.1f}" rx="{rx:.1f}" ry="{ry:.1f}" '
                f'fill="{fill}" stroke="{stroke}" stroke-width="{stroke_width:.1f}" '
                f'opacity="{opacity:.2f}" {transform}{filter_attr}/>'
            )

        elif elem_type == 'path':
            d = generate_complex_path()
            elements.append(
                f'    <path d="{d}" fill="{fill}" stroke="{stroke}" '
                f'stroke-width="{stroke_width:.1f}" opacity="{opacity:.2f}" {transform}{filter_attr}/>'
            )

        elif elem_type == 'polygon':
            # Generate 5-8 sided polygon
            num_points = random.randint(5, 8)
            cx = random.uniform(200, 1600)
            cy = random.uniform(200, 800)
            r = random.uniform(50, 150)
            points = []
            for j in range(num_points):
                angle = (j / num_points) * 2 * math.pi
                px = cx + r * math.cos(angle) * random.uniform(0.7, 1.3)
                py = cy + r * math.sin(angle) * random.uniform(0.7, 1.3)
                points.append(f"{px:.1f},{py:.1f}")
            elements.append(
                f'    <polygon points="{" ".join(points)}" fill="{fill}" stroke="{stroke}" '
                f'stroke-width="{stroke_width:.1f}" opacity="{opacity:.2f}" {transform}{filter_attr}/>'
            )

    return elements


def generate_fbf_animation(num_frames=600, cycle_duration=1.0):
    """Generate FBF.SVG animation structure for continuous rendering.

    This creates an invisible FBF.SVG-style animation (discrete href switching)
    that forces continuous re-renders at high frame rate.

    Args:
        num_frames: Number of unique frames (affects file size, ~80 bytes per frame)
        cycle_duration: Animation cycle duration in seconds
                       At 600 frames / 1.0s = 600 FPS animation rate
                       This ensures every render at <600 FPS sees a unique frame
    """
    # Create unique frame symbols (invisible 1x1 pixel rects)
    symbols = []
    for i in range(num_frames):
        symbols.append(f'    <symbol id="_bf{i}" viewBox="0 0 1 1"><rect fill="none"/></symbol>')

    # Build values string for discrete animation (cycles through all frames)
    values = ";".join([f"#_bf{i}" for i in range(num_frames)])

    animation = f'''  <!-- FBF.SVG animation driver: {num_frames} frames in {cycle_duration}s = {int(num_frames/cycle_duration)} FPS -->
  <!-- Ensures continuous re-renders for fair benchmark comparison -->
{chr(10).join(symbols)}

  <!-- Invisible animated element (1x1 pixel, off-screen) -->
  <use id="_benchanim" x="-100" y="-100" width="1" height="1" href="#_bf0">
    <animate attributeName="href"
             values="{values}"
             dur="{cycle_duration}s"
             repeatCount="indefinite"
             calcMode="discrete"/>
  </use>'''

    return animation


def generate_benchmark_svg(
    output_path: Path,
    num_elements: int = 500,
    num_linear_gradients: int = 20,
    num_radial_gradients: int = 20,
    num_filters: int = 10,
    width: int = 1920,
    height: int = 1080,
    seed: int = 42,
    add_fbf_animation: bool = True,
    animation_fps: int = 60
):
    """Generate the complete benchmark SVG file.

    Args:
        add_fbf_animation: Add FBF.SVG animation for continuous rendering (default: True)
        animation_fps: Target FPS for the animation driver (default: 60)
    """
    random.seed(seed)

    gradient_defs = generate_gradient_defs(num_linear_gradients, num_radial_gradients)
    filter_defs = generate_blur_filters(num_filters)
    elements = generate_elements(
        num_elements,
        num_linear_gradients + num_radial_gradients,
        num_filters
    )

    # Optional FBF animation for fbfsvg-player compatibility
    # 600 frames in 1 second = 600 FPS animation rate
    # This ensures every render at <600 FPS sees a unique frame
    fbf_animation = ""
    if add_fbf_animation:
        fbf_animation = "\n" + generate_fbf_animation(num_frames=600, cycle_duration=1.0)

    # Add xlink namespace if we have animation (needed for href in <use>)
    xlink_ns = ' xmlns:xlink="http://www.w3.org/1999/xlink"' if add_fbf_animation else ''

    anim_note = f"  <!-- FBF.SVG animation at {animation_fps} FPS for continuous rendering -->" if add_fbf_animation else ""

    svg = f'''<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg"{xlink_ns} viewBox="0 0 {width} {height}" width="{width}" height="{height}">
  <!-- Benchmark SVG for ThorVG vs fbfsvg-player comparison -->
  <!-- Contains {num_elements} elements with gradients and gaussian blur filters -->
  <!-- Generated with seed={seed} for reproducibility -->
{anim_note}

  <defs>
{chr(10).join(gradient_defs)}

{chr(10).join(filter_defs)}
  </defs>

  <!-- Background -->
  <rect width="100%" height="100%" fill="#1a1a2e"/>

  <!-- Complex elements -->
{chr(10).join(elements)}
{fbf_animation}
</svg>'''

    output_path.write_text(svg)
    print(f"Generated: {output_path}")
    print(f"  Elements: {num_elements}")
    print(f"  Gradients: {num_linear_gradients} linear + {num_radial_gradients} radial")
    print(f"  Blur filters: {num_filters}")
    print(f"  File size: {len(svg):,} bytes")


def main():
    parser = argparse.ArgumentParser(
        description='Generate complex static SVG for benchmarking'
    )
    parser.add_argument(
        '-o', '--output',
        type=Path,
        default=Path('svg_input_samples/benchmark_static.svg'),
        help='Output SVG file path'
    )
    parser.add_argument(
        '-n', '--num-elements',
        type=int,
        default=500,
        help='Number of shape elements (default: 500)'
    )
    parser.add_argument(
        '--width',
        type=int,
        default=1920,
        help='SVG width (default: 1920)'
    )
    parser.add_argument(
        '--height',
        type=int,
        default=1080,
        help='SVG height (default: 1080)'
    )
    parser.add_argument(
        '--seed',
        type=int,
        default=42,
        help='Random seed for reproducibility (default: 42)'
    )
    parser.add_argument(
        '--no-animation',
        action='store_true',
        help='Generate static SVG without FBF animation (for ThorVG-only tests)'
    )
    parser.add_argument(
        '--animation-fps',
        type=int,
        default=60,
        help='Target FPS for FBF animation driver (default: 60)'
    )

    args = parser.parse_args()

    # Create parent directory if needed
    args.output.parent.mkdir(parents=True, exist_ok=True)

    generate_benchmark_svg(
        output_path=args.output,
        num_elements=args.num_elements,
        width=args.width,
        height=args.height,
        seed=args.seed,
        add_fbf_animation=not args.no_animation,
        animation_fps=args.animation_fps
    )


if __name__ == '__main__':
    main()
