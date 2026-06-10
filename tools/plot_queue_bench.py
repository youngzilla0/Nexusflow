#!/usr/bin/env python3

import re
import sys
from pathlib import Path


LINE_RE = re.compile(
    r"^(BM_(?P<impl>LBQ|LFQ|LFN)_(?P<payload>Int|Ptr|Blob)_PC)"
    r"/real_time/threads:(?P<threads>\d+).*?"
    r"items_per_second=(?P<ips>[0-9.]+)(?P<ips_unit>[kM]?)/s"
    r".*?pop_fail=(?P<pop_fail>[0-9.]+)(?P<pop_fail_unit>[kMG]?)"
    r".*?pop_ok=(?P<pop_ok>[0-9.]+)(?P<pop_ok_unit>[kMG]?)"
    r".*?push_fail=(?P<push_fail>[0-9.]+)(?P<push_fail_unit>[kMG]?)"
    r".*?push_ok=(?P<push_ok>[0-9.]+)(?P<push_ok_unit>[kMG]?)$"
)

IMPL_LABEL = {"LBQ": "LockBaseQueue", "LFQ": "LockFreeQueue", "LFN": "LockFreeNodeQueue"}
PAYLOAD_LABEL = {"Int": "int", "Ptr": "shared_ptr", "Blob": "Blob2K"}
COLORS = {"LBQ": "#1f77b4", "LFQ": "#d62728", "LFN": "#2ca02c"}


def parse_number(value: str, unit: str) -> float:
    scale = {"": 1.0, "k": 1e3, "M": 1e6, "G": 1e9}
    return float(value) * scale[unit]


def parse_file(path: Path):
    data = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        match = LINE_RE.match(line)
        if not match:
            continue
        groups = match.groupdict()
        impl = groups["impl"]
        payload = groups["payload"]
        threads = int(groups["threads"])
        push_ok = parse_number(groups["push_ok"], groups["push_ok_unit"])
        push_fail = parse_number(groups["push_fail"], groups["push_fail_unit"])
        pop_ok = parse_number(groups["pop_ok"], groups["pop_ok_unit"])
        pop_fail = parse_number(groups["pop_fail"], groups["pop_fail_unit"])
        data[(payload, impl, threads)] = {
            "ips": parse_number(groups["ips"], groups["ips_unit"]),
            "push_ok": push_ok,
            "push_fail": push_fail,
            "pop_ok": pop_ok,
            "pop_fail": pop_fail,
            "push_fail_ratio": push_fail / max(push_ok + push_fail, 1.0),
            "pop_fail_ratio": pop_fail / max(pop_ok + pop_fail, 1.0),
        }
    return data


def svg_escape(text: str) -> str:
    return (
        text.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


def polyline_points(xs, ys, x_map, y_map):
    return " ".join(f"{x_map(x):.1f},{y_map(y):.1f}" for x, y in zip(xs, ys))


def draw_panel(svg, x0, y0, w, h, title, xs, series, y_label, y_max, percent=False):
    pad_l, pad_r, pad_t, pad_b = 58, 18, 34, 40
    plot_x0 = x0 + pad_l
    plot_y0 = y0 + pad_t
    plot_w = w - pad_l - pad_r
    plot_h = h - pad_t - pad_b

    def x_map(x):
        return plot_x0 + (x - min(xs)) / max(xs[-1] - xs[0], 1) * plot_w

    def y_map(y):
        return plot_y0 + (1.0 - y / max(y_max, 1e-9)) * plot_h

    svg.append(f'<rect x="{x0}" y="{y0}" width="{w}" height="{h}" rx="18" fill="#fffdf9" stroke="#dfd8cf"/>')
    svg.append(
        f'<text x="{x0 + 18}" y="{y0 + 22}" font-size="15" font-weight="700" fill="#1d1d1d">{svg_escape(title)}</text>'
    )

    for i in range(5):
        y_val = y_max * i / 4.0
        py = y_map(y_val)
        label = f"{y_val:.0f}%" if percent else f"{y_val:.1f}"
        svg.append(f'<line x1="{plot_x0}" y1="{py:.1f}" x2="{plot_x0 + plot_w}" y2="{py:.1f}" stroke="#ece7df" stroke-dasharray="4 4"/>')
        svg.append(
            f'<text x="{plot_x0 - 10}" y="{py + 4:.1f}" text-anchor="end" font-size="11" fill="#6a655f">{label}</text>'
        )

    for x in xs:
        px = x_map(x)
        svg.append(f'<line x1="{px:.1f}" y1="{plot_y0}" x2="{px:.1f}" y2="{plot_y0 + plot_h}" stroke="#f3eee8"/>')
        svg.append(
            f'<text x="{px:.1f}" y="{plot_y0 + plot_h + 22}" text-anchor="middle" font-size="11" fill="#6a655f">{x}</text>'
        )

    svg.append(f'<line x1="{plot_x0}" y1="{plot_y0 + plot_h}" x2="{plot_x0 + plot_w}" y2="{plot_y0 + plot_h}" stroke="#c9c1b8"/>')
    svg.append(f'<line x1="{plot_x0}" y1="{plot_y0}" x2="{plot_x0}" y2="{plot_y0 + plot_h}" stroke="#c9c1b8"/>')
    svg.append(
        f'<text x="{x0 + w / 2:.1f}" y="{y0 + h - 8}" text-anchor="middle" font-size="11" fill="#6a655f">Threads</text>'
    )
    svg.append(
        f'<text x="{x0 + 16}" y="{y0 + h / 2:.1f}" transform="rotate(-90 {x0 + 16},{y0 + h / 2:.1f})" text-anchor="middle" font-size="11" fill="#6a655f">{svg_escape(y_label)}</text>'
    )

    for impl, values in series.items():
        points = polyline_points(xs, values, x_map, y_map)
        color = COLORS[impl]
        svg.append(
            f'<polyline fill="none" stroke="{color}" stroke-width="3" points="{points}" stroke-linecap="round" stroke-linejoin="round"/>'
        )
        for x, y in zip(xs, values):
            px, py = x_map(x), y_map(y)
            svg.append(f'<circle cx="{px:.1f}" cy="{py:.1f}" r="4.5" fill="{color}" stroke="#fffdf9" stroke-width="1.5"/>')


def plot(data, output_path: Path):
    payloads = ["Int", "Ptr", "Blob"]
    impls = ["LBQ", "LFQ", "LFN"]
    threads = [2, 4, 8, 16]
    panel_w = 470
    panel_h = 270
    gap_x = 24
    gap_y = 26
    margin = 26
    width = margin * 2 + panel_w * 3 + gap_x * 2
    height = 120 + panel_h * 2 + gap_y

    throughput_max = 0.0
    for payload in payloads:
        for impl in impls:
            for t in threads:
                item = data.get((payload, impl, t))
                if item:
                    throughput_max = max(throughput_max, item["ips"] / 1e6)

    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#f7f4ef"/>',
        '<text x="26" y="36" font-size="28" font-weight="800" fill="#1d1d1d">Queue Benchmark Summary</text>',
        '<text x="26" y="62" font-size="14" fill="#5f5a55">Top row: successful throughput. Bottom row: push failure ratio under contention.</text>',
    ]

    legend_x = width - 520
    legend_y = 34
    for idx, impl in enumerate(impls):
        x = legend_x + idx * 165
        svg.append(f'<line x1="{x}" y1="{legend_y}" x2="{x + 22}" y2="{legend_y}" stroke="{COLORS[impl]}" stroke-width="4" stroke-linecap="round"/>')
        svg.append(f'<circle cx="{x + 11}" cy="{legend_y}" r="4.5" fill="{COLORS[impl]}"/>')
        svg.append(
            f'<text x="{x + 30}" y="{legend_y + 5}" font-size="12" fill="#3e3a36">{svg_escape(impl + " / " + IMPL_LABEL[impl])}</text>'
        )

    for col, payload in enumerate(payloads):
        x0 = margin + col * (panel_w + gap_x)
        throughput_series = {}
        fail_series = {}
        for impl in impls:
            throughput_series[impl] = [data[(payload, impl, t)]["ips"] / 1e6 for t in threads]
            fail_series[impl] = [data[(payload, impl, t)]["push_fail_ratio"] * 100.0 for t in threads]

        draw_panel(
            svg,
            x0,
            86,
            panel_w,
            panel_h,
            f"{PAYLOAD_LABEL[payload]} Throughput",
            threads,
            throughput_series,
            "M items/s",
            max(throughput_max * 1.05, 1.0),
        )
        draw_panel(
            svg,
            x0,
            86 + panel_h + gap_y,
            panel_w,
            panel_h,
            f"{PAYLOAD_LABEL[payload]} Push Fail Ratio",
            threads,
            fail_series,
            "Fail %",
            100.0,
            percent=True,
        )

    svg.append("</svg>")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text("\n".join(svg), encoding="utf-8")


def main():
    if len(sys.argv) != 3:
        print("usage: plot_queue_bench.py <input.txt> <output.png>", file=sys.stderr)
        return 1
    input_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])
    data = parse_file(input_path)
    if not data:
        print("no benchmark rows parsed", file=sys.stderr)
        return 2
    plot(data, output_path)
    print(output_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
