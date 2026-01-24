#!/usr/bin/env python3
"""
Generate 500-frame benchmark SVG with 1/4 partial update patterns.

This SVG tests dirty region tracking optimization:
- 500 unique visible frames (not invisible placeholders)
- Canvas divided into 4 quadrants
- Every 10 frames, only ONE quadrant changes
- Other 9 frames: ALL quadrants change

Performance stress features:
- Linear and radial gradient fills for every element
- 7 levels of nested group transforms per element
- Complex transform chains (translate, rotate, scale, skew)

This pattern allows benchmarking partial rendering optimization:
- 10% of frames: only 25% of area needs redraw
- 90% of frames: full redraw needed

Usage:
    python generate_partial_benchmark_svg.py -o benchmark_partial.svg
"""

import random
import math
import argparse
from pathlib import Path


def generate_gradient_defs(frame: int, quadrant: int, num_gradients: int = 50):
    """Generate gradient definitions for a frame/quadrant.

    Args:
        frame: Frame number
        quadrant: Quadrant index (0-3)
        num_gradients: Number of gradients to generate

    Returns:
        Tuple of (defs_content, gradient_ids_list)
    """
    random.seed(frame * 4 + quadrant + 12345)

    defs = []
    gradient_ids = []

    for i in range(num_gradients):
        grad_id = f"g{frame}q{quadrant}_{i}"
        gradient_ids.append(grad_id)

        # Alternate between linear and radial gradients
        if i % 2 == 0:
            # Linear gradient with random angle
            x1 = random.uniform(0, 100)
            y1 = random.uniform(0, 100)
            x2 = random.uniform(0, 100)
            y2 = random.uniform(0, 100)

            # 3-5 color stops
            num_stops = random.randint(3, 5)
            stops = []
            for s in range(num_stops):
                offset = s / (num_stops - 1) * 100
                color = f"#{random.randint(20, 255):02x}{random.randint(20, 255):02x}{random.randint(20, 255):02x}"
                opacity = random.uniform(0.5, 1.0)
                stops.append(f'        <stop offset="{offset:.0f}%" stop-color="{color}" stop-opacity="{opacity:.2f}"/>')

            defs.append(f'''      <linearGradient id="{grad_id}" x1="{x1:.0f}%" y1="{y1:.0f}%" x2="{x2:.0f}%" y2="{y2:.0f}%">
{chr(10).join(stops)}
      </linearGradient>''')
        else:
            # Radial gradient
            cx = random.uniform(20, 80)
            cy = random.uniform(20, 80)
            r = random.uniform(40, 80)
            fx = cx + random.uniform(-20, 20)
            fy = cy + random.uniform(-20, 20)

            # 3-5 color stops
            num_stops = random.randint(3, 5)
            stops = []
            for s in range(num_stops):
                offset = s / (num_stops - 1) * 100
                color = f"#{random.randint(20, 255):02x}{random.randint(20, 255):02x}{random.randint(20, 255):02x}"
                opacity = random.uniform(0.5, 1.0)
                stops.append(f'        <stop offset="{offset:.0f}%" stop-color="{color}" stop-opacity="{opacity:.2f}"/>')

            defs.append(f'''      <radialGradient id="{grad_id}" cx="{cx:.0f}%" cy="{cy:.0f}%" r="{r:.0f}%" fx="{fx:.0f}%" fy="{fy:.0f}%">
{chr(10).join(stops)}
      </radialGradient>''')

    return '\n'.join(defs), gradient_ids


def generate_nested_transform_groups(content: str, depth: int = 7) -> str:
    """Wrap content in nested groups with transforms.

    Each level applies a different transform type for maximum stress testing.

    Args:
        content: The inner SVG content
        depth: Number of nesting levels (default 7)

    Returns:
        Content wrapped in nested transform groups
    """
    result = content
    indent = "        "

    # Different transform types for each nesting level
    transform_generators = [
        lambda: f"translate({random.uniform(-3, 3):.2f}, {random.uniform(-3, 3):.2f})",
        lambda: f"rotate({random.uniform(-5, 5):.2f})",
        lambda: f"scale({random.uniform(0.98, 1.02):.4f}, {random.uniform(0.98, 1.02):.4f})",
        lambda: f"skewX({random.uniform(-2, 2):.2f})",
        lambda: f"skewY({random.uniform(-2, 2):.2f})",
        lambda: f"translate({random.uniform(-2, 2):.2f}, {random.uniform(-2, 2):.2f})",
        lambda: f"rotate({random.uniform(-3, 3):.2f})",
    ]

    for level in range(depth):
        transform = transform_generators[level % len(transform_generators)]()
        result = f'{indent}<g transform="{transform}">\n{result}\n{indent}</g>'
        indent += "  "

    return result


def generate_quadrant_content(quadrant: int, frame: int, qw: float, qh: float,
                              min_elements: int = 80, max_elements: int = 120,
                              max_gradients: int = 50):
    """Generate diverse SVG content for one quadrant with gradients and nested transforms.

    Args:
        quadrant: 0=top-left, 1=top-right, 2=bottom-left, 3=bottom-right
        frame: Frame number (for variation)
        qw: Quadrant width
        qh: Quadrant height
        min_elements: Minimum elements per quadrant
        max_elements: Maximum elements per quadrant
        max_gradients: Maximum gradient definitions per quadrant

    Returns:
        Tuple of (gradient_defs, element_content)
    """
    # Seed based on frame and quadrant for reproducible randomness
    random.seed(frame * 4 + quadrant + 42)

    # Element count affects both rendering complexity and file size
    num_elements = random.randint(min_elements, max_elements)
    num_gradients = min(max_gradients, num_elements)

    # Generate gradient definitions for this quadrant
    gradient_defs, gradient_ids = generate_gradient_defs(frame, quadrant, num_gradients)

    elements = []

    # Offset based on quadrant position
    ox = (quadrant % 2) * qw
    oy = (quadrant // 2) * qh

    for i in range(num_elements):
        # Reseed for this specific element
        random.seed(frame * 4 + quadrant + 42 + i * 1000)

        elem_type = random.choice(['rect', 'circle', 'ellipse', 'polygon'])

        # Use gradient fill
        grad_idx = i % len(gradient_ids)
        fill = f"url(#{gradient_ids[grad_idx]})"

        # Gradient stroke for extra complexity
        stroke_grad_idx = (i + 1) % len(gradient_ids)
        stroke = f"url(#{gradient_ids[stroke_grad_idx]})"
        stroke_width = random.uniform(2, 5)
        opacity = random.uniform(0.5, 0.95)

        # Generate the base element
        if elem_type == 'rect':
            x = ox + random.uniform(10, qw - 100)
            y = oy + random.uniform(10, qh - 100)
            w = random.uniform(50, min(150, qw - x + ox - 10))
            h = random.uniform(30, min(100, qh - y + oy - 10))
            rx = random.uniform(0, 15)
            base_elem = (
                f'<rect x="{x:.1f}" y="{y:.1f}" width="{w:.1f}" height="{h:.1f}" '
                f'rx="{rx:.1f}" fill="{fill}" stroke="{stroke}" stroke-width="{stroke_width:.1f}" '
                f'opacity="{opacity:.2f}"/>'
            )

        elif elem_type == 'circle':
            r = random.uniform(15, 60)
            cx = ox + random.uniform(r + 5, qw - r - 5)
            cy = oy + random.uniform(r + 5, qh - r - 5)
            base_elem = (
                f'<circle cx="{cx:.1f}" cy="{cy:.1f}" r="{r:.1f}" '
                f'fill="{fill}" stroke="{stroke}" stroke-width="{stroke_width:.1f}" '
                f'opacity="{opacity:.2f}"/>'
            )

        elif elem_type == 'ellipse':
            rx = random.uniform(20, 80)
            ry = random.uniform(15, 50)
            cx = ox + random.uniform(rx + 5, qw - rx - 5)
            cy = oy + random.uniform(ry + 5, qh - ry - 5)
            base_elem = (
                f'<ellipse cx="{cx:.1f}" cy="{cy:.1f}" rx="{rx:.1f}" ry="{ry:.1f}" '
                f'fill="{fill}" stroke="{stroke}" stroke-width="{stroke_width:.1f}" '
                f'opacity="{opacity:.2f}"/>'
            )

        elif elem_type == 'polygon':
            # 4-6 sided polygon
            num_points = random.randint(4, 6)
            r = random.uniform(30, 70)
            cx = ox + random.uniform(r + 10, qw - r - 10)
            cy = oy + random.uniform(r + 10, qh - r - 10)
            points = []
            for j in range(num_points):
                angle = (j / num_points) * 2 * math.pi + random.uniform(-0.2, 0.2)
                px = cx + r * math.cos(angle) * random.uniform(0.6, 1.0)
                py = cy + r * math.sin(angle) * random.uniform(0.6, 1.0)
                points.append(f"{px:.1f},{py:.1f}")
            base_elem = (
                f'<polygon points="{" ".join(points)}" fill="{fill}" stroke="{stroke}" '
                f'stroke-width="{stroke_width:.1f}" opacity="{opacity:.2f}"/>'
            )

        # Wrap in 7 levels of nested transform groups
        nested_elem = generate_nested_transform_groups(base_elem, depth=7)
        elements.append(nested_elem)

    return gradient_defs, '\n'.join(elements)


def generate_frame_symbol(frame: int, width: int, height: int,
                          partial_quadrant: int = -1, base_frame: int = 0,
                          min_elements: int = 80, max_elements: int = 120,
                          max_gradients: int = 50):
    """Generate a frame symbol with 4 quadrants using gradients and nested transforms.

    Args:
        frame: Frame number
        width: Full canvas width
        height: Full canvas height
        partial_quadrant: If >= 0, only this quadrant differs from base_frame
                         (-1 means all quadrants are unique)
        base_frame: For partial updates, the frame to copy unchanged quadrants from
        min_elements: Minimum elements per quadrant
        max_elements: Maximum elements per quadrant
        max_gradients: Maximum gradient definitions per quadrant
    """
    qw = width / 2
    qh = height / 2

    quadrant_defs = []
    quadrant_content = []

    for q in range(4):
        if partial_quadrant >= 0 and q != partial_quadrant:
            # Use base_frame content for unchanged quadrants
            defs, content = generate_quadrant_content(q, base_frame, qw, qh,
                                                       min_elements, max_elements, max_gradients)
        else:
            # Generate unique content for this quadrant
            defs, content = generate_quadrant_content(q, frame, qw, qh,
                                                       min_elements, max_elements, max_gradients)
        quadrant_defs.append(defs)
        quadrant_content.append(content)

    # Background color varies by frame for visual feedback
    random.seed(frame + 1000)
    bg_color = f"#{random.randint(15, 40):02x}{random.randint(15, 50):02x}{random.randint(30, 70):02x}"

    # Frame number indicator in top-right corner (always visible)
    frame_label = f'      <text x="{width - 20}" y="40" font-size="36" fill="#ffffff" text-anchor="end" font-family="monospace">Frame {frame}</text>'

    # Combine all gradient definitions
    all_defs = '\n'.join(quadrant_defs)

    return f'''  <symbol id="_f{frame}" viewBox="0 0 {width} {height}">
    <defs>
{all_defs}
    </defs>

    <!-- Background -->
    <rect width="{width}" height="{height}" fill="{bg_color}"/>

    <!-- Quadrant divider lines -->
    <line x1="{qw}" y1="0" x2="{qw}" y2="{height}" stroke="#333" stroke-width="2"/>
    <line x1="0" y1="{qh}" x2="{width}" y2="{qh}" stroke="#333" stroke-width="2"/>

    <!-- Quadrant 0: Top-Left -->
    <g id="_f{frame}_q0">
{quadrant_content[0]}
    </g>

    <!-- Quadrant 1: Top-Right -->
    <g id="_f{frame}_q1">
{quadrant_content[1]}
    </g>

    <!-- Quadrant 2: Bottom-Left -->
    <g id="_f{frame}_q2">
{quadrant_content[2]}
    </g>

    <!-- Quadrant 3: Bottom-Right -->
    <g id="_f{frame}_q3">
{quadrant_content[3]}
    </g>

    <!-- Frame number indicator -->
{frame_label}
  </symbol>'''


def generate_partial_benchmark_svg(
    output_path: Path,
    num_frames: int = 500,
    width: int = 1920,
    height: int = 1080,
    duration: float = 10.0,
    partial_every: int = 10,
    min_elements: int = 80,
    max_elements: int = 120,
    max_gradients: int = 50
):
    """Generate benchmark SVG with partial update pattern.

    Pattern:
    - Frames 0, 10, 20, 30... : Only quadrant 0 changes from previous base
    - Frames 1, 11, 21, 31... : Only quadrant 1 changes from previous base
    - Frames 2, 12, 22, 32... : Only quadrant 2 changes from previous base
    - Frames 3, 13, 23, 33... : Only quadrant 3 changes from previous base
    - Frames 4-9, 14-19, 24-29... : ALL quadrants change (full redraw)

    Args:
        num_frames: Total number of frames
        width: Canvas width
        height: Canvas height
        duration: Animation cycle duration in seconds
        partial_every: How often partial frames occur (default 10)
    """
    symbols = []
    partial_frame_count = 0
    full_frame_count = 0

    # Base frame for partial updates (changes every partial_every frames)
    base_frame = 0

    print(f"Generating {num_frames} frames with gradients and 7-level transform nesting...")

    for frame in range(num_frames):
        if frame % 50 == 0:
            print(f"  Frame {frame}/{num_frames}...")

        # Determine if this is a partial update frame
        frame_in_cycle = frame % partial_every

        if frame_in_cycle < 4:
            # Partial update: only one quadrant changes
            # The quadrant that changes cycles 0,1,2,3
            partial_quadrant = frame_in_cycle
            symbols.append(generate_frame_symbol(frame, width, height,
                                                  partial_quadrant=partial_quadrant,
                                                  base_frame=base_frame,
                                                  min_elements=min_elements,
                                                  max_elements=max_elements,
                                                  max_gradients=max_gradients))
            partial_frame_count += 1
        else:
            # Full update: all quadrants change
            symbols.append(generate_frame_symbol(frame, width, height,
                                                  partial_quadrant=-1,
                                                  min_elements=min_elements,
                                                  max_elements=max_elements,
                                                  max_gradients=max_gradients))
            full_frame_count += 1

            # Update base_frame at frame 4, 14, 24, etc. (first full frame after partials)
            if frame_in_cycle == 4:
                base_frame = frame

    # Build animation values (href references)
    values = ";".join([f"#_f{i}" for i in range(num_frames)])

    fps = num_frames / duration

    svg = f'''<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink"
     viewBox="0 0 {width} {height}" width="{width}" height="{height}">
  <!--
    Partial Update Benchmark SVG (Gradient + Transform Stress Test)
    ================================================================
    Total frames: {num_frames}
    Duration: {duration}s ({fps:.1f} FPS)

    Performance stress features:
    - Linear and radial gradient fills on all elements
    - Gradient strokes for additional complexity
    - 7 levels of nested transform groups per element
    - Transform types: translate, rotate, scale, skewX, skewY

    Update pattern (repeats every {partial_every} frames):
    - Frame 0,10,20...: Only quadrant 0 changes (top-left)
    - Frame 1,11,21...: Only quadrant 1 changes (top-right)
    - Frame 2,12,22...: Only quadrant 2 changes (bottom-left)
    - Frame 3,13,23...: Only quadrant 3 changes (bottom-right)
    - Frame 4-9,14-19..: ALL quadrants change (full redraw)

    Statistics:
    - Partial frames: {partial_frame_count} ({100*partial_frame_count/num_frames:.0f}%)
    - Full frames: {full_frame_count} ({100*full_frame_count/num_frames:.0f}%)

    Expected behavior with dirty region tracking:
    - {100*partial_frame_count/num_frames:.0f}% of frames should render ~25% of area
    - {100*full_frame_count/num_frames:.0f}% of frames require full render
  -->

  <defs>
{chr(10).join(symbols)}
  </defs>

  <!-- Animated use element displays current frame -->
  <use id="_anim" x="0" y="0" width="{width}" height="{height}" href="#_f0">
    <animate attributeName="href"
             values="{values}"
             dur="{duration}s"
             repeatCount="indefinite"
             calcMode="discrete"/>
  </use>
</svg>'''

    output_path.write_text(svg)

    print(f"\nGenerated: {output_path}")
    print(f"  Frames: {num_frames} ({fps:.1f} FPS)")
    print(f"  Duration: {duration}s")
    print(f"  Resolution: {width}x{height}")
    print(f"  Partial frames: {partial_frame_count} ({100*partial_frame_count/num_frames:.0f}%)")
    print(f"  Full frames: {full_frame_count} ({100*full_frame_count/num_frames:.0f}%)")
    print(f"  File size: {len(svg):,} bytes ({len(svg)/1024/1024:.1f} MB)")
    print(f"  Features: Gradient fills (linear+radial), 7-level nested transforms")


def main():
    parser = argparse.ArgumentParser(
        description='Generate partial update benchmark SVG with gradients and nested transforms'
    )
    parser.add_argument(
        '-o', '--output',
        type=Path,
        default=Path('svg_input_samples/benchmark_partial.svg'),
        help='Output SVG file path'
    )
    parser.add_argument(
        '-n', '--num-frames',
        type=int,
        default=500,
        help='Number of frames (default: 500)'
    )
    parser.add_argument(
        '-d', '--duration',
        type=float,
        default=10.0,
        help='Animation duration in seconds (default: 10)'
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
        '--partial-every',
        type=int,
        default=10,
        help='Partial update every N frames (default: 10)'
    )
    parser.add_argument(
        '--min-elements',
        type=int,
        default=80,
        help='Minimum elements per quadrant (default: 80)'
    )
    parser.add_argument(
        '--max-elements',
        type=int,
        default=120,
        help='Maximum elements per quadrant (default: 120)'
    )
    parser.add_argument(
        '--max-gradients',
        type=int,
        default=50,
        help='Maximum gradients per quadrant (default: 50)'
    )

    args = parser.parse_args()

    # Create parent directory if needed
    args.output.parent.mkdir(parents=True, exist_ok=True)

    generate_partial_benchmark_svg(
        output_path=args.output,
        num_frames=args.num_frames,
        width=args.width,
        height=args.height,
        duration=args.duration,
        partial_every=args.partial_every,
        min_elements=args.min_elements,
        max_elements=args.max_elements,
        max_gradients=args.max_gradients
    )


if __name__ == '__main__':
    main()
