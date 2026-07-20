#!/usr/bin/env python3
"""Render theme-adaptive animated SVG charts from benchmarks/results.json.

- Transparent background (works on light & dark README themes)
- Text/grid colors adapt via prefers-color-scheme
- Bars animate with CSS keyframes (works in <img> SVG viewers)
"""
from __future__ import annotations

import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / "benchmarks" / "results.json"
OUT_RPS = ROOT / "assets" / "benchmark_rps.svg"
OUT_LAT = ROOT / "assets" / "benchmark_latency.svg"

COLORS = {
    "Socketify": "#0D9488",
    "Express (Node)": "#16A34A",
    "Flask (Waitress)": "#2563EB",
    "Django (Waitress)": "#15803D",
}

SHORT = {
    "Socketify": "Socketify",
    "Express (Node)": "Express",
    "Flask (Waitress)": "Flask",
    "Django (Waitress)": "Django",
}


def _nice_max(v: float) -> float:
    if v <= 0:
        return 1.0
    exp = 10 ** math.floor(math.log10(v))
    for m in (1, 2, 2.5, 5, 10):
        if m * exp >= v:
            return m * exp
    return 10 * exp


def _fmt_rps(v: float) -> str:
    return f"{v:,.0f}"


def _fmt_ms(v: float) -> str:
    if v < 1:
        return f"{v:.3f} ms"
    if v < 10:
        return f"{v:.2f} ms"
    return f"{v:.0f} ms"


def write_bar_chart(
    *,
    path: Path,
    title: str,
    subtitle: str,
    rows: list[tuple[str, float, str]],
    higher_better: bool,
) -> None:
    width, height = 920, 400
    ml, mr, mt, mb = 150, 120, 78, 52
    chart_w = width - ml - mr
    chart_h = height - mt - mb
    n = max(len(rows), 1)
    gap = chart_h / n
    bar_h = gap * 0.55
    vmax = _nice_max(max(v for _, v, _ in rows) * 1.05)
    ticks = 4
    tick_vals = [vmax * i / ticks for i in range(ticks + 1)]

    parts: list[str] = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
        f'viewBox="0 0 {width} {height}" role="img" aria-label="{title}">',
        "<defs>",
        "  <style><![CDATA[",
        "    .title { font: 700 20px system-ui, -apple-system, 'Segoe UI', sans-serif; fill: #1e293b; }",
        "    .sub   { font: 12.5px system-ui, -apple-system, 'Segoe UI', sans-serif; fill: #64748b; }",
        "    .label { font: 600 14px system-ui, -apple-system, 'Segoe UI', sans-serif; fill: #334155; }",
        "    .value { font: 700 13px system-ui, -apple-system, 'Segoe UI', sans-serif; fill: #0f172a;",
        "             opacity: 0; animation: fadeIn 0.45s ease-out forwards; }",
        "    .tick  { font: 11px system-ui, -apple-system, 'Segoe UI', sans-serif; fill: #64748b; }",
        "    .grid  { stroke: #cbd5e1; stroke-width: 1; stroke-dasharray: 4 5; opacity: 0.9; }",
        "    .axis  { stroke: #94a3b8; stroke-width: 1.25; fill: none; }",
        "    .bar-group { transform-box: fill-box; transform-origin: left center;",
        "                 transform: scaleX(0); animation: growBar 1.05s cubic-bezier(0.22, 0.61, 0.36, 1) forwards; }",
        "    @keyframes growBar { from { transform: scaleX(0); } to { transform: scaleX(1); } }",
        "    @keyframes fadeIn  { from { opacity: 0; } to { opacity: 1; } }",
        "    @media (prefers-color-scheme: dark) {",
        "      .title { fill: #f1f5f9; }",
        "      .sub   { fill: #94a3b8; }",
        "      .label { fill: #e2e8f0; }",
        "      .value { fill: #f8fafc; }",
        "      .tick  { fill: #94a3b8; }",
        "      .grid  { stroke: #475569; opacity: 0.75; }",
        "      .axis  { stroke: #64748b; }",
        "    }",
        "    @media (prefers-reduced-motion: reduce) {",
        "      .bar-group { animation: none; transform: scaleX(1); }",
        "      .value { animation: none; opacity: 1; }",
        "    }",
        "  ]]></style>",
        "</defs>",
        f'<text class="title" x="{width/2}" y="30" text-anchor="middle">{title}</text>',
        f'<text class="sub" x="{width/2}" y="52" text-anchor="middle">{subtitle}</text>',
        f'<line class="axis" x1="{ml}" y1="{mt}" x2="{ml}" y2="{mt + chart_h}" />',
        f'<line class="axis" x1="{ml}" y1="{mt + chart_h}" x2="{ml + chart_w}" y2="{mt + chart_h}" />',
    ]

    for tv in tick_vals[1:]:
        x = ml + (tv / vmax) * chart_w
        parts.append(f'<line class="grid" x1="{x:.1f}" y1="{mt}" x2="{x:.1f}" y2="{mt + chart_h}" />')
        if higher_better:
            tick_label = _fmt_rps(tv)
        else:
            tick_label = f"{tv:.2f}" if tv < 10 else f"{tv:.0f}"
        parts.append(
            f'<text class="tick" x="{x:.1f}" y="{mt + chart_h + 18}" text-anchor="middle">{tick_label}</text>'
        )

    hint = "Higher is better · animated SVG · transparent for light/dark themes"
    if not higher_better:
        hint = "Lower is better · animated SVG · transparent for light/dark themes"
    parts.append(
        f'<text class="sub" x="{width/2}" y="{height - 12}" text-anchor="middle">{hint}</text>'
    )

    for i, (name, value, color) in enumerate(rows):
        y = mt + i * gap + (gap - bar_h) / 2
        target_w = max(4.0, (value / vmax) * chart_w)
        delay = 0.12 + i * 0.14
        value_delay = delay + 0.75

        parts.append(
            f'<text class="label" x="{ml - 14}" y="{y + bar_h/2 + 5:.1f}" text-anchor="end">{name}</text>'
        )
        # Wrapper group scales from the left; rect has final width
        parts.append(
            f'<g class="bar-group" style="animation-delay: {delay:.2f}s">'
            f'<rect x="{ml}" y="{y:.1f}" width="{target_w:.1f}" height="{bar_h:.1f}" '
            f'rx="8" fill="{color}" opacity="0.95"/>'
            f"</g>"
        )
        val_text = _fmt_rps(value) if higher_better else _fmt_ms(value)
        parts.append(
            f'<text class="value" x="{ml + target_w + 12:.1f}" y="{y + bar_h/2 + 5:.1f}" '
            f'style="animation-delay: {value_delay:.2f}s">{val_text}</text>'
        )

    parts.append("</svg>")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(parts) + "\n", encoding="utf-8")
    print(f"wrote {path}")


def main() -> None:
    data = json.loads(RESULTS.read_text(encoding="utf-8"))
    results = data["results"]

    rps_rows = [
        (SHORT.get(r["name"], r["name"]), float(r["rps"]), COLORS.get(r["name"], "#64748b"))
        for r in sorted(results, key=lambda x: x["rps"], reverse=True)
    ]
    lat_rows = [
        (
            SHORT.get(r["name"], r["name"]),
            float(r["latency_p99_ms"]),
            COLORS.get(r["name"], "#64748b"),
        )
        for r in sorted(results, key=lambda x: x["latency_p99_ms"])
    ]

    write_bar_chart(
        path=OUT_RPS,
        title="JSON /ping throughput (req/s)",
        subtitle="Socketify vs Express · Flask · Django — same machine (wrk -t4 -c100 -d8s)",
        rows=rps_rows,
        higher_better=True,
    )
    write_bar_chart(
        path=OUT_LAT,
        title="P99 latency (ms)",
        subtitle="Socketify vs Express · Flask · Django — same machine (wrk -t4 -c100 -d8s)",
        rows=lat_rows,
        higher_better=False,
    )


if __name__ == "__main__":
    main()
