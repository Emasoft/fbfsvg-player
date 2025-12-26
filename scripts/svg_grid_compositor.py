#!/usr/bin/env python3
"""
SVG Grid Compositor - Create grid layouts of FBF.SVG animations.

Features:
- Auto-calculate cell sizes from rows/columns specification
- Manual cell size mode for fine control
- Optional background animation (cells overlay the background)
- No-background mode with solid color container
- Text labels below each cell
- Aspect ratio preservation when scaling SVGs to cells
- Nested composite support (composites can contain other composites)
- HiDPI/Retina scaling support (--scale 2 for 2x displays)

Usage:
    # Auto-calculate mode (3x3 grid)
    uv run scripts/svg_grid_compositor.py --rows 3 --columns 3 --output grid.svg file1.svg file2.svg ...

    # With background animation
    uv run scripts/svg_grid_compositor.py --background bg.svg --rows 3 --columns 3 --output out.svg *.svg

    # No-background with black background
    uv run scripts/svg_grid_compositor.py --rows 4 --columns 3 --bg-color "#000000" --output out.svg *.svg

    # HiDPI/Retina display (2x scaling - creates 3840x2160 from 1920x1080)
    uv run scripts/svg_grid_compositor.py --rows 3 --columns 2 --scale 2 --output grid_hidpi.svg *.svg
"""

import argparse
import os
import re
import sys
from pathlib import Path
from typing import Optional

# Import the ID prefixing function from svg_id_prefixer
from svg_id_prefixer import prefix_svg_ids


def parse_viewbox(svg_content: str) -> tuple[float, float, float, float]:
    """Extract viewBox dimensions from SVG content.

    Returns:
        (x, y, width, height) tuple from viewBox, or (0, 0, 100, 100) if not found.
    """
    match = re.search(r'viewBox=["\']\s*([^"\']+)\s*["\']', svg_content, re.IGNORECASE)
    if match:
        parts = match.group(1).split()
        if len(parts) == 4:
            return tuple(float(p) for p in parts)
    # Fallback: try width/height attributes
    width_match = re.search(r'\bwidth=["\']\s*(\d+(?:\.\d+)?)', svg_content)
    height_match = re.search(r'\bheight=["\']\s*(\d+(?:\.\d+)?)', svg_content)
    w = float(width_match.group(1)) if width_match else 100
    h = float(height_match.group(1)) if height_match else 100
    return (0, 0, w, h)


def extract_svg_inner(svg_content: str) -> str:
    """Extract content between <svg> and </svg> tags."""
    match = re.search(r"<svg[^>]*>", svg_content, re.IGNORECASE)
    if not match:
        return svg_content
    start = match.end()
    end = svg_content.rfind("</svg>")
    if end == -1:
        return svg_content[start:]
    return svg_content[start:end]


def create_grid_composite(
    tile_paths: list[str],
    # Grid dimensions
    rows: Optional[int] = None,
    columns: int = 3,
    cell_width: Optional[float] = None,
    cell_height: Optional[float] = None,
    # Container options
    background_svg_path: Optional[str] = None,
    container_width: int = 1920,
    container_height: int = 1080,
    container_bg_color: str = "#ffffff",
    # Margins
    margin: float = 20,
    # Labels
    show_labels: bool = False,
    label_height: float = 24,
    font_size: float = 14,
    # Scaling
    preserve_aspect_ratio: bool = True,
) -> str:
    """
    Create a grid composite of multiple SVG animations.

    TWO MODES:
    - Auto-calculate: When `rows` is set, cell dimensions are computed from container size
    - Manual: When `cell_width` and `cell_height` are set, those dimensions are used

    Args:
        tile_paths: List of paths to SVG files to tile
        rows: Number of rows in grid (enables auto-calculate mode)
        columns: Number of columns in grid
        cell_width: Manual cell width (ignored in auto mode)
        cell_height: Manual cell height (ignored in auto mode)
        background_svg_path: Optional background SVG animation
        container_width: Width when no background (default 1920)
        container_height: Height when no background (default 1080)
        container_bg_color: Background color when no background SVG
        margin: Margin between cells and edges
        show_labels: Show filename labels below each cell
        label_height: Height reserved for labels
        font_size: Font size for labels
        preserve_aspect_ratio: Keep SVG aspect ratios when scaling

    Returns:
        Combined SVG content as string
    """
    if not tile_paths:
        raise ValueError("No tile SVG files provided")

    # Load and prefix all tile SVGs
    tiles = []
    for i, path in enumerate(tile_paths):
        with open(path, "r", encoding="utf-8") as f:
            content = f.read()
        # Prefix with t0_, t1_, t2_, etc.
        prefix = f"t{i}_"
        result = prefix_svg_ids(content, prefix=prefix, verify=True)
        if result["errors"]:
            print(f"Warning: Prefixing errors in {path}: {result['errors'][:3]}", file=sys.stderr)
        vb = parse_viewbox(content)
        tiles.append(
            {
                "path": path,
                "name": Path(path).stem,
                "content": result["content"],
                "prefix": prefix,
                "viewbox": vb,
                "width": vb[2],
                "height": vb[3],
            }
        )

    # Determine container dimensions
    if background_svg_path:
        with open(background_svg_path, "r", encoding="utf-8") as f:
            bg_content = f.read()
        # Prefix background IDs
        bg_result = prefix_svg_ids(bg_content, prefix="bg_", verify=True)
        bg_content = bg_result["content"]
        bg_vb = parse_viewbox(bg_content)
        cont_width, cont_height = bg_vb[2], bg_vb[3]
        has_background = True
    else:
        cont_width, cont_height = container_width, container_height
        bg_content = None
        has_background = False

    # Calculate grid layout
    num_tiles = len(tiles)

    # Auto-calculate mode: compute rows if not specified
    if rows is None:
        rows = (num_tiles + columns - 1) // columns

    # Calculate cell dimensions
    total_label_space = label_height * rows if show_labels else 0
    available_width = cont_width - margin * (columns + 1)
    available_height = cont_height - margin * (rows + 1) - total_label_space

    if cell_width is None or rows is not None:
        # Auto mode: calculate from available space
        calc_cell_width = available_width / columns
        calc_cell_height = available_height / rows
    else:
        # Manual mode: use provided dimensions
        calc_cell_width = cell_width
        calc_cell_height = cell_height if cell_height else cell_width * 0.75

    # Generate positioned tiles
    tile_elements = []
    label_elements = []
    clip_defs = []  # clipPath definitions for Skia compatibility

    for i, tile in enumerate(tiles):
        col = i % columns
        row = i // columns

        # Skip if beyond grid
        if row >= rows:
            print(f"Warning: Tile {tile['name']} exceeds grid capacity, skipping", file=sys.stderr)
            continue

        # Calculate cell position
        cell_x = margin + col * (calc_cell_width + margin)
        cell_y = margin + row * (calc_cell_height + margin + (label_height if show_labels else 0))

        # Scale tile to fit cell while preserving aspect ratio
        tile_w, tile_h = tile["width"], tile["height"]
        if preserve_aspect_ratio:
            scale = min(calc_cell_width / tile_w, calc_cell_height / tile_h)
            scaled_w = tile_w * scale
            scaled_h = tile_h * scale
            # Center in cell
            offset_x = (calc_cell_width - scaled_w) / 2
            offset_y = (calc_cell_height - scaled_h) / 2
        else:
            scale = 1
            scaled_w, scaled_h = calc_cell_width, calc_cell_height
            offset_x, offset_y = 0, 0

        # Extract inner content and wrap in positioned group
        inner = extract_svg_inner(tile["content"])

        # Create clipPath for this tile (Skia doesn't respect overflow="hidden" on nested SVGs)
        clip_id = f"grid_clip_{i}"
        tile_vb = tile["viewbox"]
        clip_def = f'''    <clipPath id="{clip_id}">
      <rect x="{tile_vb[0]}" y="{tile_vb[1]}" width="{tile_vb[2]}" height="{tile_vb[3]}"/>
    </clipPath>'''
        clip_defs.append(clip_def)

        # Create nested SVG element with explicit clipPath for Skia compatibility
        tile_element = f'''  <!-- Tile: {tile["name"]} -->
  <svg x="{cell_x + offset_x:.2f}" y="{cell_y + offset_y:.2f}"
       width="{scaled_w:.2f}" height="{scaled_h:.2f}"
       viewBox="{tile_vb[0]} {tile_vb[1]} {tile_vb[2]} {tile_vb[3]}"
       overflow="hidden">
    <defs>
{clip_def}
    </defs>
    <g clip-path="url(#{clip_id})">
{inner}
    </g>
  </svg>'''
        tile_elements.append(tile_element)

        # Add label if enabled
        if show_labels:
            label_x = cell_x + calc_cell_width / 2
            label_y = cell_y + calc_cell_height + label_height * 0.7
            label_element = f'''  <text x="{label_x:.2f}" y="{label_y:.2f}"
        text-anchor="middle" font-family="sans-serif"
        font-size="{font_size}" fill="#333333">{tile["name"]}</text>'''
            label_elements.append(label_element)

    # Assemble final SVG
    if has_background:
        # Insert tiles into background SVG
        # Find STAGE_BACKGROUND or insert after opening svg tag
        stage_bg_match = re.search(
            r'(<g[^>]*id=["\']STAGE_BACKGROUND["\'][^>]*>)', bg_content, re.IGNORECASE
        )
        if stage_bg_match:
            # Insert after STAGE_BACKGROUND opening tag
            insert_pos = stage_bg_match.end()
            tiles_content = "\n" + "\n".join(tile_elements) + "\n" + "\n".join(label_elements)
            combined = bg_content[:insert_pos] + tiles_content + bg_content[insert_pos:]
        else:
            # Insert before closing </svg>
            close_pos = bg_content.rfind("</svg>")
            tiles_content = (
                "\n<!-- Grid Tiles -->\n"
                + "\n".join(tile_elements)
                + "\n"
                + "\n".join(label_elements)
                + "\n"
            )
            combined = bg_content[:close_pos] + tiles_content + bg_content[close_pos:]
    else:
        # Create new container SVG
        tiles_content = "\n".join(tile_elements)
        labels_content = "\n".join(label_elements) if label_elements else ""

        combined = f'''<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg"
     xmlns:xlink="http://www.w3.org/1999/xlink"
     width="{int(cont_width)}" height="{int(cont_height)}"
     viewBox="0 0 {cont_width} {cont_height}">
  <!-- Background -->
  <rect x="0" y="0" width="{cont_width}" height="{cont_height}" fill="{container_bg_color}"/>

  <!-- Grid Tiles -->
{tiles_content}

  <!-- Labels -->
{labels_content}
</svg>'''

    return combined


def main():
    parser = argparse.ArgumentParser(
        description="Create grid layouts of FBF.SVG animations",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # 3x3 grid with auto-calculated cell sizes
  %(prog)s --rows 3 --columns 3 --output grid.svg *.fbf.svg

  # With background animation
  %(prog)s --background bg.svg --rows 2 --columns 3 --labels --output out.svg *.svg

  # No-background mode with black background
  %(prog)s --rows 4 --columns 3 --bg-color "#000000" --labels --output out.svg *.svg

  # Manual cell dimensions
  %(prog)s --columns 4 --cell-width 300 --cell-height 200 --output grid.svg *.svg
""",
    )

    # Required
    parser.add_argument("tiles", nargs="+", help="SVG files to arrange in grid")
    parser.add_argument("-o", "--output", required=True, help="Output SVG file path")

    # Grid dimensions
    parser.add_argument("--rows", type=int, help="Number of rows (enables auto-calculate mode)")
    parser.add_argument(
        "--columns", "-c", type=int, default=3, help="Number of columns (default: 3)"
    )
    parser.add_argument("--cell-width", type=float, help="Manual cell width")
    parser.add_argument("--cell-height", type=float, help="Manual cell height")

    # Container options
    parser.add_argument("--background", "-b", help="Background SVG animation")
    parser.add_argument(
        "--container-width",
        type=int,
        default=1920,
        help="Container width for no-background mode (default: 1920)",
    )
    parser.add_argument(
        "--container-height",
        type=int,
        default=1080,
        help="Container height for no-background mode (default: 1080)",
    )
    parser.add_argument(
        "--bg-color",
        default="#ffffff",
        help="Background color for no-background mode (default: #ffffff)",
    )

    # Margins
    parser.add_argument(
        "--margin", "-m", type=float, default=20, help="Margin between cells (default: 20)"
    )

    # Labels
    parser.add_argument(
        "--labels", "-l", action="store_true", help="Show filename labels below cells"
    )
    parser.add_argument("--label-height", type=float, default=24, help="Label height (default: 24)")
    parser.add_argument("--font-size", type=float, default=14, help="Label font size (default: 14)")

    # Scaling
    parser.add_argument(
        "--no-preserve-aspect", action="store_true", help="Don't preserve SVG aspect ratios"
    )
    parser.add_argument(
        "--scale",
        "-s",
        type=float,
        default=1.0,
        help="Scale factor for container dimensions (use 2 for HiDPI/Retina displays, default: 1)",
    )

    args = parser.parse_args()

    # Apply scale factor to container dimensions
    args.container_width = int(args.container_width * args.scale)
    args.container_height = int(args.container_height * args.scale)
    args.margin = args.margin * args.scale
    args.label_height = args.label_height * args.scale
    args.font_size = args.font_size * args.scale

    # Validate tile files exist
    for path in args.tiles:
        if not os.path.exists(path):
            print(f"Error: File not found: {path}", file=sys.stderr)
            sys.exit(1)

    # Validate background if specified
    if args.background and not os.path.exists(args.background):
        print(f"Error: Background file not found: {args.background}", file=sys.stderr)
        sys.exit(1)

    try:
        result = create_grid_composite(
            tile_paths=args.tiles,
            rows=args.rows,
            columns=args.columns,
            cell_width=args.cell_width,
            cell_height=args.cell_height,
            background_svg_path=args.background,
            container_width=args.container_width,
            container_height=args.container_height,
            container_bg_color=args.bg_color,
            margin=args.margin,
            show_labels=args.labels,
            label_height=args.label_height,
            font_size=args.font_size,
            preserve_aspect_ratio=not args.no_preserve_aspect,
        )

        with open(args.output, "w", encoding="utf-8") as f:
            f.write(result)

        print(f"Created grid composite: {args.output}")
        print(f"  Tiles: {len(args.tiles)}")
        print(f"  Grid: {args.rows or 'auto'}x{args.columns}")
        if args.background:
            print(f"  Background: {args.background}")
        else:
            print(f"  Container: {args.container_width}x{args.container_height}")
        if args.scale != 1.0:
            print(f"  Scale: {args.scale}x (HiDPI)")
        if args.labels:
            print("  Labels: enabled")

    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        import traceback

        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
