#!/usr/bin/env python3
"""Create nested SVG experiment by appending fbf.svg files as child nodes."""

import xml.etree.ElementTree as ET
import re
import os

# Register namespaces - use empty prefix for SVG namespace so elements don't get svg: prefix
ET.register_namespace('', 'http://www.w3.org/2000/svg')
ET.register_namespace('xlink', 'http://www.w3.org/1999/xlink')
ET.register_namespace('cc', 'http://creativecommons.org/ns#')
ET.register_namespace('dc', 'http://purl.org/dc/elements/1.1/')
ET.register_namespace('fbf', 'http://opentoonz.github.io/fbf/1.0#')
ET.register_namespace('rdf', 'http://www.w3.org/1999/02/22-rdf-syntax-ns#')

# Paths
base_dir = '/Users/emanuelesabetta/Code/SKIA-BUILD-ARM64/svg_input_samples'
base_file = os.path.join(base_dir, 'seagull.fbf.svg')
output_file = os.path.join(base_dir, 'nested_experiment.fbf.svg')

# Files to nest (in order) with their original dimensions
nested_files = [
    ('panther_bird.fbf.svg', 1280, 720),
    ('walk_cycle.fbf.svg', 512, 512),
    ('panther_bird_header.fbf.svg', 1280, 720),
    ('girl_hair.fbf.svg', 1200, 674),
]

# Load base SVG
print(f"Loading base: {base_file}")
tree = ET.parse(base_file)
root = tree.getroot()

# Find STAGE_BACKGROUND group
ns = {'svg': 'http://www.w3.org/2000/svg'}
stage_bg = root.find('.//{http://www.w3.org/2000/svg}g[@id="STAGE_BACKGROUND"]')

if stage_bg is None:
    # Try without namespace
    stage_bg = root.find('.//g[@id="STAGE_BACKGROUND"]')

if stage_bg is None:
    print("ERROR: Could not find STAGE_BACKGROUND group!")
    exit(1)

print(f"Found STAGE_BACKGROUND group")

# Seagull viewBox: -48.35 -51.55 98.2 144.8
# Position tiles starting from top-left, arranged horizontally then wrapping
start_x = -48.35
start_y = -51.55
current_x = start_x
current_y = start_y
max_row_height = 0

for i, (filename, orig_w, orig_h) in enumerate(nested_files):
    filepath = os.path.join(base_dir, filename)
    print(f"Loading nested SVG {i+1}: {filename} ({orig_w}x{orig_h})")

    # Load the nested SVG
    nested_tree = ET.parse(filepath)
    nested_root = nested_tree.getroot()

    # Calculate 1/20 scaled dimensions (1/4 was too large for seagull viewBox ~98x145)
    scaled_w = orig_w / 20
    scaled_h = orig_h / 20

    # Check if we need to wrap to next row (if exceeds ~100 units width)
    if current_x + scaled_w > start_x + 100 and current_x != start_x:
        current_x = start_x
        current_y += max_row_height + 2  # 2 unit gap
        max_row_height = 0

    # Set positioning attributes on the nested SVG root
    nested_root.set('x', str(current_x))
    nested_root.set('y', str(current_y))
    nested_root.set('width', str(scaled_w))
    nested_root.set('height', str(scaled_h))
    nested_root.set('overflow', 'hidden')  # Force clipping to prevent defs leaking

    # Preserve viewBox (don't touch it per user instruction)
    # The viewBox is already set in each file

    # Add unique ID prefix to avoid conflicts
    prefix = f"nested{i}_"

    # Get viewBox for clipPath (Skia bug: defs render in nested SVGs, need explicit clip)
    viewBox = nested_root.get('viewBox', f'0 0 {orig_w} {orig_h}')
    vb_parts = viewBox.split()
    if len(vb_parts) == 4:
        vb_x, vb_y, vb_w, vb_h = vb_parts
        # Create clipPath that matches viewBox
        clip_id = f"{prefix}viewbox_clip"
        defs_elem = nested_root.find('.//{http://www.w3.org/2000/svg}defs')
        if defs_elem is None:
            defs_elem = ET.SubElement(nested_root, 'defs')
            nested_root.insert(0, defs_elem)  # Put defs first
        clip_path = ET.SubElement(defs_elem, 'clipPath')
        clip_path.set('id', clip_id)
        clip_rect = ET.SubElement(clip_path, 'rect')
        clip_rect.set('x', vb_x)
        clip_rect.set('y', vb_y)
        clip_rect.set('width', vb_w)
        clip_rect.set('height', vb_h)
        # Apply clip to root element
        nested_root.set('clip-path', f'url(#{clip_id})')

    # SKIA BUG WORKAROUND: Skia renders defs content directly in nested SVGs
    # Move all frame elements far outside the viewBox, then add counter-transform on <use>
    OFFSET_X = 10000  # Move frames this far outside viewBox

    # Find all frame elements in defs (typically id="frame0001", "FRAME001", etc.)
    defs_elem = nested_root.find('.//{http://www.w3.org/2000/svg}defs')
    if defs_elem is None:
        defs_elem = nested_root.find('.//defs')

    # Track which frame IDs were transformed
    transformed_frame_ids = set()

    if defs_elem is not None:
        frame_pattern = re.compile(r'^(frame|FRAME)\d+', re.IGNORECASE)
        for elem in defs_elem:
            elem_id = elem.get('id', '')
            if frame_pattern.match(elem_id):
                # Add transform to move frame content outside viewBox
                existing_transform = elem.get('transform', '')
                new_transform = f"translate(-{OFFSET_X}, 0)"
                if existing_transform:
                    new_transform = f"{new_transform} {existing_transform}"
                elem.set('transform', new_transform)
                transformed_frame_ids.add(elem_id)
                print(f"    Applied offset transform to {elem_id}")

    # Only apply counter-transform to <use> elements that reference transformed frames
    if transformed_frame_ids:
        for use_elem in nested_root.iter('{http://www.w3.org/2000/svg}use'):
            href = use_elem.get('{http://www.w3.org/1999/xlink}href', '') or use_elem.get('href', '')
            if href.startswith('#'):
                ref_id = href[1:]
                if ref_id in transformed_frame_ids:
                    existing_transform = use_elem.get('transform', '')
                    counter_transform = f"translate({OFFSET_X}, 0)"
                    if existing_transform:
                        counter_transform = f"{counter_transform} {existing_transform}"
                    use_elem.set('transform', counter_transform)
                    print(f"    Applied counter-transform to <use> referencing {ref_id}")

        # Also check without namespace
        for use_elem in nested_root.iter('use'):
            if '{http://www.w3.org/2000/svg}' not in use_elem.tag:
                href = use_elem.get('{http://www.w3.org/1999/xlink}href', '') or use_elem.get('href', '')
                if href.startswith('#'):
                    ref_id = href[1:]
                    if ref_id in transformed_frame_ids:
                        existing_transform = use_elem.get('transform', '')
                        if f"translate({OFFSET_X}" not in existing_transform:  # Avoid double-applying
                            counter_transform = f"translate({OFFSET_X}, 0)"
                            if existing_transform:
                                counter_transform = f"{counter_transform} {existing_transform}"
                            use_elem.set('transform', counter_transform)
                            print(f"    Applied counter-transform to <use> referencing {ref_id} (no ns)")

    # Rename IDs in the nested SVG to avoid conflicts
    def prefix_ids(element, prefix):
        # Update id attribute
        if 'id' in element.attrib:
            element.set('id', prefix + element.get('id'))

        # Update xlink:href references
        xlink_href = element.get('{http://www.w3.org/1999/xlink}href')
        if xlink_href and xlink_href.startswith('#'):
            element.set('{http://www.w3.org/1999/xlink}href', '#' + prefix + xlink_href[1:])

        # Update href references (SVG 2)
        href = element.get('href')
        if href and href.startswith('#'):
            element.set('href', '#' + prefix + href[1:])

        # Update values attribute in animate elements (contains frame refs like #FRAME001;#FRAME002)
        values = element.get('values')
        if values and '#' in values:
            # Replace all #ID references with #prefix_ID
            new_values = re.sub(r'#([A-Za-z0-9_]+)', f'#{prefix}\\1', values)
            element.set('values', new_values)

        # Update url() references in attributes like clip-path, fill, stroke, mask, filter
        for attr in ['clip-path', 'fill', 'stroke', 'mask', 'filter', 'marker-start', 'marker-mid', 'marker-end']:
            attr_val = element.get(attr)
            if attr_val and 'url(#' in attr_val:
                # Replace url(#id) with url(#prefix_id)
                new_val = re.sub(r'url\(#([A-Za-z0-9_]+)\)', f'url(#{prefix}\\1)', attr_val)
                element.set(attr, new_val)

        # Also check style attribute for url() references
        style = element.get('style')
        if style and 'url(#' in style:
            new_style = re.sub(r'url\(#([A-Za-z0-9_]+)\)', f'url(#{prefix}\\1)', style)
            element.set('style', new_style)

        # Recurse into children
        for child in element:
            prefix_ids(child, prefix)

    prefix_ids(nested_root, prefix)

    # Append to STAGE_BACKGROUND
    stage_bg.append(nested_root)
    print(f"  Added at x={current_x:.1f}, y={current_y:.1f}, size={scaled_w:.1f}x{scaled_h:.1f}")

    # Update position for next tile
    current_x += scaled_w + 2  # 2 unit gap between tiles
    max_row_height = max(max_row_height, scaled_h)

# Write output
print(f"\nWriting combined SVG to: {output_file}")
tree.write(output_file, encoding='unicode', xml_declaration=True)

# Post-process to remove svg: namespace prefix (Skia doesn't like it)
print("Removing svg: namespace prefixes...")
with open(output_file, 'r') as f:
    content = f.read()

# Remove svg: prefix from elements
content = content.replace('<svg:', '<')
content = content.replace('</svg:', '</')
content = content.replace('xmlns:svg="http://www.w3.org/2000/svg"', '')

with open(output_file, 'w') as f:
    f.write(content)

print("Done!")
