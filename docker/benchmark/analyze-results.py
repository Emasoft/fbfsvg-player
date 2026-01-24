#!/usr/bin/env python3
"""
analyze-results.py - Analyze and compare benchmark results between ThorVG and fbfsvg-player

Reads JSON benchmark files and generates comparison reports with:
- FPS comparison tables
- Performance ratio analysis
- Statistical summaries
- Markdown report generation

Usage:
    ./analyze-results.py results/benchmark_*.json
    ./analyze-results.py results/ --output report.md
"""

import json
import sys
import os
import argparse
from pathlib import Path
from datetime import datetime
from typing import Dict, List, Any, Optional
import statistics

# ANSI color codes for terminal output
class Colors:
    RED = '\033[0;31m'
    GREEN = '\033[0;32m'
    YELLOW = '\033[1;33m'
    CYAN = '\033[0;36m'
    BOLD = '\033[1m'
    NC = '\033[0m'  # No Color


def load_benchmark_results(path: Path) -> List[Dict[str, Any]]:
    """Load benchmark results from JSON file(s)."""
    results = []

    if path.is_file():
        with open(path) as f:
            data = json.load(f)
            if isinstance(data, list):
                results.extend(data)
            else:
                results.append(data)
    elif path.is_dir():
        for json_file in sorted(path.glob("benchmark_*.json")):
            with open(json_file) as f:
                data = json.load(f)
                if isinstance(data, list):
                    results.extend(data)
                else:
                    results.append(data)

    return results


def calculate_stats(values: List[float]) -> Dict[str, float]:
    """Calculate statistical summary for a list of values."""
    if not values:
        return {"mean": 0, "std": 0, "min": 0, "max": 0, "median": 0}

    return {
        "mean": statistics.mean(values),
        "std": statistics.stdev(values) if len(values) > 1 else 0,
        "min": min(values),
        "max": max(values),
        "median": statistics.median(values)
    }


def group_by_file(results: List[Dict[str, Any]]) -> Dict[str, Dict[str, List[Dict]]]:
    """Group results by SVG filename and player type."""
    grouped = {}

    for result in results:
        svg_file = result.get("file", "unknown")
        player = result.get("player", "unknown")

        if svg_file not in grouped:
            grouped[svg_file] = {"thorvg": [], "fbfsvg": []}

        if "thorvg" in player.lower():
            grouped[svg_file]["thorvg"].append(result)
        else:
            grouped[svg_file]["fbfsvg"].append(result)

    return grouped


def compare_players(grouped: Dict[str, Dict[str, List[Dict]]]) -> List[Dict[str, Any]]:
    """Compare ThorVG and fbfsvg-player for each file."""
    comparisons = []

    for svg_file, players in grouped.items():
        thorvg_fps = [r.get("avg_fps", 0) for r in players["thorvg"] if r.get("avg_fps")]
        fbfsvg_fps = [r.get("avg_fps", 0) for r in players["fbfsvg"] if r.get("avg_fps")]

        thorvg_frame_time = [r.get("avg_frame_time_ms", 0) for r in players["thorvg"] if r.get("avg_frame_time_ms")]
        fbfsvg_frame_time = [r.get("avg_frame_time_ms", 0) for r in players["fbfsvg"] if r.get("avg_frame_time_ms")]

        comparison = {
            "file": svg_file,
            "thorvg": {
                "runs": len(players["thorvg"]),
                "fps": calculate_stats(thorvg_fps),
                "frame_time_ms": calculate_stats(thorvg_frame_time)
            },
            "fbfsvg": {
                "runs": len(players["fbfsvg"]),
                "fps": calculate_stats(fbfsvg_fps),
                "frame_time_ms": calculate_stats(fbfsvg_frame_time)
            }
        }

        # Calculate performance ratio (fbfsvg / thorvg)
        if comparison["thorvg"]["fps"]["mean"] > 0:
            comparison["fps_ratio"] = comparison["fbfsvg"]["fps"]["mean"] / comparison["thorvg"]["fps"]["mean"]
        else:
            comparison["fps_ratio"] = None

        comparisons.append(comparison)

    return comparisons


def print_comparison_table(comparisons: List[Dict[str, Any]]):
    """Print a formatted comparison table to stdout."""
    # Unicode box drawing characters
    H = "─"
    V = "│"
    TL = "┌"
    TR = "┐"
    BL = "└"
    BR = "┘"
    TM = "┬"
    BM = "┴"
    LM = "├"
    RM = "┤"
    X = "┼"

    # Column widths
    w_file = 30
    w_thorvg = 15
    w_fbfsvg = 15
    w_ratio = 12
    w_winner = 10

    # Header
    header = f"{V} {'SVG File':<{w_file}} {V} {'ThorVG FPS':>{w_thorvg}} {V} {'fbfsvg FPS':>{w_fbfsvg}} {V} {'Ratio':>{w_ratio}} {V} {'Winner':^{w_winner}} {V}"
    total_width = len(header)

    print(f"\n{Colors.BOLD}Performance Comparison: ThorVG vs fbfsvg-player{Colors.NC}\n")

    # Top border
    print(TL + H * (w_file + 2) + TM + H * (w_thorvg + 2) + TM + H * (w_fbfsvg + 2) + TM + H * (w_ratio + 2) + TM + H * (w_winner + 2) + TR)

    # Header row
    print(f"{V} {Colors.BOLD}{'SVG File':<{w_file}}{Colors.NC} {V} {Colors.BOLD}{'ThorVG FPS':>{w_thorvg}}{Colors.NC} {V} {Colors.BOLD}{'fbfsvg FPS':>{w_fbfsvg}}{Colors.NC} {V} {Colors.BOLD}{'Ratio':>{w_ratio}}{Colors.NC} {V} {Colors.BOLD}{'Winner':^{w_winner}}{Colors.NC} {V}")

    # Header separator
    print(LM + H * (w_file + 2) + X + H * (w_thorvg + 2) + X + H * (w_fbfsvg + 2) + X + H * (w_ratio + 2) + X + H * (w_winner + 2) + RM)

    # Data rows
    for comp in comparisons:
        file_name = Path(comp["file"]).name
        if len(file_name) > w_file:
            file_name = file_name[:w_file-3] + "..."

        thorvg_fps = comp["thorvg"]["fps"]["mean"]
        fbfsvg_fps = comp["fbfsvg"]["fps"]["mean"]
        ratio = comp.get("fps_ratio")

        # Determine winner
        if thorvg_fps > 0 and fbfsvg_fps > 0:
            if ratio and ratio > 1.05:
                winner = f"{Colors.GREEN}fbfsvg{Colors.NC}"
                winner_raw = "fbfsvg"
            elif ratio and ratio < 0.95:
                winner = f"{Colors.RED}ThorVG{Colors.NC}"
                winner_raw = "ThorVG"
            else:
                winner = f"{Colors.YELLOW}~Tie{Colors.NC}"
                winner_raw = "~Tie"
        else:
            winner = "-"
            winner_raw = "-"

        thorvg_str = f"{thorvg_fps:.1f}" if thorvg_fps > 0 else "N/A"
        fbfsvg_str = f"{fbfsvg_fps:.1f}" if fbfsvg_fps > 0 else "N/A"
        ratio_str = f"{ratio:.2f}x" if ratio else "N/A"

        # Color the ratio
        if ratio:
            if ratio > 1.1:
                ratio_str = f"{Colors.GREEN}{ratio:.2f}x{Colors.NC}"
            elif ratio < 0.9:
                ratio_str = f"{Colors.RED}{ratio:.2f}x{Colors.NC}"
            else:
                ratio_str = f"{Colors.YELLOW}{ratio:.2f}x{Colors.NC}"

        # Need to calculate visible width for winner column (without color codes)
        winner_padding = w_winner - len(winner_raw)
        winner_padded = " " * (winner_padding // 2) + winner + " " * (winner_padding - winner_padding // 2)

        print(f"{V} {file_name:<{w_file}} {V} {thorvg_str:>{w_thorvg}} {V} {fbfsvg_str:>{w_fbfsvg}} {V} {ratio_str:>{w_ratio + (len(ratio_str) - len(ratio_str.replace(Colors.GREEN, '').replace(Colors.RED, '').replace(Colors.YELLOW, '').replace(Colors.NC, '')))}} {V} {winner_padded} {V}")

    # Bottom border
    print(BL + H * (w_file + 2) + BM + H * (w_thorvg + 2) + BM + H * (w_fbfsvg + 2) + BM + H * (w_ratio + 2) + BM + H * (w_winner + 2) + BR)

    # Summary
    print(f"\n{Colors.CYAN}Legend:{Colors.NC}")
    print(f"  Ratio = fbfsvg FPS / ThorVG FPS (>1.0 means fbfsvg is faster)")
    print(f"  {Colors.GREEN}Green{Colors.NC} = fbfsvg wins (>5% faster)")
    print(f"  {Colors.RED}Red{Colors.NC} = ThorVG wins (>5% faster)")
    print(f"  {Colors.YELLOW}Yellow{Colors.NC} = Tie (within 5%)")


def generate_markdown_report(comparisons: List[Dict[str, Any]], output_path: Path):
    """Generate a markdown report file."""
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    lines = [
        "# SVG Player Benchmark Report",
        "",
        f"Generated: {timestamp}",
        "",
        "## Summary",
        "",
        "| SVG File | ThorVG FPS | fbfsvg FPS | Ratio | Winner |",
        "|----------|------------|------------|-------|--------|",
    ]

    total_thorvg = 0
    total_fbfsvg = 0
    count = 0

    for comp in comparisons:
        file_name = Path(comp["file"]).name
        thorvg_fps = comp["thorvg"]["fps"]["mean"]
        fbfsvg_fps = comp["fbfsvg"]["fps"]["mean"]
        ratio = comp.get("fps_ratio")

        if thorvg_fps > 0 and fbfsvg_fps > 0:
            total_thorvg += thorvg_fps
            total_fbfsvg += fbfsvg_fps
            count += 1

        if ratio and ratio > 1.05:
            winner = "✅ fbfsvg"
        elif ratio and ratio < 0.95:
            winner = "❌ ThorVG"
        else:
            winner = "🟡 Tie"

        thorvg_str = f"{thorvg_fps:.1f}" if thorvg_fps > 0 else "N/A"
        fbfsvg_str = f"{fbfsvg_fps:.1f}" if fbfsvg_fps > 0 else "N/A"
        ratio_str = f"{ratio:.2f}x" if ratio else "N/A"

        lines.append(f"| {file_name} | {thorvg_str} | {fbfsvg_str} | {ratio_str} | {winner} |")

    # Overall summary
    if count > 0:
        avg_thorvg = total_thorvg / count
        avg_fbfsvg = total_fbfsvg / count
        avg_ratio = avg_fbfsvg / avg_thorvg if avg_thorvg > 0 else 0

        lines.extend([
            "",
            "## Overall Statistics",
            "",
            f"- **Files tested**: {count}",
            f"- **Average ThorVG FPS**: {avg_thorvg:.1f}",
            f"- **Average fbfsvg FPS**: {avg_fbfsvg:.1f}",
            f"- **Average performance ratio**: {avg_ratio:.2f}x",
            "",
        ])

        if avg_ratio > 1.0:
            lines.append(f"**Conclusion**: fbfsvg-player is {((avg_ratio - 1) * 100):.0f}% faster on average")
        else:
            lines.append(f"**Conclusion**: ThorVG is {((1 - avg_ratio) * 100):.0f}% faster on average")

    # Detailed stats
    lines.extend([
        "",
        "## Detailed Statistics",
        "",
    ])

    for comp in comparisons:
        file_name = Path(comp["file"]).name
        lines.extend([
            f"### {file_name}",
            "",
            "| Metric | ThorVG | fbfsvg |",
            "|--------|--------|--------|",
            f"| Runs | {comp['thorvg']['runs']} | {comp['fbfsvg']['runs']} |",
            f"| Mean FPS | {comp['thorvg']['fps']['mean']:.1f} | {comp['fbfsvg']['fps']['mean']:.1f} |",
            f"| Std FPS | {comp['thorvg']['fps']['std']:.1f} | {comp['fbfsvg']['fps']['std']:.1f} |",
            f"| Min FPS | {comp['thorvg']['fps']['min']:.1f} | {comp['fbfsvg']['fps']['min']:.1f} |",
            f"| Max FPS | {comp['thorvg']['fps']['max']:.1f} | {comp['fbfsvg']['fps']['max']:.1f} |",
            f"| Mean Frame Time (ms) | {comp['thorvg']['frame_time_ms']['mean']:.2f} | {comp['fbfsvg']['frame_time_ms']['mean']:.2f} |",
            "",
        ])

    # Write report
    with open(output_path, "w") as f:
        f.write("\n".join(lines))

    print(f"\n{Colors.GREEN}Report written to: {output_path}{Colors.NC}")


def main():
    parser = argparse.ArgumentParser(
        description="Analyze and compare benchmark results between ThorVG and fbfsvg-player"
    )
    parser.add_argument(
        "input",
        type=Path,
        help="JSON benchmark file or directory containing benchmark_*.json files"
    )
    parser.add_argument(
        "--output", "-o",
        type=Path,
        default=None,
        help="Output markdown report file (optional)"
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Output comparison data as JSON"
    )

    args = parser.parse_args()

    if not args.input.exists():
        print(f"{Colors.RED}Error: {args.input} does not exist{Colors.NC}", file=sys.stderr)
        sys.exit(1)

    # Load results
    results = load_benchmark_results(args.input)

    if not results:
        print(f"{Colors.RED}Error: No benchmark results found{Colors.NC}", file=sys.stderr)
        sys.exit(1)

    print(f"{Colors.CYAN}Loaded {len(results)} benchmark results{Colors.NC}")

    # Group and compare
    grouped = group_by_file(results)
    comparisons = compare_players(grouped)

    # Output
    if args.json:
        print(json.dumps(comparisons, indent=2))
    else:
        print_comparison_table(comparisons)

        if args.output:
            generate_markdown_report(comparisons, args.output)


if __name__ == "__main__":
    main()
