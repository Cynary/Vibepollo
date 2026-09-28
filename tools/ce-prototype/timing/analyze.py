"""Analyze the controlled frame-ID experiment, not arbitrary game frame identity.

Usage: python analyze.py RUN_DIRECTORY
Windows timestamps use the QPC frequency written by frame_probe. WGC source times
are scheduled 100 ns values, not capture-completion times. They are never used to
match frames. Readback IDs validate the compositor-selection join; clean runs use
that validated fixture-specific join and must be reported as such.
"""
import bisect
import csv
import json
import re
import statistics
import sys
from pathlib import Path


def summary(values):
    values = sorted(values)
    if not values:
        return {"n": 0}
    return {"n": len(values), "mean_ms": statistics.mean(values),
            "p50_ms": statistics.median(values),
            "p99_ms": values[min(len(values)-1, int(len(values)*.99))],
            "max_ms": values[-1], "min_ms": values[0]}


def containing_frame(starts, rows, time):
    index = bisect.bisect_right(starts, time)-1
    if index < 0 or time > int(rows[index]["end_qpc"]):
        return None
    return int(rows[index]["id"])


def analyze(root):
    root = Path(root)
    meta = dict(re.findall(r"(\w+)=(\d+)", (root/"frame-probe-result.txt").read_text()))
    frequency = int(meta["frequency"])
    pid = meta.get("pid")
    rows = list(csv.DictReader((root/"frame-probe-present.csv").open()))
    captures = list(csv.DictReader((root/"frame-probe-wgc.csv").open()))
    starts = [int(r["begin_qpc"]) for r in rows]
    frames = {int(r["id"]): r for r in rows}
    # The controlled fixture guarantees these two identities coincide. Reject
    # evidence where that contract failed instead of shifting indices to fit.
    if any(r["id"] != r["present_count"] for r in rows):
        raise ValueError("fixture frame-ID / present-count mismatch")
    native = {}
    surfaces = set()
    selections = []
    ready = {}
    tokens = {}
    for line in (root/"events.csv").open():
        c = line.rstrip().split(",")
        time, process, thread, provider, event = int(c[0]), c[1], c[2], c[3], c[4]
        fields = dict(part.split("=", 1) for part in c[6:] if "=" in part)
        own = process == pid
        frame = containing_frame(starts, rows, time) if own else None
        if own and provider == "ca11c036" and event == "42" and frame:
            native.setdefault(frame, []).append(time)
        if own and provider == "8c416c79" and event == "201":
            surfaces.add((fields.get("CompositionSurfaceLuid"), fields.get("BindId")))
        if provider == "9e9bba3c" and event == "467":
            selections.append((time, fields.get("surfaceLuid"), fields.get("bindId"),
                               int(fields.get("presentCount", "0"))))
        if provider == "802ec45a" and event in ("171", "215"):
            key = fields.get("Token")
            candidate = frame if own and fields.get("Model") == "2" else None
            tokens[key] = candidate
        if provider == "802ec45a" and event == "172":
            candidate = tokens.pop(fields.get("Token"), None)
            if candidate:
                ready.setdefault(candidate, time)
    selections = sorted(x for x in selections if (x[1], x[2]) in surfaces)
    times = [x[0] for x in selections]
    # Use a fixed time window, not the first N packets or a hand-picked tail.
    begin, end = starts[0]+frequency*3, starts[-1]-frequency
    measures = {name: [] for name in ("outer_present_to_callback", "outer_to_dxgi",
        "dxgi_to_callback", "dxgi_to_ready_handoff", "handoff_to_selection",
        "selection_to_callback", "callback_to_readback", "source_minus_callback")}
    matched = invalid = ambiguous = wrong = checked = duplicate = 0
    previous_selection = None
    for r in captures:
        callback = int(r["entry_qpc"])
        if not begin <= callback <= end:
            continue
        i = bisect.bisect_right(times, callback)-1
        if i < 0:
            invalid += 1
            continue
        selected, _, _, frame = selections[i]
        if previous_selection == i:
            duplicate += 1
            continue
        previous_selection = i
        if meta.get("validate") == "1":
            if r["valid"] != "1":
                invalid += 1
                continue
            checked += 1
            if int(r["id"]) != frame:
                wrong += 1
                continue
        if frame not in frames or len(native.get(frame, [])) != 1:
            ambiguous += 1
            continue
        dxgi = native[frame][0]
        if not int(frames[frame]["begin_qpc"]) <= dxgi <= selected <= callback:
            invalid += 1
            continue
        matched += 1
        scale = 1000/frequency
        measures["outer_present_to_callback"].append((callback-int(frames[frame]["begin_qpc"]))*scale)
        measures["outer_to_dxgi"].append((dxgi-int(frames[frame]["begin_qpc"]))*scale)
        measures["dxgi_to_callback"].append((callback-dxgi)*scale)
        measures["selection_to_callback"].append((callback-selected)*scale)
        if dxgi <= ready.get(frame, 0) <= selected:
            measures["dxgi_to_ready_handoff"].append((ready[frame]-dxgi)*scale)
            measures["handoff_to_selection"].append((selected-ready[frame])*scale)
        if int(r["readback_qpc"]):
            measures["callback_to_readback"].append((int(r["readback_qpc"])-callback)*scale)
        measures["source_minus_callback"].append(int(r["source_100ns"])/10000-callback*scale)
    return {"mode": "pixel-validated" if meta.get("validate") == "1" else "fixture-validated selection join; no readback",
            "matched": matched, "identity_checked": checked, "identity_mismatch": wrong,
            "invalid": invalid, "ambiguous": ambiguous, "duplicate_selection": duplicate,
            "metrics": {k: summary(v) for k, v in measures.items()}}


if __name__ == "__main__":
    print(json.dumps(analyze(sys.argv[1]), indent=2))
