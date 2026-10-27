#!/usr/bin/env python3
"""Replay a gauge log through afr_core and compare with what was logged.

Feeds the log's v_out and led (and supply_v if fitted) columns through the
afr_replay program built from test/host, then compares the recomputed
states, warnings and peaks with the logged ones. Use it to check any change to
the core logic against real sessions:

    cmake -S test/host -B build/host && cmake --build build/host
    python3 tools/replay.py /sdcard/logs/20261026_090507.csv

The gauge logs every processed sample (about 215 Hz), so replay sees what
the gauge saw. LED-driven changes can still land a row or two apart; any
difference within --slack rows of a logged change is reported but not
counted as a failure. Exit status is 1 if anything else differs.
"""
import argparse
import csv
import os
import statistics
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_BIN = os.path.join(HERE, "..", "build", "host", "afr_replay")


def read_log(path):
    meta, lines = {}, []
    with open(path, newline="") as f:
        for line in f:
            if line.startswith("#"):
                key, _, value = line[1:].strip().partition("=")
                meta[key.strip()] = value.strip()  # later lines win
            elif line.strip():
                lines.append(line)
    rows = [r for r in csv.DictReader(lines) if r.get("t_ms", "").isdigit()]
    return meta, rows


def first_word(v):
    return v.split()[0] if v else ""


def replay_args(meta, rows):
    dts = [int(b["t_ms"]) - int(a["t_ms"]) for a, b in zip(rows, rows[1:2000])]
    period = max(1, round(statistics.median(dts))) if dts else 5
    args = ["--period-ms", str(period)]
    ends = first_word(meta.get("lambda_end_points", ""))
    if "," in ends:
        lam0, lam5 = ends.split(",")[:2]
        args += ["--lam0", lam0, "--lam5", lam5]
    fuel = meta.get("fuel", "").split()
    if len(fuel) >= 2:
        args += ["--stoich", fuel[1]]
    for key in ("lean", "race_lean", "rich", "smooth_ms"):
        if meta.get(key):
            args += ["--" + key.replace("_", "-"), first_word(meta[key])]
    if meta.get("supply_sense") == "yes":
        args += ["--supply"]
    return args


def near_change(seq, i, slack):
    lo, hi = max(0, i - slack), min(len(seq) - 1, i + slack)
    return any(seq[j] != seq[i] for j in range(lo, hi + 1))


def logged_peaks(rows):
    """Richest and leanest READY lambda."""
    lam = [float(r["lambda"]) for r in rows if r["state"] == "READY" and r["lambda"]]
    return (min(lam), max(lam)) if lam else (None, None)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--bin", default=DEFAULT_BIN, help="afr_replay program")
    ap.add_argument("--slack", type=int, default=3, help="rows of slack around logged changes")
    ap.add_argument("--peak-tol", type=float, default=0.002)
    ap.add_argument("--expect-alarm", default="", help="fail unless the log holds these warning codes")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()

    meta, rows = read_log(a.log)
    if not rows:
        sys.exit(f"{a.log}: no rows")
    feed = "".join(f"{r['t_ms']},{r['v_out']},{r['led']},{r.get('supply_v', '')},{r.get('mode', 'S')}\n"
                   for r in rows)
    cmd = [a.bin] + replay_args(meta, rows)
    try:
        out = subprocess.run(cmd, input=feed, capture_output=True, text=True, check=True).stdout
    except FileNotFoundError:
        sys.exit(f"{a.bin} not found: build test/host first (see --help)")

    re_rows, re_peaks = [], (None, None)
    for line in out.splitlines():
        if line.startswith("# peaks"):
            parts = dict(p.split("=") for p in line.split()[2:])
            re_peaks = tuple(float(parts[k]) if parts[k] else None for k in ("rich", "lean"))
        else:
            _, st, _, alarm = line.split(",")
            re_rows.append((st, alarm))

    log_states = [r["state"] for r in rows]
    log_alarms = [r["alarm"] for r in rows]
    hard, soft = [], 0
    for i, (r, (st, alarm)) in enumerate(zip(rows, re_rows)):
        for what, logged, got, seq in (("state", r["state"], st, log_states),
                                       ("alarm", r["alarm"], alarm, log_alarms)):
            if logged == got:
                continue
            if near_change(seq, i, a.slack):
                soft += 1
                if a.verbose:
                    print(f"t={r['t_ms']}: {what} {logged} -> {got} (at a change)")
            else:
                hard.append(f"t={r['t_ms']}: {what} logged {logged}, replay {got}")

    lp = logged_peaks(rows)
    for name, l, g in zip(("rich", "lean"), lp, re_peaks):
        if (l is None) != (g is None) or (l is not None and abs(l - g) > a.peak_tol):
            hard.append(f"peak {name}: logged {l}, replay {g}")

    seen = sorted(set(log_alarms) - {"0"})
    for code in a.expect_alarm:
        if code not in seen:
            hard.append(f"expected warning {code} not in the log")

    dropped = sum(int(r.get("dropped") or 0) for r in rows)
    span = (int(rows[-1]["t_ms"]) - int(rows[0]["t_ms"])) / 1000.0
    print(f"{len(rows)} rows over {span:.1f} s ({len(rows) / max(span, 1e-9):.0f} Hz), dropped rows {dropped}")
    print(f"warnings in log: {' '.join(seen) or 'none'}")
    print(f"peaks: logged R {lp[0]} L {lp[1]}; replay R {re_peaks[0]} L {re_peaks[1]}")
    print(f"{soft} differences within {a.slack} rows of a change; {len(hard)} elsewhere")
    for h in hard[:20]:
        print("  " + h)
    sys.exit(1 if hard else 0)


if __name__ == "__main__":
    main()
