#!/usr/bin/env python3
"""
SVG ID Prefixer - Efficiently prefix all IDs and references in SVG files.

This module handles all SVG ID reference patterns:
- id="..." declarations
- xlink:href="#id" and href="#id"
- url(#id) in any attribute (fill, stroke, clip-path, mask, filter, marker-*, etc.)
- xlink:href="url(#id)" (non-standard but exists in the wild)
- Animation values="#id;#id;..." (semicolon-delimited frame references)
- Animation begin/end="id.event" (event-based timing)
- CSS url(#id) references in style attributes and <style> blocks
- JavaScript getElementById("id") and similar
- CDATA sections with ID references

OPTIMIZED: Uses dictionary-based replacement instead of regex alternation
for O(n) performance with large numbers of IDs.

Usage:
    from svg_id_prefixer import prefix_svg_ids

    # Prefix with auto-generated short prefix
    result = prefix_svg_ids(svg_content)

    # Prefix with custom prefix
    result = prefix_svg_ids(svg_content, prefix="emb_")

    # Get stats and verify
    result = prefix_svg_ids(svg_content, prefix="x_", verify=True)
    print(f"Prefixed {result['stats']['ids_found']} IDs")
"""

import re
import hashlib
import string
from typing import Optional


def generate_short_prefix(content: str, length: int = 2) -> str:
    """Generate a short unique prefix based on content hash.

    Uses base36 (0-9, a-z) for compact representation.
    Default 2 chars = 1296 unique prefixes.
    """
    hash_bytes = hashlib.md5(content.encode("utf-8")).digest()
    # Convert first bytes to base36
    num = int.from_bytes(hash_bytes[:4], "big")
    chars = string.digits + string.ascii_lowercase
    result = []
    for _ in range(length):
        result.append(chars[num % 36])
        num //= 36
    return "".join(result) + "_"


def prefix_svg_ids(
    svg_content: str, prefix: Optional[str] = None, verify: bool = True
) -> dict:
    """
    Prefix all IDs and their references in an SVG document.

    Uses dictionary-based replacement for O(n) performance per pattern.

    IMPORTANT: This function prefixes a SINGLE SVG. When combining multiple SVGs,
    you MUST prefix each one FIRST with unique prefixes, THEN combine them.
    Never combine first and prefix later - that causes ID collisions!

    Args:
        svg_content: The SVG content as a string
        prefix: Optional custom prefix. If None, generates a short hash-based prefix.
        verify: If True, verifies no unprefixed IDs remain after processing.

    Returns:
        dict with keys:
            - 'content': The prefixed SVG content
            - 'prefix': The prefix that was used
            - 'stats': Dict with 'ids_found', 'references_updated', 'errors'
            - 'errors': List of any verification errors (if verify=True)
    """
    if prefix is None:
        prefix = generate_short_prefix(svg_content)

    stats = {"ids_found": 0, "references_updated": 0, "errors": []}

    # Step 1: Collect all existing IDs
    id_pattern = re.compile(r'\bid=["\']([^"\']+)["\']')
    original_ids = set(id_pattern.findall(svg_content))
    stats["ids_found"] = len(original_ids)

    # Build a set of IDs that need prefixing (exclude already-prefixed ones)
    ids_to_prefix = {id_val for id_val in original_ids if not id_val.startswith(prefix)}

    if not ids_to_prefix:
        # Nothing to prefix
        return {"content": svg_content, "prefix": prefix, "stats": stats, "errors": []}

    # Create a mapping for fast lookup
    id_map = {id_val: f"{prefix}{id_val}" for id_val in ids_to_prefix}
    ref_count = 0

    result = svg_content

    # Pattern 1: id="..." declarations
    def replace_id_decl(m):
        nonlocal ref_count
        quote = m.group(1)
        id_val = m.group(2)
        if id_val in id_map:
            ref_count += 1
            return f"id={quote}{id_map[id_val]}{quote}"
        return m.group(0)

    result = re.sub(r'\bid=(["\'])([^"\']+)\1', replace_id_decl, result)

    # Pattern 2: xlink:href="#id" and href="#id"
    def replace_href(m):
        nonlocal ref_count
        attr = m.group(1)
        quote = m.group(2)
        id_val = m.group(3)
        if id_val in id_map:
            ref_count += 1
            return f"{attr}={quote}#{id_map[id_val]}{quote}"
        return m.group(0)

    result = re.sub(r'(xlink:href|href)=(["\'])#([^"\']+)\2', replace_href, result)

    # Pattern 3: xlink:href="url(#id)" (non-standard)
    def replace_href_url(m):
        nonlocal ref_count
        attr = m.group(1)
        quote = m.group(2)
        id_val = m.group(3)
        if id_val in id_map:
            ref_count += 1
            return f"{attr}={quote}url(#{id_map[id_val]}){quote}"
        return m.group(0)

    result = re.sub(
        r'(xlink:href|href)=(["\'])url\(#([^)]+)\)\2', replace_href_url, result
    )

    # Pattern 4: url(#id) in any attribute value
    def replace_url_ref(m):
        nonlocal ref_count
        id_val = m.group(1)
        if id_val in id_map:
            ref_count += 1
            return f"url(#{id_map[id_val]})"
        return m.group(0)

    result = re.sub(r"url\(#([^)]+)\)", replace_url_ref, result)

    # Pattern 5: Animation values with ID references
    # values="    #id1;    #id2;    #id3"
    def replace_values_refs(m):
        nonlocal ref_count
        quote = m.group(1)
        values_content = m.group(2)

        def replace_single_ref(m2):
            nonlocal ref_count
            id_val = m2.group(1)
            if id_val in id_map:
                ref_count += 1
                return f"#{id_map[id_val]}"
            return m2.group(0)

        # Match #id followed by ; or whitespace or end of values
        new_values = re.sub(r'#([^\s;"\'\)]+)', replace_single_ref, values_content)
        return f"values={quote}{new_values}{quote}"

    result = re.sub(r'values=(["\'])([^"\']*)\1', replace_values_refs, result)

    # Pattern 6: Animation begin/end timing with id.event syntax
    def replace_timing_ref(m):
        nonlocal ref_count
        attr = m.group(1)
        quote = m.group(2)
        timing_content = m.group(3)

        def replace_timing_id(m2):
            nonlocal ref_count
            id_val = m2.group(1)
            event = m2.group(2)
            if id_val in id_map:
                ref_count += 1
                return f"{id_map[id_val]}.{event}"
            return m2.group(0)

        # Match id.event pattern
        new_timing = re.sub(
            r"([a-zA-Z_][a-zA-Z0-9_-]*)\.(\w+)", replace_timing_id, timing_content
        )
        return f"{attr}={quote}{new_timing}{quote}"

    result = re.sub(r'(begin|end)=(["\'])([^"\']+)\2', replace_timing_ref, result)

    # Pattern 7: from/to/by attributes that reference IDs
    def replace_from_to(m):
        nonlocal ref_count
        attr = m.group(1)
        quote = m.group(2)
        id_val = m.group(3)
        if id_val in id_map:
            ref_count += 1
            return f"{attr}={quote}#{id_map[id_val]}{quote}"
        return m.group(0)

    result = re.sub(r'(from|to|by)=(["\'])#([^"\']+)\2', replace_from_to, result)

    # Pattern 8: JavaScript getElementById("id")
    def replace_js_getelementbyid(m):
        nonlocal ref_count
        quote = m.group(1)
        id_val = m.group(2)
        if id_val in id_map:
            ref_count += 1
            return f"getElementById({quote}{id_map[id_val]}{quote})"
        return m.group(0)

    result = re.sub(
        r'getElementById\((["\'])([^"\']+)\1\)', replace_js_getelementbyid, result
    )

    # Pattern 9: querySelector("#id")
    def replace_js_queryselector(m):
        nonlocal ref_count
        quote = m.group(1)
        id_val = m.group(2)
        if id_val in id_map:
            ref_count += 1
            return f"querySelector({quote}#{id_map[id_val]}{quote})"
        return m.group(0)

    result = re.sub(
        r'querySelector\((["\'])#([^"\']+)\1\)', replace_js_queryselector, result
    )

    # Pattern 10: data-* attributes with #id references
    def replace_data_attr(m):
        nonlocal ref_count
        attr = m.group(1)
        quote = m.group(2)
        id_val = m.group(3)
        if id_val in id_map:
            ref_count += 1
            return f"{attr}={quote}#{id_map[id_val]}{quote}"
        return m.group(0)

    result = re.sub(r'(data-[a-z-]+)=(["\'])#([^"\']+)\2', replace_data_attr, result)

    stats["references_updated"] = ref_count

    # Verification step
    errors = []
    if verify:
        # Check for any remaining unprefixed ID declarations
        remaining_ids = set(id_pattern.findall(result))
        for id_val in remaining_ids:
            if id_val in ids_to_prefix:
                errors.append(f"Unprefixed ID declaration remains: {id_val}")

        # Quick scan for obvious unprefixed references (sample for speed)
        sample_ids = list(ids_to_prefix)[:100]  # Check first 100 only
        for original_id in sample_ids:
            check_patterns = [
                f'href="#{original_id}"',
                f"href='#{original_id}'",
                f"url(#{original_id})",
            ]
            for pattern in check_patterns:
                if pattern in result:
                    errors.append(f"Unprefixed reference remains for ID: {original_id}")
                    break

        stats["errors"] = errors

    return {"content": result, "prefix": prefix, "stats": stats, "errors": errors}


def prefix_svg_file(
    input_path: str,
    output_path: Optional[str] = None,
    prefix: Optional[str] = None,
    verify: bool = True,
) -> dict:
    """
    Prefix all IDs in an SVG file.

    Args:
        input_path: Path to input SVG file
        output_path: Path to output file. If None, overwrites input file.
        prefix: Optional custom prefix
        verify: If True, verifies no unprefixed IDs remain

    Returns:
        Same dict as prefix_svg_ids, plus 'input_path' and 'output_path'
    """
    with open(input_path, "r", encoding="utf-8") as f:
        content = f.read()

    result = prefix_svg_ids(content, prefix=prefix, verify=verify)

    out_path = output_path or input_path
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(result["content"])

    result["input_path"] = input_path
    result["output_path"] = out_path

    return result


def combine_svgs_with_prefixes(
    svg_contents: list[tuple],
    container_viewbox: str = "0 0 1200 674",
    container_width: int = 1200,
    container_height: int = 674,
) -> dict:
    """
    Combine multiple SVGs into one, each with unique prefixes.

    Supports two tuple formats:
    - (svg_content, name) - SVG placed at origin (0, 0)
    - (svg_content, name, x, y) - SVG placed at specified coordinates

    Args:
        svg_contents: List of tuples. Each tuple is either:
            - (svg_content: str, name: str) for default placement
            - (svg_content: str, name: str, x: float, y: float) for positioned placement
        container_viewbox: ViewBox for the container SVG
        container_width: Width of container
        container_height: Height of container

    Returns:
        dict with:
            - 'content': Combined SVG content
            - 'prefixes': Dict mapping names to prefixes used
            - 'stats': Combined stats
            - 'positions': Dict mapping names to (x, y) positions
    """
    prefixes_used = {}
    positions_used = {}
    all_prefixed = []
    total_ids = 0
    total_refs = 0

    for i, item in enumerate(svg_contents):
        # Handle both (content, name) and (content, name, x, y) formats
        if len(item) == 2:
            svg_content, name = item
            x, y = 0.0, 0.0
        elif len(item) == 4:
            svg_content, name, x, y = item
        else:
            raise ValueError(
                f"Invalid tuple format: expected 2 or 4 elements, got {len(item)}"
            )

        # Generate unique short prefix for each SVG
        prefix = _index_to_prefix(i)

        result = prefix_svg_ids(svg_content, prefix=prefix, verify=True)

        if result["errors"]:
            raise ValueError(f"Prefixing failed for {name}: {result['errors']}")

        prefixes_used[name] = prefix
        positions_used[name] = (x, y)
        all_prefixed.append((result["content"], name, x, y))
        total_ids += result["stats"]["ids_found"]
        total_refs += result["stats"]["references_updated"]

    # Combine into a single SVG
    combined_parts = []
    for prefixed_content, name, x, y in all_prefixed:
        inner = _extract_svg_inner(prefixed_content)
        # Apply transform only if position is non-zero
        if x != 0 or y != 0:
            transform_attr = f' transform="translate({x}, {y})"'
        else:
            transform_attr = ""
        combined_parts.append(
            f"<!-- BEGIN: {name} -->\n"
            f'<g id="{prefixes_used[name]}root"{transform_attr}>\n{inner}\n</g>\n'
            f"<!-- END: {name} -->"
        )

    combined_svg = f'''<?xml version="1.0" encoding="UTF-8"?>
<svg xmlns="http://www.w3.org/2000/svg"
     xmlns:xlink="http://www.w3.org/1999/xlink"
     width="{container_width}" height="{container_height}"
     viewBox="{container_viewbox}">
{chr(10).join(combined_parts)}
</svg>'''

    return {
        "content": combined_svg,
        "prefixes": prefixes_used,
        "positions": positions_used,
        "stats": {
            "total_ids": total_ids,
            "total_references": total_refs,
            "svg_count": len(svg_contents),
        },
    }


def tile_svgs_grid(
    svg_contents: list[tuple[str, str]],
    columns: int = 2,
    cell_width: float = 400,
    cell_height: float = 300,
    padding: float = 10,
) -> list[tuple[str, str, float, float]]:
    """
    Arrange SVGs in a grid layout, returning positioned tuples for combine_svgs_with_prefixes.

    Args:
        svg_contents: List of (svg_content, name) tuples
        columns: Number of columns in the grid
        cell_width: Width of each cell
        cell_height: Height of each cell
        padding: Padding between cells

    Returns:
        List of (svg_content, name, x, y) tuples with calculated positions
    """
    positioned = []
    for i, (content, name) in enumerate(svg_contents):
        col = i % columns
        row = i // columns
        x = col * (cell_width + padding)
        y = row * (cell_height + padding)
        positioned.append((content, name, x, y))
    return positioned


def calculate_grid_container_size(
    count: int,
    columns: int = 2,
    cell_width: float = 400,
    cell_height: float = 300,
    padding: float = 10,
) -> tuple[int, int, str]:
    """
    Calculate container dimensions for a grid of SVGs.

    Args:
        count: Number of SVGs
        columns: Number of columns
        cell_width: Width of each cell
        cell_height: Height of each cell
        padding: Padding between cells

    Returns:
        (width, height, viewbox_string) tuple
    """
    rows = (count + columns - 1) // columns
    width = int(columns * cell_width + (columns - 1) * padding)
    height = int(rows * cell_height + (rows - 1) * padding)
    viewbox = f"0 0 {width} {height}"
    return width, height, viewbox


def _index_to_prefix(index: int) -> str:
    """Convert index to short prefix: 0->a_, 1->b_, ..., 25->z_, 26->aa_, etc."""
    chars = string.ascii_lowercase
    result = []
    n = index
    while True:
        result.append(chars[n % 26])
        n = n // 26 - 1
        if n < 0:
            break
    return "".join(reversed(result)) + "_"


def _extract_svg_inner(svg_content: str) -> str:
    """Extract content between <svg> and </svg> tags."""
    match = re.search(r"<svg[^>]*>", svg_content, re.IGNORECASE)
    if not match:
        return svg_content
    start = match.end()

    end = svg_content.rfind("</svg>")
    if end == -1:
        return svg_content[start:]

    return svg_content[start:end]


# CLI interface
if __name__ == "__main__":
    import argparse
    import sys
    import time

    parser = argparse.ArgumentParser(
        description="Prefix all IDs in SVG files to enable safe combining"
    )
    parser.add_argument("input", help="Input SVG file")
    parser.add_argument("-o", "--output", help="Output file (default: overwrite input)")
    parser.add_argument(
        "-p", "--prefix", help="Custom prefix (default: auto-generated)"
    )
    parser.add_argument(
        "-v",
        "--verify",
        action="store_true",
        default=True,
        help="Verify no unprefixed IDs remain (default: True)",
    )
    parser.add_argument(
        "--no-verify", action="store_false", dest="verify", help="Skip verification"
    )
    parser.add_argument(
        "-q", "--quiet", action="store_true", help="Suppress output except errors"
    )

    args = parser.parse_args()

    try:
        start_time = time.time()

        result = prefix_svg_file(
            args.input, output_path=args.output, prefix=args.prefix, verify=args.verify
        )

        elapsed = time.time() - start_time

        if not args.quiet:
            print(f"Prefix used: {result['prefix']}")
            print(f"IDs found: {result['stats']['ids_found']}")
            print(f"References updated: {result['stats']['references_updated']}")
            print(f"Time: {elapsed:.2f}s")
            print(f"Output: {result['output_path']}")

        if result["errors"]:
            print(f"ERRORS: {len(result['errors'])}", file=sys.stderr)
            for err in result["errors"][:10]:  # Limit error output
                print(f"  - {err}", file=sys.stderr)
            if len(result["errors"]) > 10:
                print(f"  ... and {len(result['errors']) - 10} more", file=sys.stderr)
            sys.exit(1)

        if not args.quiet:
            print("SUCCESS: All IDs prefixed correctly")

    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        import traceback

        traceback.print_exc()
        sys.exit(1)
