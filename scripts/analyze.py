#!/usr/bin/env python3
# Check experiment records and produce grouped energy statistics and paired static-baseline comparisons.
"""Validate outputs, compare workloads, and summarize energy (stdlib only)."""
import argparse
import csv
import hashlib
import math
from pathlib import Path
import statistics

def read_rows(path):
    with path.open(newline="") as f:
        return list(csv.DictReader(f))

def validate(folder, row):
    n = int(row["requests"])
    ok, failed = int(row["successes"]), int(row["timeouts"])
    if n != ok + failed:
        raise ValueError(f"{folder}: unfinished requests or inconsistent counters")
    components = sum(float(row[k]) for k in ["communication_j", "cache_j", "base_j", "dt_j"])
    if not math.isclose(components, float(row["total_j"]), rel_tol=1e-8, abs_tol=1e-12):
        raise ValueError(f"{folder}: energy components do not sum to total")
    dt = sum(float(row[k]) for k in ["dt_control_j", "dt_collection_j", "dt_compute_j", "dt_base_j"])
    if not math.isclose(dt, float(row["dt_j"]), rel_tol=1e-8, abs_tol=1e-12):
        raise ValueError(f"{folder}: DT components are inconsistent")
    alloc = read_rows(folder / "allocations.csv")
    params = dict(line.split("=", 1) for line in (folder / "parameters.txt").read_text().splitlines()
                  if "=" in line and not line.startswith("#"))
    # Shrink-before-grow updates may leave spare budget temporarily but must never exceed it.
    for q in alloc:
        values = [int(q[k]) for k in ["R1", "R2", "R3"]]
        if sum(values) > int(params["budget"]) or any(
                not int(params["minimum"]) <= x <= int(params["maximum"]) for x in values):
            raise ValueError(f"{folder}: invalid cache quota")
    counts = read_rows(folder / "timeline.csv")
    if any(int(x["occupancy"]) > int(x["capacity"]) for x in counts):
        raise ValueError(f"{folder}: cache occupancy exceeds capacity")
    if ok and (int(row["tx_bytes"]) == 0 or int(row["rx_bytes"]) == 0):
        raise ValueError(f"{folder}: network traffic was not recorded")
    deliveries = read_rows(folder / "deliveries.csv")
    # Require exactly one terminal delivery record for every logical request.
    keys = [(x["consumer"], x["request_id"]) for x in deliveries]
    if len(keys) != n or len(set(keys)) != n or sum(x["status"] == "ok" for x in deliveries) != ok:
        raise ValueError(f"{folder}: delivery records are incomplete or duplicated")
    return hashlib.sha256((folder / "requests.csv").read_bytes()).hexdigest()

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("results", type=Path)
    args = p.parse_args()
    root = args.results.resolve()
    files = sorted(root.glob("*/summary.csv"))
    if not files:
        p.error("No */summary.csv found")
    records, hashes, pairs = [], {}, {}
    for file in files:
        rows = read_rows(file)
        if len(rows) != 1:
            raise ValueError(f"Expected one summary in {file}")
        row = rows[0]
        digest = validate(file.parent, row)
        # Paired modes must use identical request schedules for each workload and seed.
        key = (row["workload"], row["seed"])
        if key in hashes and hashes[key] != digest:
            raise ValueError(f"Different request schedules for workload/seed {key}")
        hashes[key] = digest
        row["directory"] = file.parent.name
        records.append(row)
        pairs[(row["workload"], row["seed"], row["mode"], row["period_s"])] = row
    with (root / "all_summaries.csv").open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(records[0])); w.writeheader(); w.writerows(records)
    groups = {}
    for row in records:
        groups.setdefault((row["workload"], row["mode"], row["period_s"]), []).append(row)
    metrics = ["communication_j", "cache_j", "dt_j", "total_j", "j_per_success", "success_ratio", "mean_delay_s"]
    table = []
    for (workload, mode, period), rows in sorted(groups.items()):
        entry = {"workload": workload, "mode": mode, "period_s": period, "runs": len(rows)}
        for metric in metrics:
            values = [float(x[metric]) for x in rows]
            entry[metric + "_mean"] = statistics.mean(values)
            entry[metric + "_sd"] = statistics.stdev(values) if len(values) > 1 else 0.0
        table.append(entry)
    with (root / "comparison.csv").open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=list(table[0])); w.writeheader(); w.writerows(table)
    lines = ["# Experiment validation", "", "Energy values follow the supplied coefficients; not hardware measurements.",
             "", "Request schedules, quotas, delivery counts and energy sums: PASS.", ""]
    baselines = {(r["workload"], r["seed"]): r for r in records if r["mode"] == "static"}
    lines.append("Noop uses real control traffic; business behavior may differ from static through queue competition.")
    lines.extend(["", "| Mode | Workload | Period s | Runs | Total J mean | DT J mean | J/success mean |", "|---|---|---:|---:|---:|---:|---:|"])
    for t in table:
        lines.append(f"| {t['mode']} | {t['workload']} | {t['period_s']} | {t['runs']} | "
                     f"{t['total_j_mean']:.6g} | {t['dt_j_mean']:.6g} | {t['j_per_success_mean']:.6g} |")
    paired=[]
    for (workload, mode, period), rows in sorted(groups.items()):
        if mode == "static": continue
        differences=[]; wins=0
        for row in rows:
            base=baselines.get((workload,row["seed"]))
            if base is None: continue
            # Positive savings mean lower energy than the same-seed static run.
            delta=float(base["total_j"])-float(row["total_j"])
            differences.append(delta); wins+=delta>0
        if not differences: continue
        n=len(differences); mean=statistics.mean(differences)
        critical={1:12.706,2:4.303,3:3.182,4:2.776,5:2.571,6:2.447,7:2.365,8:2.306,9:2.262,10:2.228,19:2.093,29:2.045}.get(n-1)
        if critical is None and n>1:
            # Conservative stepwise t critical; report this approximation.
            critical=next((v for df,v in sorted({1:12.706,2:4.303,3:3.182,4:2.776,5:2.571,6:2.447,7:2.365,8:2.306,9:2.262,10:2.228,19:2.093,29:2.045}.items(),reverse=True) if df<=n-1),12.706)
        # A single pair has no sample variance, so its confidence interval is undefined.
        margin=critical*statistics.stdev(differences)/math.sqrt(n) if n>1 else float("nan")
        paired.append(dict(workload=workload,mode=mode,period_s=period,runs=n,lower_energy_seeds=wins,mean_saved_j=mean,ci95_low_j=mean-margin,ci95_high_j=mean+margin))
    if paired:
        with (root/"paired.csv").open("w",newline="") as f:
            w=csv.DictWriter(f,fieldnames=list(paired[0]));w.writeheader();w.writerows(paired)
    lines.append("Paired intervals use t critical values (conservative stepwise approximation for unlisted sample sizes).")
    (root / "analysis.md").write_text("\n".join(lines) + "\n")
    print("Validated. Created all_summaries.csv, comparison.csv and analysis.md")

if __name__ == "__main__":
    main()
