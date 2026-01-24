#!/usr/bin/env python3
"""Generate 1000 complex numbered SVG frames for benchmark testing.

Creates a folder of SVG files named frame_00001.svg, frame_00002.svg, etc.
Each frame contains:
- Gaussian blur effects
- Linear gradients
- Radial/conical gradients
- Various opacity and overlapping
- Elements nested 7 levels deep with different transforms
- Closed paths
- Basic text
- Clip paths
- Masks
"""

import os
import sys
import math
import random

def generate_svg_frame(frame_num: int, total_frames: int, num_elements: int = 25) -> str:
    """Generate a single complex SVG frame.

    Args:
        frame_num: Current frame number (1-indexed)
        total_frames: Total number of frames in sequence
        num_elements: Number of complex elements to render

    Returns:
        SVG content as string
    """
    # Progress through animation cycle (0.0 to 1.0)
    t = (frame_num - 1) / max(total_frames - 1, 1)

    # Seed random for reproducibility per frame
    random.seed(frame_num * 12345)

    svg_parts = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1920 1080" width="1920" height="1080">',
        '  <defs>',
    ]

    # Define Gaussian blur filters with varying intensity
    for i in range(5):
        blur_val = 1 + i * 2
        svg_parts.append(f'''    <filter id="blur{i}" x="-50%" y="-50%" width="200%" height="200%">
      <feGaussianBlur in="SourceGraphic" stdDeviation="{blur_val}"/>
    </filter>''')

    # Complex drop shadow filters
    svg_parts.append('''    <filter id="shadow" x="-50%" y="-50%" width="200%" height="200%">
      <feGaussianBlur in="SourceAlpha" stdDeviation="3" result="blur"/>
      <feOffset in="blur" dx="4" dy="4" result="offsetBlur"/>
      <feMerge>
        <feMergeNode in="offsetBlur"/>
        <feMergeNode in="SourceGraphic"/>
      </feMerge>
    </filter>''')

    # Linear gradients with multiple stops
    gradient_colors = [
        ["#FF6B6B", "#4ECDC4", "#45B7D1", "#96E6A1"],
        ["#FFE66D", "#FF6B6B", "#C44D58", "#8B0000"],
        ["#4169E1", "#7B68EE", "#9370DB", "#FFD700"],
        ["#32CD32", "#228B22", "#006400", "#ADFF2F"],
        ["#FF1493", "#FF69B4", "#FFB6C1", "#FFC0CB"],
    ]

    for i, colors in enumerate(gradient_colors):
        # Linear gradient with angle based on frame
        angle = (45 + i * 30 + frame_num * 0.5) % 360
        rad = math.radians(angle)
        x2 = 50 + 50 * math.cos(rad)
        y2 = 50 + 50 * math.sin(rad)
        svg_parts.append(f'''    <linearGradient id="lgrad{i}" x1="0%" y1="0%" x2="{x2:.1f}%" y2="{y2:.1f}%">
      <stop offset="0%" stop-color="{colors[0]}"/>
      <stop offset="33%" stop-color="{colors[1]}"/>
      <stop offset="66%" stop-color="{colors[2]}"/>
      <stop offset="100%" stop-color="{colors[3]}"/>
    </linearGradient>''')

    # Radial gradients
    for i, colors in enumerate(gradient_colors):
        cx = 30 + (i * 10 + frame_num * 0.3) % 40
        cy = 30 + (i * 8 + frame_num * 0.2) % 40
        svg_parts.append(f'''    <radialGradient id="rgrad{i}" cx="{cx:.1f}%" cy="{cy:.1f}%" r="60%">
      <stop offset="0%" stop-color="{colors[0]}" stop-opacity="1"/>
      <stop offset="50%" stop-color="{colors[1]}" stop-opacity="0.8"/>
      <stop offset="100%" stop-color="{colors[2]}" stop-opacity="0.6"/>
    </radialGradient>''')

    # Define clip paths - various shapes for clipping
    # Circular clip path
    svg_parts.append(f'''    <clipPath id="clipCircle">
      <circle cx="0" cy="0" r="50"/>
    </clipPath>''')

    # Star-shaped clip path
    star_points = []
    for j in range(10):
        angle = j * math.pi / 5 - math.pi / 2
        r = 50 if j % 2 == 0 else 25
        px = r * math.cos(angle)
        py = r * math.sin(angle)
        star_points.append(f"{px:.1f},{py:.1f}")
    star_path = "M" + " L".join(star_points) + "Z"
    svg_parts.append(f'''    <clipPath id="clipStar">
      <path d="{star_path}"/>
    </clipPath>''')

    # Hexagon clip path
    hex_points = []
    for j in range(6):
        angle = j * math.pi / 3 + math.pi / 6
        px = 45 * math.cos(angle)
        py = 45 * math.sin(angle)
        hex_points.append(f"{px:.1f},{py:.1f}")
    hex_path = "M" + " L".join(hex_points) + "Z"
    svg_parts.append(f'''    <clipPath id="clipHex">
      <path d="{hex_path}"/>
    </clipPath>''')

    # Rectangle with rounded corners clip path
    svg_parts.append(f'''    <clipPath id="clipRect">
      <rect x="-40" y="-30" width="80" height="60" rx="10"/>
    </clipPath>''')

    # Dynamic clip paths that vary per frame
    dynamic_r = 35 + 15 * math.sin(t * math.pi * 2)
    svg_parts.append(f'''    <clipPath id="clipDynamic">
      <circle cx="0" cy="0" r="{dynamic_r:.1f}"/>
    </clipPath>''')

    # Define masks - for alpha/luminance masking
    # Gradient mask (soft edges)
    svg_parts.append(f'''    <mask id="maskGradient" maskContentUnits="objectBoundingBox">
      <rect x="0" y="0" width="1" height="1" fill="url(#maskGrad)"/>
    </mask>
    <linearGradient id="maskGrad" x1="0%" y1="0%" x2="100%" y2="100%">
      <stop offset="0%" stop-color="white"/>
      <stop offset="100%" stop-color="black"/>
    </linearGradient>''')

    # Circular fade mask
    svg_parts.append(f'''    <mask id="maskCircleFade">
      <circle cx="0" cy="0" r="60" fill="white"/>
      <circle cx="0" cy="0" r="30" fill="black"/>
    </mask>''')

    # Striped mask
    svg_parts.append(f'''    <mask id="maskStripes">
      <rect x="-50" y="-50" width="100" height="100" fill="white"/>
      <rect x="-50" y="-40" width="100" height="10" fill="black"/>
      <rect x="-50" y="-20" width="100" height="10" fill="black"/>
      <rect x="-50" y="0" width="100" height="10" fill="black"/>
      <rect x="-50" y="20" width="100" height="10" fill="black"/>
      <rect x="-50" y="40" width="100" height="10" fill="black"/>
    </mask>''')

    # Dynamic mask that varies per frame
    mask_opacity = 0.3 + 0.7 * abs(math.sin(t * math.pi))
    svg_parts.append(f'''    <mask id="maskDynamic">
      <rect x="-60" y="-60" width="120" height="120" fill="white" fill-opacity="{mask_opacity:.2f}"/>
      <circle cx="0" cy="0" r="25" fill="white"/>
    </mask>''')

    svg_parts.append('  </defs>')

    # Background with gradient
    svg_parts.append('  <rect width="1920" height="1080" fill="url(#lgrad0)"/>')

    # List of clip paths and masks to cycle through
    clip_paths = ["clipCircle", "clipStar", "clipHex", "clipRect", "clipDynamic"]
    masks = ["maskGradient", "maskCircleFade", "maskStripes", "maskDynamic"]

    # Generate complex nested elements
    for elem_id in range(num_elements):
        # Base position varies per frame
        base_x = 100 + (elem_id % 8) * 220 + math.sin(t * 2 * math.pi + elem_id * 0.5) * 30
        base_y = 100 + (elem_id // 8) * 280 + math.cos(t * 2 * math.pi + elem_id * 0.7) * 25

        # Element properties
        grad_type = "lgrad" if elem_id % 3 == 0 else "rgrad"
        grad_idx = elem_id % len(gradient_colors)
        blur_idx = elem_id % 5
        base_opacity = 0.5 + 0.4 * math.sin(t * math.pi + elem_id * 0.3)

        # Select clip path and mask for this element
        clip_id = clip_paths[elem_id % len(clip_paths)]
        mask_id = masks[elem_id % len(masks)]

        # Decide which clipping/masking to use (alternate between clip-path and mask)
        use_clip = elem_id % 3 == 0
        use_mask = elem_id % 3 == 1
        # elem_id % 3 == 2 uses neither (for variety)

        clip_attr = f' clip-path="url(#{clip_id})"' if use_clip else ''
        mask_attr = f' mask="url(#{mask_id})"' if use_mask else ''

        # Start 7-level nesting with different transforms at each level
        transforms = []
        for level in range(7):
            # Different transform at each nesting level
            tx = math.sin(t * math.pi * 2 + level * 0.5 + elem_id) * 5
            ty = math.cos(t * math.pi * 2 + level * 0.3 + elem_id) * 4
            rot = math.sin(t * math.pi + level * 0.2) * 5 * (level + 1)
            scale = 1.0 - level * 0.02
            transforms.append(f'translate({tx:.2f},{ty:.2f}) rotate({rot:.2f}) scale({scale:.3f})')

        # Inner content - complex shape
        shape_type = elem_id % 4
        size = 60 + math.sin(t * math.pi + elem_id) * 15

        if shape_type == 0:
            # Complex closed path (star)
            points = []
            for j in range(10):
                angle = j * math.pi / 5 - math.pi / 2
                r = size if j % 2 == 0 else size * 0.5
                px = r * math.cos(angle)
                py = r * math.sin(angle)
                points.append(f"{px:.1f},{py:.1f}")
            path_d = "M" + " L".join(points) + "Z"
            inner = f'<path d="{path_d}" fill="url(#{grad_type}{grad_idx})" filter="url(#blur{blur_idx})" opacity="{base_opacity:.2f}"/>'

        elif shape_type == 1:
            # Rounded rect with inner circle
            inner = f'''<rect x="{-size/2:.1f}" y="{-size/2:.1f}" width="{size:.1f}" height="{size:.1f}" rx="{size/4:.1f}" fill="url(#{grad_type}{grad_idx})" filter="url(#shadow)" opacity="{base_opacity:.2f}"/>
        <circle cx="0" cy="0" r="{size/3:.1f}" fill="url(#rgrad{(grad_idx+1)%5})" opacity="{base_opacity*0.7:.2f}"/>'''

        elif shape_type == 2:
            # Complex closed path (hexagon)
            points = []
            for j in range(6):
                angle = j * math.pi / 3 + math.pi / 6
                px = size * 0.6 * math.cos(angle)
                py = size * 0.6 * math.sin(angle)
                points.append(f"{px:.1f},{py:.1f}")
            path_d = "M" + " L".join(points) + "Z"
            inner = f'''<path d="{path_d}" fill="url(#{grad_type}{grad_idx})" filter="url(#blur{blur_idx})" opacity="{base_opacity:.2f}"/>
        <ellipse cx="0" cy="0" rx="{size/4:.1f}" ry="{size/6:.1f}" fill="#FFFFFF" opacity="{base_opacity*0.5:.2f}"/>'''

        else:
            # Circle with text
            inner = f'''<circle cx="0" cy="0" r="{size/2:.1f}" fill="url(#{grad_type}{grad_idx})" filter="url(#shadow)" opacity="{base_opacity:.2f}"/>
        <text x="0" y="5" text-anchor="middle" font-family="Arial, sans-serif" font-size="14" fill="#FFFFFF" opacity="{base_opacity:.2f}">{frame_num}</text>'''

        # Build nested group structure (7 levels)
        # Apply clip-path at level 3 and mask at level 5 for complex interaction
        result = inner
        for level in range(6, -1, -1):
            extra_attrs = ""
            if level == 3 and use_clip:
                extra_attrs = clip_attr
            elif level == 5 and use_mask:
                extra_attrs = mask_attr
            result = f'<g transform="{transforms[level]}"{extra_attrs}>\n        {result}\n      </g>'

        svg_parts.append(f'''  <g transform="translate({base_x:.1f}, {base_y:.1f})">
      {result}
  </g>''')

    # Add overlapping semi-transparent layer on top with masks
    for i in range(3):
        x = 300 + i * 500 + math.sin(t * math.pi * 2 + i) * 100
        y = 300 + math.cos(t * math.pi * 2 + i * 0.7) * 80
        mask_ref = masks[i % len(masks)]
        svg_parts.append(f'''  <g transform="translate({x:.1f}, {y:.1f})" mask="url(#{mask_ref})">
    <ellipse cx="0" cy="0" rx="200" ry="150" fill="url(#rgrad{i%5})" filter="url(#blur3)" opacity="0.3"/>
  </g>''')

    svg_parts.append('</svg>')
    return '\n'.join(svg_parts)


def main():
    if len(sys.argv) < 2:
        print("Usage: python generate_complex_frames.py <output_folder> [num_frames] [num_elements]")
        print("  num_frames: Number of frames to generate (default: 1000)")
        print("  num_elements: Number of complex elements per frame (default: 25)")
        sys.exit(1)

    output_folder = sys.argv[1]
    num_frames = int(sys.argv[2]) if len(sys.argv) > 2 else 1000
    num_elements = int(sys.argv[3]) if len(sys.argv) > 3 else 25

    os.makedirs(output_folder, exist_ok=True)

    print(f"Generating {num_frames} complex frames with {num_elements} elements each...")
    print("Features: Gaussian blur, linear/radial gradients, 7-level nesting, paths, text, clip-paths, masks")

    for i in range(1, num_frames + 1):
        svg_content = generate_svg_frame(i, num_frames, num_elements)
        filename = f"frame_{i:05d}.svg"
        filepath = os.path.join(output_folder, filename)
        with open(filepath, 'w') as f:
            f.write(svg_content)

        if i % 100 == 0:
            print(f"  Generated {i}/{num_frames} frames")

    # Print summary
    total_size = sum(os.path.getsize(os.path.join(output_folder, f))
                     for f in os.listdir(output_folder) if f.endswith('.svg'))
    print(f"Done! {num_frames} frames saved to: {output_folder}")
    print(f"Total size: {total_size / (1024*1024):.2f} MB")


if __name__ == "__main__":
    main()
