#!/usr/bin/env python3

import json
import re
import sys
from pathlib import Path


NAME_RE = re.compile(
    r"^BM_ReportPipelineTopologyCompare_"
    r"(?P<topology>Linear|Diamond)"
    r"(?P<payload>Timestamp|Payload1KiB)_"
    r"(?P<scenario>Blocking|BlockingJoin)"
    r"/(?P<param>\d+)$"
)


def load_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def fmt_float(value, digits=3):
    if value is None:
        return "TBD"
    return f"{value:.{digits}f}"


def fmt_intish(value):
    if value is None:
        return "TBD"
    if abs(value - round(value)) < 1e-9:
        return str(int(round(value)))
    return f"{value:.3f}"


def benchmark_key(entry):
    match = NAME_RE.match(entry["name"])
    if not match:
        return None
    groups = match.groupdict()
    topology = "Linear" if groups["topology"] == "Linear" else "DiamondJoin"
    payload = "Timestamp" if groups["payload"] == "Timestamp" else "Payload1KiB"
    param_name = "depth" if topology == "Linear" else "branches"
    return {
        "topology": topology,
        "payload": payload,
        "parameter": f"{param_name} = {groups['param']}",
        "param_value": int(groups["param"]),
    }


def parse_entries(doc):
    rows = []
    for entry in doc.get("benchmarks", []):
        if entry.get("run_type") != "iteration":
            continue
        meta = benchmark_key(entry)
        if meta is None:
            continue
        counters = entry.get("counters", {})
        rows.append(
            {
                **meta,
                "ElapsedUs": counters.get("ElapsedUs"),
                "PerMessageElapsedUs": counters.get("PerMessageElapsedUs"),
                "Throughput": counters.get("Throughput"),
                "PortEnqueued": counters.get("PortEnqueued"),
                "PortDequeued": counters.get("PortDequeued"),
                "MaxPortPeakDepth": counters.get("MaxPortPeakDepth"),
                "DeliveryTimedOut": counters.get("DeliveryTimedOut"),
                "DrainTimedOut": counters.get("DrainTimedOut"),
                "EffectivePayloadMiBps": counters.get("EffectivePayloadMiBps"),
            }
        )
    return rows


def sort_rows(rows):
    topology_order = {"Linear": 0, "DiamondJoin": 1}
    payload_order = {"Timestamp": 0, "Payload1KiB": 1}
    return sorted(rows, key=lambda row: (payload_order[row["payload"]], topology_order[row["topology"]], row["param_value"]))


def render_timestamp_table(rows):
    lines = [
        "| Topology | Parameter | Elapsed time | Avg elapsed / msg | Throughput | Port enqueued | Port dequeued | Peak depth | Delivery timeout | Drain timeout |",
        "|----------|-----------|--------------|-------------------|------------|---------------|---------------|------------|------------------|---------------|",
    ]
    for row in rows:
        if row["payload"] != "Timestamp":
            continue
        lines.append(
            "| {topology} | {parameter} | {elapsed} us | {per_msg} us | {throughput} | {enqueued} | {dequeued} | {peak} | {delivery} | {drain} |".format(
                topology=row["topology"],
                parameter=row["parameter"],
                elapsed=fmt_float(row["ElapsedUs"]),
                per_msg=fmt_float(row["PerMessageElapsedUs"]),
                throughput=fmt_float(row["Throughput"]),
                enqueued=fmt_intish(row["PortEnqueued"]),
                dequeued=fmt_intish(row["PortDequeued"]),
                peak=fmt_intish(row["MaxPortPeakDepth"]),
                delivery=fmt_intish(row["DeliveryTimedOut"]),
                drain=fmt_intish(row["DrainTimedOut"]),
            )
        )
    return "\n".join(lines)


def render_payload_table(rows):
    lines = [
        "| Topology | Parameter | Elapsed time | Avg elapsed / msg | Throughput | Effective payload rate | Port enqueued | Port dequeued | Delivery timeout | Drain timeout |",
        "|----------|-----------|--------------|-------------------|------------|------------------------|---------------|---------------|------------------|---------------|",
    ]
    for row in rows:
        if row["payload"] != "Payload1KiB":
            continue
        lines.append(
            "| {topology} | {parameter} | {elapsed} us | {per_msg} us | {throughput} | {payload_rate} MiB/s | {enqueued} | {dequeued} | {delivery} | {drain} |".format(
                topology=row["topology"],
                parameter=row["parameter"],
                elapsed=fmt_float(row["ElapsedUs"]),
                per_msg=fmt_float(row["PerMessageElapsedUs"]),
                throughput=fmt_float(row["Throughput"]),
                payload_rate=fmt_float(row["EffectivePayloadMiBps"]),
                enqueued=fmt_intish(row["PortEnqueued"]),
                dequeued=fmt_intish(row["PortDequeued"]),
                delivery=fmt_intish(row["DeliveryTimedOut"]),
                drain=fmt_intish(row["DrainTimedOut"]),
            )
        )
    return "\n".join(lines)


def render(rows):
    ordered = sort_rows(rows)
    sections = [
        "#### 4.10.1 Timestamp 拓扑对照表",
        "",
        render_timestamp_table(ordered),
        "",
        "#### 4.10.2 1 KiB Shared Payload 拓扑对照表",
        "",
        render_payload_table(ordered),
    ]
    return "\n".join(sections) + "\n"


def main():
    if len(sys.argv) < 3:
        print("usage: render_topology_compare_tables.py <benchmark-a.json> [benchmark-b.json ...] <output.md>", file=sys.stderr)
        return 1

    input_paths = [Path(arg) for arg in sys.argv[1:-1]]
    output_path = Path(sys.argv[-1])

    rows = []
    for input_path in input_paths:
        doc = load_json(input_path)
        rows.extend(parse_entries(doc))
    if not rows:
        print("no topology-compare benchmark rows found", file=sys.stderr)
        return 2

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(render(rows), encoding="utf-8")
    print(output_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
