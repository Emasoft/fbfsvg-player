#!/usr/bin/env python3
"""Generate 1000 extreme complexity SVG frames for benchmark testing.

Creates frames with 1000+ elements per frame including:
- Giant text characters sized as the whole image
- Rotated and clipped text
- Overlays and masks
- Gaussian blur effects
- Linear and radial gradients
- 7-level nested transforms
- Closed paths
- Clip paths and masks on everything
- Large cubic bezier paths with 500+ control points each
- Bezier figures covering half the screen (960x540+)
"""

import os
import sys
import math
import random

# Characters to use for giant text
TEXT_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"

def generate_svg_frame(frame_num: int, total_frames: int, num_elements: int = 1000) -> str:
    """Generate a single extreme complexity SVG frame.

    Args:
        frame_num: Current frame number (1-indexed)
        total_frames: Total number of frames in sequence
        num_elements: Number of elements to render (minimum 1000)

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

    # Define 20 Gaussian blur filters with varying intensity
    for i in range(20):
        blur_val = 0.5 + i * 1.5
        svg_parts.append(f'''    <filter id="blur{i}" x="-100%" y="-100%" width="300%" height="300%">
      <feGaussianBlur in="SourceGraphic" stdDeviation="{blur_val:.1f}"/>
    </filter>''')

    # Complex drop shadow with color
    for i in range(5):
        color = ["#000000", "#FF0000", "#00FF00", "#0000FF", "#FF00FF"][i]
        svg_parts.append(f'''    <filter id="shadow{i}" x="-100%" y="-100%" width="300%" height="300%">
      <feGaussianBlur in="SourceAlpha" stdDeviation="5" result="blur"/>
      <feOffset in="blur" dx="{5+i*2}" dy="{5+i*2}" result="offsetBlur"/>
      <feFlood flood-color="{color}" flood-opacity="0.5" result="color"/>
      <feComposite in="color" in2="offsetBlur" operator="in" result="shadow"/>
      <feMerge>
        <feMergeNode in="shadow"/>
        <feMergeNode in="SourceGraphic"/>
      </feMerge>
    </filter>''')

    # 20 Linear gradients with multiple stops
    gradient_colors = [
        ["#FF6B6B", "#4ECDC4", "#45B7D1", "#96E6A1", "#FFD93D"],
        ["#FFE66D", "#FF6B6B", "#C44D58", "#8B0000", "#2E0219"],
        ["#4169E1", "#7B68EE", "#9370DB", "#FFD700", "#FF4500"],
        ["#32CD32", "#228B22", "#006400", "#ADFF2F", "#7CFC00"],
        ["#FF1493", "#FF69B4", "#FFB6C1", "#FFC0CB", "#FFFFFF"],
        ["#00CED1", "#20B2AA", "#48D1CC", "#40E0D0", "#7FFFD4"],
        ["#FF8C00", "#FFA500", "#FFD700", "#FFFF00", "#ADFF2F"],
        ["#8A2BE2", "#9400D3", "#9932CC", "#BA55D3", "#DA70D6"],
        ["#DC143C", "#FF0000", "#FF4500", "#FF6347", "#FF7F50"],
        ["#1E90FF", "#00BFFF", "#87CEEB", "#87CEFA", "#B0E0E6"],
    ]

    for i in range(20):
        colors = gradient_colors[i % len(gradient_colors)]
        # Linear gradient with rotating angle per frame
        angle = (i * 18 + frame_num * 2) % 360
        rad = math.radians(angle)
        x2 = 50 + 50 * math.cos(rad)
        y2 = 50 + 50 * math.sin(rad)
        svg_parts.append(f'''    <linearGradient id="lgrad{i}" x1="0%" y1="0%" x2="{x2:.1f}%" y2="{y2:.1f}%">
      <stop offset="0%" stop-color="{colors[0]}"/>
      <stop offset="25%" stop-color="{colors[1]}"/>
      <stop offset="50%" stop-color="{colors[2]}"/>
      <stop offset="75%" stop-color="{colors[3]}"/>
      <stop offset="100%" stop-color="{colors[4]}"/>
    </linearGradient>''')

    # 20 Radial gradients
    for i in range(20):
        colors = gradient_colors[i % len(gradient_colors)]
        cx = 30 + (i * 5 + frame_num * 0.5) % 40
        cy = 30 + (i * 7 + frame_num * 0.3) % 40
        svg_parts.append(f'''    <radialGradient id="rgrad{i}" cx="{cx:.1f}%" cy="{cy:.1f}%" r="70%">
      <stop offset="0%" stop-color="{colors[0]}" stop-opacity="1"/>
      <stop offset="30%" stop-color="{colors[1]}" stop-opacity="0.9"/>
      <stop offset="60%" stop-color="{colors[2]}" stop-opacity="0.7"/>
      <stop offset="100%" stop-color="{colors[3]}" stop-opacity="0.5"/>
    </radialGradient>''')

    # 10 different clip paths
    clip_paths = []

    # Circular clip paths of various sizes
    for i in range(3):
        r = 200 + i * 100
        svg_parts.append(f'''    <clipPath id="clipCircle{i}">
      <circle cx="0" cy="0" r="{r}"/>
    </clipPath>''')
        clip_paths.append(f"clipCircle{i}")

    # Star clip path (large)
    star_points = []
    for j in range(10):
        angle = j * math.pi / 5 - math.pi / 2
        r = 400 if j % 2 == 0 else 200
        px = r * math.cos(angle)
        py = r * math.sin(angle)
        star_points.append(f"{px:.1f},{py:.1f}")
    star_path = "M" + " L".join(star_points) + "Z"
    svg_parts.append(f'''    <clipPath id="clipStar">
      <path d="{star_path}"/>
    </clipPath>''')
    clip_paths.append("clipStar")

    # Hexagon clip path (large)
    hex_points = []
    for j in range(6):
        angle = j * math.pi / 3 + math.pi / 6
        px = 350 * math.cos(angle)
        py = 350 * math.sin(angle)
        hex_points.append(f"{px:.1f},{py:.1f}")
    hex_path = "M" + " L".join(hex_points) + "Z"
    svg_parts.append(f'''    <clipPath id="clipHex">
      <path d="{hex_path}"/>
    </clipPath>''')
    clip_paths.append("clipHex")

    # Rectangle clip paths
    for i in range(3):
        w = 600 + i * 200
        h = 400 + i * 150
        svg_parts.append(f'''    <clipPath id="clipRect{i}">
      <rect x="{-w/2:.1f}" y="{-h/2:.1f}" width="{w}" height="{h}" rx="{20+i*10}"/>
    </clipPath>''')
        clip_paths.append(f"clipRect{i}")

    # Dynamic clip path that varies per frame
    dynamic_r = 250 + 150 * math.sin(t * math.pi * 2)
    svg_parts.append(f'''    <clipPath id="clipDynamic">
      <circle cx="0" cy="0" r="{dynamic_r:.1f}"/>
    </clipPath>''')
    clip_paths.append("clipDynamic")

    # Text clip path - giant letter
    char_idx = frame_num % len(TEXT_CHARS)
    giant_char = TEXT_CHARS[char_idx]
    svg_parts.append(f'''    <clipPath id="clipText">
      <text x="0" y="350" font-family="Arial Black, Impact, sans-serif" font-size="1000" font-weight="bold" text-anchor="middle">{giant_char}</text>
    </clipPath>''')
    clip_paths.append("clipText")

    # 10 different masks
    masks = []

    # Gradient masks (soft edges)
    for i in range(3):
        svg_parts.append(f'''    <mask id="maskGrad{i}" maskContentUnits="objectBoundingBox">
      <rect x="0" y="0" width="1" height="1" fill="url(#maskGradFill{i})"/>
    </mask>
    <linearGradient id="maskGradFill{i}" x1="{i*30}%" y1="0%" x2="{100-i*30}%" y2="100%">
      <stop offset="0%" stop-color="white"/>
      <stop offset="50%" stop-color="gray"/>
      <stop offset="100%" stop-color="black"/>
    </linearGradient>''')
        masks.append(f"maskGrad{i}")

    # Circular fade masks
    for i in range(2):
        outer_r = 400 + i * 100
        inner_r = 100 + i * 50
        svg_parts.append(f'''    <mask id="maskCircle{i}">
      <circle cx="0" cy="0" r="{outer_r}" fill="white"/>
      <circle cx="0" cy="0" r="{inner_r}" fill="black"/>
    </mask>''')
        masks.append(f"maskCircle{i}")

    # Striped masks
    for i in range(2):
        stripe_h = 20 + i * 10
        stripes = ""
        for s in range(-20, 21):
            stripes += f'<rect x="-600" y="{s * stripe_h * 2}" width="1200" height="{stripe_h}" fill="black"/>\n      '
        svg_parts.append(f'''    <mask id="maskStripes{i}">
      <rect x="-600" y="-600" width="1200" height="1200" fill="white"/>
      {stripes}</mask>''')
        masks.append(f"maskStripes{i}")

    # Dynamic mask
    mask_opacity = 0.3 + 0.7 * abs(math.sin(t * math.pi))
    svg_parts.append(f'''    <mask id="maskDynamic">
      <rect x="-600" y="-600" width="1200" height="1200" fill="white" fill-opacity="{mask_opacity:.2f}"/>
      <circle cx="0" cy="0" r="200" fill="white"/>
    </mask>''')
    masks.append("maskDynamic")

    # Text mask - giant letter reveals content
    svg_parts.append(f'''    <mask id="maskText">
      <rect x="-1000" y="-600" width="2000" height="1200" fill="black"/>
      <text x="0" y="350" font-family="Arial Black, Impact, sans-serif" font-size="1000" font-weight="bold" text-anchor="middle" fill="white">{giant_char}</text>
    </mask>''')
    masks.append("maskText")

    # Checkerboard mask
    checks = ""
    for row in range(-10, 11):
        for col in range(-10, 11):
            if (row + col) % 2 == 0:
                checks += f'<rect x="{col * 50}" y="{row * 50}" width="50" height="50" fill="white"/>\n      '
    svg_parts.append(f'''    <mask id="maskChecker">
      <rect x="-600" y="-600" width="1200" height="1200" fill="black"/>
      {checks}</mask>''')
    masks.append("maskChecker")

    svg_parts.append('  </defs>')

    # Background with gradient
    svg_parts.append(f'  <rect width="1920" height="1080" fill="url(#lgrad{frame_num % 20})"/>')

    # ===== LAYER 1: Giant text characters as background (10 chars) =====
    for i in range(10):
        char = TEXT_CHARS[(frame_num + i) % len(TEXT_CHARS)]
        x = random.randint(-200, 1920)
        y = random.randint(200, 1200)
        rot = (frame_num * 2 + i * 36 + math.sin(t * math.pi * 2 + i) * 20) % 360
        opacity = 0.1 + random.random() * 0.3
        grad_idx = (frame_num + i) % 20
        blur_idx = i % 10
        clip_idx = i % len(clip_paths)

        svg_parts.append(f'''  <g transform="translate({x}, {y}) rotate({rot:.1f})" clip-path="url(#{clip_paths[clip_idx]})">
    <text x="0" y="0" font-family="Arial Black, Impact, sans-serif" font-size="800" font-weight="bold"
          text-anchor="middle" fill="url(#lgrad{grad_idx})" filter="url(#blur{blur_idx})" opacity="{opacity:.2f}">{char}</text>
  </g>''')

    # ===== LAYER 2: Medium shapes with clips and masks (200 elements) =====
    for i in range(200):
        x = (i % 20) * 100 + 50 + math.sin(t * math.pi * 2 + i * 0.1) * 20
        y = (i // 20) * 100 + 50 + math.cos(t * math.pi * 2 + i * 0.15) * 15

        grad_idx = i % 20
        blur_idx = i % 15
        opacity = 0.3 + 0.5 * math.sin(t * math.pi + i * 0.05)

        shape_type = i % 6
        clip_id = clip_paths[i % len(clip_paths)]
        mask_id = masks[i % len(masks)]

        # Alternate between clip and mask
        extra_attr = f'clip-path="url(#{clip_id})"' if i % 2 == 0 else f'mask="url(#{mask_id})"'

        # 7-level nesting
        transforms = []
        for level in range(7):
            tx = math.sin(t * math.pi * 2 + level * 0.3 + i * 0.02) * 3
            ty = math.cos(t * math.pi * 2 + level * 0.4 + i * 0.03) * 2
            rot = math.sin(t * math.pi + level * 0.1 + i * 0.01) * 10
            scale = 1.0 - level * 0.01
            transforms.append(f'translate({tx:.2f},{ty:.2f}) rotate({rot:.2f}) scale({scale:.3f})')

        if shape_type == 0:
            # Star
            size = 30 + math.sin(t * math.pi + i) * 10
            points = []
            for j in range(10):
                angle = j * math.pi / 5 - math.pi / 2
                r = size if j % 2 == 0 else size * 0.5
                px = r * math.cos(angle)
                py = r * math.sin(angle)
                points.append(f"{px:.1f},{py:.1f}")
            path_d = "M" + " L".join(points) + "Z"
            inner = f'<path d="{path_d}" fill="url(#rgrad{grad_idx})" filter="url(#blur{blur_idx})" opacity="{opacity:.2f}"/>'
        elif shape_type == 1:
            # Circle
            r = 25 + math.sin(t * math.pi + i) * 8
            inner = f'<circle cx="0" cy="0" r="{r:.1f}" fill="url(#lgrad{grad_idx})" filter="url(#shadow{i%5})" opacity="{opacity:.2f}"/>'
        elif shape_type == 2:
            # Rectangle
            w = 40 + math.sin(t * math.pi + i) * 10
            h = 30 + math.cos(t * math.pi + i) * 8
            inner = f'<rect x="{-w/2:.1f}" y="{-h/2:.1f}" width="{w:.1f}" height="{h:.1f}" rx="5" fill="url(#rgrad{grad_idx})" filter="url(#blur{blur_idx})" opacity="{opacity:.2f}"/>'
        elif shape_type == 3:
            # Hexagon
            size = 25 + math.sin(t * math.pi + i) * 8
            points = []
            for j in range(6):
                angle = j * math.pi / 3 + math.pi / 6
                px = size * math.cos(angle)
                py = size * math.sin(angle)
                points.append(f"{px:.1f},{py:.1f}")
            path_d = "M" + " L".join(points) + "Z"
            inner = f'<path d="{path_d}" fill="url(#lgrad{grad_idx})" filter="url(#shadow{i%5})" opacity="{opacity:.2f}"/>'
        elif shape_type == 4:
            # Ellipse
            rx = 30 + math.sin(t * math.pi + i) * 10
            ry = 20 + math.cos(t * math.pi + i) * 7
            inner = f'<ellipse cx="0" cy="0" rx="{rx:.1f}" ry="{ry:.1f}" fill="url(#rgrad{grad_idx})" filter="url(#blur{blur_idx})" opacity="{opacity:.2f}"/>'
        else:
            # Text number
            inner = f'<text x="0" y="8" font-family="Arial, sans-serif" font-size="20" font-weight="bold" text-anchor="middle" fill="url(#lgrad{grad_idx})" opacity="{opacity:.2f}">{i}</text>'

        # Build 7-level nested structure
        result = inner
        for level in range(6, -1, -1):
            clip_on_level = f' {extra_attr}' if level == 3 else ''
            result = f'<g transform="{transforms[level]}"{clip_on_level}>{result}</g>'

        svg_parts.append(f'  <g transform="translate({x:.1f}, {y:.1f})">{result}</g>')

    # ===== LAYER 3: Small scattered elements (500 elements) =====
    for i in range(500):
        x = random.randint(0, 1920)
        y = random.randint(0, 1080)

        grad_idx = i % 20
        opacity = 0.2 + random.random() * 0.4
        size = 5 + random.random() * 15
        rot = random.randint(0, 360)

        shape = i % 4
        if shape == 0:
            elem = f'<circle cx="{x}" cy="{y}" r="{size:.1f}" fill="url(#rgrad{grad_idx})" opacity="{opacity:.2f}"/>'
        elif shape == 1:
            elem = f'<rect x="{x-size/2:.1f}" y="{y-size/2:.1f}" width="{size:.1f}" height="{size:.1f}" transform="rotate({rot} {x} {y})" fill="url(#lgrad{grad_idx})" opacity="{opacity:.2f}"/>'
        elif shape == 2:
            # Small star
            points = []
            for j in range(10):
                angle = j * math.pi / 5 - math.pi / 2 + math.radians(rot)
                r = size if j % 2 == 0 else size * 0.5
                px = x + r * math.cos(angle)
                py = y + r * math.sin(angle)
                points.append(f"{px:.1f},{py:.1f}")
            path_d = "M" + " L".join(points) + "Z"
            elem = f'<path d="{path_d}" fill="url(#rgrad{grad_idx})" opacity="{opacity:.2f}"/>'
        else:
            # Small text
            char = TEXT_CHARS[random.randint(0, len(TEXT_CHARS)-1)]
            elem = f'<text x="{x}" y="{y}" font-family="Arial, sans-serif" font-size="{size*2:.0f}" transform="rotate({rot} {x} {y})" fill="url(#lgrad{grad_idx})" opacity="{opacity:.2f}">{char}</text>'

        svg_parts.append(f'  {elem}')

    # ===== LAYER 4: Overlay text with masks (50 large texts) =====
    for i in range(50):
        char = TEXT_CHARS[(frame_num * 3 + i) % len(TEXT_CHARS)]
        x = random.randint(100, 1820)
        y = random.randint(100, 1000)
        size = 100 + random.randint(0, 200)
        rot = (frame_num + i * 7 + math.sin(t * math.pi * 2 + i * 0.5) * 30) % 360
        opacity = 0.15 + random.random() * 0.25
        grad_idx = (frame_num + i * 2) % 20
        mask_id = masks[i % len(masks)]
        blur_idx = i % 8

        svg_parts.append(f'''  <g transform="translate({x}, {y}) rotate({rot:.1f})" mask="url(#{mask_id})">
    <text x="0" y="{size/3:.0f}" font-family="Arial Black, Impact, sans-serif" font-size="{size}" font-weight="bold"
          text-anchor="middle" fill="url(#lgrad{grad_idx})" filter="url(#blur{blur_idx})" opacity="{opacity:.2f}">{char}</text>
  </g>''')

    # ===== LAYER 5: Blurred overlay shapes with clips (200 elements) =====
    for i in range(200):
        x = random.randint(-100, 2020)
        y = random.randint(-100, 1180)

        grad_idx = i % 20
        blur_idx = 10 + i % 10
        opacity = 0.1 + random.random() * 0.2
        clip_id = clip_paths[i % len(clip_paths)]

        size = 50 + random.randint(0, 100)

        svg_parts.append(f'''  <g transform="translate({x}, {y})" clip-path="url(#{clip_id})">
    <ellipse cx="0" cy="0" rx="{size}" ry="{size*0.7:.0f}" fill="url(#rgrad{grad_idx})" filter="url(#blur{blur_idx})" opacity="{opacity:.2f}"/>
  </g>''')

    # ===== LAYER 6: Final text overlays clipped by giant letter (40 texts) =====
    for i in range(40):
        char = TEXT_CHARS[(frame_num * 5 + i * 3) % len(TEXT_CHARS)]
        x = 960 + math.sin(t * math.pi * 2 + i * 0.3) * 400
        y = 540 + math.cos(t * math.pi * 2 + i * 0.4) * 300
        size = 60 + random.randint(0, 80)
        rot = i * 9 + frame_num * 0.5
        opacity = 0.3 + random.random() * 0.3
        grad_idx = i % 20

        svg_parts.append(f'''  <g transform="translate({x:.0f}, {y:.0f}) rotate({rot:.1f})" clip-path="url(#clipText)">
    <text x="0" y="{size/3:.0f}" font-family="Georgia, serif" font-size="{size}" font-style="italic"
          text-anchor="middle" fill="url(#rgrad{grad_idx})" filter="url(#shadow{i%5})" opacity="{opacity:.2f}">{char}</text>
  </g>''')

    # ===== LAYER 7: Large cubic bezier paths (500+ control points each, covering half screen) =====
    # Create 4 complex bezier figures, each covering approximately half the screen
    for fig_idx in range(4):
        # Position: tile the screen in quadrants, but make each path large enough to overlap
        base_x = 480 if fig_idx % 2 == 0 else 1440
        base_y = 270 if fig_idx < 2 else 810

        # Path dimensions: ~960x540 (half screen)
        path_width = 1000
        path_height = 600

        # Generate a complex cubic bezier path with 170+ segments (510+ control points)
        num_segments = 170 + (frame_num % 30)  # 170-200 segments = 510-600 control points

        # Start point
        path_points = []
        start_x = base_x - path_width/2 + random.randint(0, 50)
        start_y = base_y - path_height/2 + random.randint(0, 50)
        path_points.append(f"M{start_x:.1f},{start_y:.1f}")

        # Current position tracker
        curr_x, curr_y = start_x, start_y

        # Generate cubic bezier segments that create an organic flowing shape
        for seg in range(num_segments):
            # Progress through the path
            seg_t = seg / num_segments

            # Create flowing, organic movement
            # Amplitude varies with position and frame
            amp_x = path_width * 0.15 * (1 + 0.3 * math.sin(t * math.pi * 2 + seg * 0.1 + fig_idx))
            amp_y = path_height * 0.15 * (1 + 0.3 * math.cos(t * math.pi * 2 + seg * 0.15 + fig_idx))

            # Control point 1
            cp1_x = curr_x + amp_x * math.sin(seg_t * math.pi * 4 + t * math.pi + fig_idx * 1.5)
            cp1_y = curr_y + amp_y * math.cos(seg_t * math.pi * 3 + t * math.pi + fig_idx * 1.2)

            # Control point 2
            cp2_x = curr_x + path_width/num_segments + amp_x * math.sin(seg_t * math.pi * 5 + t * math.pi * 1.5)
            cp2_y = curr_y + amp_y * math.cos(seg_t * math.pi * 4 + t * math.pi * 1.3) * (-1 if seg % 2 == 0 else 1)

            # End point - progress across the path with vertical oscillation
            # Create a figure-8 / infinity loop pattern that covers the area
            progress_angle = seg_t * math.pi * 4  # Two full loops
            end_x = base_x + (path_width * 0.4) * math.sin(progress_angle) * math.cos(progress_angle * 0.5)
            end_y = base_y + (path_height * 0.4) * math.sin(progress_angle * 0.5)

            # Add some frame-based variation
            end_x += 20 * math.sin(t * math.pi * 2 + seg * 0.05)
            end_y += 15 * math.cos(t * math.pi * 2 + seg * 0.07)

            path_points.append(f"C{cp1_x:.1f},{cp1_y:.1f} {cp2_x:.1f},{cp2_y:.1f} {end_x:.1f},{end_y:.1f}")
            curr_x, curr_y = end_x, end_y

        # Close the path
        path_points.append("Z")
        path_d = " ".join(path_points)

        # Style the path
        grad_idx = (frame_num + fig_idx * 5) % 20
        blur_idx = fig_idx % 10
        opacity = 0.25 + 0.15 * math.sin(t * math.pi + fig_idx * 0.5)
        mask_id = masks[fig_idx % len(masks)]
        rot = (frame_num * 0.5 + fig_idx * 15) % 360

        svg_parts.append(f'''  <g transform="translate({base_x:.0f},{base_y:.0f}) rotate({rot:.1f})" mask="url(#{mask_id})">
    <path d="{path_d}" fill="url(#rgrad{grad_idx})" stroke="url(#lgrad{(grad_idx+5)%20})" stroke-width="2"
          filter="url(#blur{blur_idx})" opacity="{opacity:.2f}"/>
  </g>''')

    # ===== LAYER 8: Additional huge bezier shapes with sharp edges (2 figures, 250+ vertices each) =====
    for fig_idx in range(2):
        # These figures cover the full height and half the width
        base_x = 480 if fig_idx == 0 else 1440
        base_y = 540

        # Generate a jagged, spiky bezier path with 250+ segments
        num_segments = 250 + (frame_num % 50)  # 250-300 segments = 750-900 control points

        path_points = []

        # Start at top of figure
        start_x = base_x
        start_y = 50
        path_points.append(f"M{start_x:.1f},{start_y:.1f}")

        curr_x, curr_y = start_x, start_y

        for seg in range(num_segments):
            seg_t = seg / num_segments

            # Create spiky, aggressive shapes
            spike_amp = 100 + 50 * math.sin(t * math.pi * 3 + seg * 0.2)

            # Spiral/expanding pattern with spikes
            angle = seg_t * math.pi * 6 + t * math.pi  # 3 full rotations
            radius = 100 + 300 * seg_t + 30 * math.sin(seg * 0.5)

            # Control points create sharp spikes
            cp1_x = curr_x + spike_amp * math.sin(angle + 0.5) * (1 if seg % 2 == 0 else -1)
            cp1_y = curr_y + spike_amp * math.cos(angle + 0.5)

            cp2_x = base_x + radius * math.cos(angle) + spike_amp * 0.3 * math.sin(angle * 2)
            cp2_y = base_y + radius * math.sin(angle) * 0.5 + spike_amp * 0.3 * math.cos(angle * 2)

            end_x = base_x + radius * math.cos(angle)
            end_y = base_y + radius * math.sin(angle) * 0.5  # Compressed vertically

            # Frame-based variation
            end_x += 10 * math.sin(t * math.pi * 2 + seg * 0.03)
            end_y += 8 * math.cos(t * math.pi * 2 + seg * 0.04)

            path_points.append(f"C{cp1_x:.1f},{cp1_y:.1f} {cp2_x:.1f},{cp2_y:.1f} {end_x:.1f},{end_y:.1f}")
            curr_x, curr_y = end_x, end_y

        path_points.append("Z")
        path_d = " ".join(path_points)

        grad_idx = (frame_num + fig_idx * 7 + 10) % 20
        blur_idx = 3 + fig_idx * 2
        opacity = 0.2 + 0.1 * math.sin(t * math.pi + fig_idx)
        clip_id = clip_paths[fig_idx % len(clip_paths)]

        svg_parts.append(f'''  <g clip-path="url(#{clip_id})">
    <path d="{path_d}" fill="url(#lgrad{grad_idx})" stroke="url(#rgrad{(grad_idx+3)%20})" stroke-width="3"
          filter="url(#blur{blur_idx})" opacity="{opacity:.2f}" stroke-linejoin="round"/>
  </g>''')

    svg_parts.append('</svg>')
    return '\n'.join(svg_parts)


def main():
    if len(sys.argv) < 2:
        print("Usage: python generate_extreme_frames.py <output_folder> [num_frames] [num_elements]")
        print("  num_frames: Number of frames to generate (default: 1000)")
        print("  num_elements: Minimum number of elements per frame (default: 1000)")
        print("")
        print("Each frame will contain:")
        print("  - 10 giant background text characters")
        print("  - 200 medium shapes with 7-level nesting and clips/masks")
        print("  - 500 small scattered elements")
        print("  - 50 large overlay texts with masks")
        print("  - 200 blurred overlay shapes with clips")
        print("  - 40 final text overlays clipped by giant letter")
        print("  - 4 large cubic bezier paths (170+ segments = 510+ control points each)")
        print("  - 2 huge bezier shapes (250+ segments = 750+ control points each)")
        print("  = 1006+ total elements with 3000+ bezier control points")
        sys.exit(1)

    output_folder = sys.argv[1]
    num_frames = int(sys.argv[2]) if len(sys.argv) > 2 else 1000
    num_elements = int(sys.argv[3]) if len(sys.argv) > 3 else 1000

    os.makedirs(output_folder, exist_ok=True)

    print(f"Generating {num_frames} EXTREME complexity frames...")
    print("Features:")
    print("  - 1006+ elements per frame")
    print("  - Giant text characters sized as whole image")
    print("  - Rotated and clipped text")
    print("  - 20 blur filters, 40 gradients (linear + radial)")
    print("  - 10 clip paths including text-shaped clips")
    print("  - 10 masks including text-shaped masks")
    print("  - 7-level nested transforms")
    print("  - Multiple overlay layers")
    print("  - 6 large cubic bezier paths (500+ control points each)")
    print("  - 3000+ total bezier control points covering half screen")
    print("")

    for i in range(1, num_frames + 1):
        svg_content = generate_svg_frame(i, num_frames, num_elements)
        filename = f"frame_{i:05d}.svg"
        filepath = os.path.join(output_folder, filename)
        with open(filepath, 'w') as f:
            f.write(svg_content)

        if i % 50 == 0:
            print(f"  Generated {i}/{num_frames} frames")

    # Print summary
    total_size = sum(os.path.getsize(os.path.join(output_folder, f))
                     for f in os.listdir(output_folder) if f.endswith('.svg'))
    print(f"\nDone! {num_frames} frames saved to: {output_folder}")
    print(f"Total size: {total_size / (1024*1024):.2f} MB")
    print(f"Average file size: {total_size / num_frames / 1024:.1f} KB")


if __name__ == "__main__":
    main()
