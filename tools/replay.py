#!/usr/bin/env python3
"""Replay a gauge log through afr_core and compare with what was logged.

Feeds the log's v_out and led columns (and supply_v if fitted) through the
afr_replay program built from test/host, then compares the recomputed states,
warnings and peaks with the logged ones. Use it to check any change to the
core logic against real drives:

    cmake -S test/host -B build/host && cmake --build build/host
    python3 tools/replay.py /sdcard/logs/20261026_090507.csv

The log runs at 10-50 Hz while the gauge decides at 200 Hz, so a state or
warning may change a row or two earlier or later on replay. Differences
within --slack rows of a logged change are reported but not counted as
failures. Exit status is 1 if anything else differs.
"""
import argparse
import csv
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_BIN = os.path.join(HERE, "..", "build", "host", "afr_replay")


def read_log(path):
    meta, rows = {}, []
    with open(path, newline="") as f:
        lines = []
        for line in f:
            if line.startswith("#"):
                key, _, value = line[1:].strip().partition("=")
                meta[key.strip()] = value.strip()  # later lines win
            elif line.strip():
                lines.append(line)
    reader = csv.DictReader(lines)
    for r in reader:
        if r.get("t_ms", "").isdigit():
            rows.append(r)
    return meta, rows


def replay_args(meta):
    args = []
    rate = int(meta.get("log_rate_hz", "20").split()[0])
    args += ["--period-ms", str(1000 // rate)]
    ends = meta.get("lambda_end_points", "").split()[0] if meta.get("lambda_end_points") else ""
    if "," in ends:
        lam0, lam5 = ends.split(",")[:2]
        args += ["--lam0", lam0, "--lam5", lam5]
    fuel = meta.get("fuel", "").split()
    if len(fuel) >= 2:
        args += ["--stoich", fuel[1]]
    for key, flag in (("warn_lean", "--warn-lean"), ("warn_rich", "--warn-rich")):
        if meta.get(key):
            args += [flag, meta[key].split()[0]]
    if meta.get("smooth_ms"):
        args += ["--smooth-ms", meta["smooth_ms"].split()[0]]
    if meta.get("supply_sense") == "yes":
        args += ["--supply"]
    return args


def near_change(seq, i, slack):
    lo, hi = max(0, i - slack), min(len(seq) - 1, i + slack)
    return any(seq[j] != seq[i] for j in range(lo, hi + 1))


def logged_peaks(rows):
    lam = [float(r["lambda"]) for r in rows if r["state"] == "READY" and r["lambda"]]
    return (min(lam), max(lam)) if lam else (None, None)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--bin", default=DEFAULT_BIN, help="afr_replay program")
    ap.add_argument("--slack", type=int, default=2, help="rows of slack around logged changes")
    ap.add_argument("--peak-tol", type=float, default=0.002)
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()

    meta, rows = read_log(a.log)
    if not rows:
        sys.exit(f"{a.log}: no rows")
    feed = "".join(f"{r['t_ms']},{r['v_out']},{r['led']},{r.get('supply_v', '')}\n" for r in rows)
    cmd = [a.bin] + replay_args(meta)
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
            t, st, lam, warn = line.split(",")
            re_rows.append((st, warn))

    log_states = [r["state"] for r in rows]
    log_warns = [r["warn"] for r in rows]
    hard, soft = [], 0
    for i, (r, (st, warn)) in enumerate(zip(rows, re_rows)):
        for what, logged, got, seq in (("state", r["state"], st, log_states),
                                       ("warn", r["warn"], warn, log_warns)):
            if logged == got:
                continue
            if near_change(seq, i, a.slack):
                soft += 1
                if a.verbose:
                    print(f"t={r['t_ms']}: {what} {logged} -> {got} (at a change)")
            else:
                hard.append(f"t={r['t_ms']}: {what} logged {logged}, replay {got}")

    lp = logged_peaks(rows)
    peak_ok = True
    for name, l, g in zip(("rich", "lean"), lp, re_peaks):
        if (l is None) != (g is None) or (l is not None and abs(l - g) > a.peak_tol):
            peak_ok = False
            hard.append(f"peak {name}: logged {l}, replay {g}")

    dropped = sum(int(r.get("dropped") or 0) for r in rows)
    print(f"{len(rows)} rows, {len(set(log_states))} states seen, dropped rows {dropped}")
    print(f"peaks logged R {lp[0]} L {lp[1]}; replay R {re_peaks[0]} L {re_peaks[1]}"
          f" {'OK' if peak_ok else 'DIFFER'}")
    print(f"{soft} differences within {a.slack} rows of a change; {len(hard)} elsewhere")
    for h in hard[:20]:
        print("  " + h)
    sys.exit(1 if hard else 0)


if __name__ == "__main__":
    main()
