#!/usr/bin/env python3
# Build and run groups across modes, update periods, and seeds, saving inputs and logs.
"""Build and run reproducible experiment groups through the existing waf."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import sys

def main():
    root = Path(__file__).resolve().parents[1]
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("ns3", type=Path)
    p.add_argument("--output", type=Path, default=root / "results")
    p.add_argument("--seeds", type=int, default=1)
    p.add_argument("--modes", nargs="+", choices=["static", "uneven", "heuristic", "shadow", "adaptive", "noop", "manual"],
                   default=["static", "uneven", "noop", "heuristic", "shadow", "adaptive"])
    p.add_argument("--workload", choices=["stable", "changing"], default="changing")
    p.add_argument("--periods", nargs="+", type=float, default=[5.0])
    p.add_argument("--duration", type=float, default=120)
    p.add_argument("--switch-time", type=float, default=60)
    p.add_argument("--energy-config", type=Path, default=root / "config/energy.conf")
    p.add_argument("--topology", type=Path, default=root / "topologies/tree.txt")
    p.add_argument("--adaptive-initial-period", type=float, default=5)
    p.add_argument("--min-period", type=float, default=1)
    p.add_argument("--max-period", type=float, default=20)
    p.add_argument("--horizon", type=float, default=30)
    p.add_argument("--dry-run", action="store_true")
    args = p.parse_args()
    ns3 = args.ns3.expanduser().resolve()
    if args.seeds < 1 or any(x <= 0 for x in args.periods):
        p.error("seeds and periods must be positive")
    if args.duration <= 0 or (args.workload == "changing" and not 0 < args.switch_time < args.duration):
        p.error("changing workload requires 0 < switch-time < duration")
    if len(args.periods) != len(set(args.periods)) or len(args.modes) != len(set(args.modes)):
        p.error("duplicate periods/modes")
    topology=args.topology.expanduser().resolve()
    if not topology.is_file(): p.error("topology does not exist")
    if not 0 < args.min_period <= args.max_period or not args.min_period <= args.adaptive_initial_period <= args.max_period or any(not args.min_period <= x <= args.max_period for x in args.periods): p.error("periods must be within min/max bounds")
    config = args.energy_config.expanduser().resolve()
    if not config.is_file():
        p.error("energy config does not exist")
    output = args.output.expanduser().resolve()
    jobs = []
    # Sweep fixed controller periods; run static modes and adaptive once per seed.
    for mode in args.modes:
        for period in (args.periods if mode in {"heuristic", "shadow", "noop"} else [args.adaptive_initial_period if mode=="adaptive" else args.periods[0]]):
            for seed in range(1, args.seeds + 1):
                path = output / f"{mode}_{args.workload}_p{period:g}_seed{seed}"
                if path.exists():
                    p.error(f"Result directory already exists: {path}. Choose a new --output.")
                jobs.append((mode, period, seed, path))
    # Preview jobs without installing sources, building, or creating output directories.
    if args.dry_run:
        print(json.dumps({"count": len(jobs), "jobs": [{"mode":m,"period":p,"seed":s,"output":str(d)} for m,p,s,d in jobs]}, indent=2))
        return
    subprocess.run([sys.executable, str(root / "scripts/install.py"), str(ns3)], check=True)
    subprocess.run([str(ns3 / "waf")], cwd=ns3, check=True)
    output.mkdir(parents=True, exist_ok=True)
    # Capture dependency revisions alongside copies of the topology and energy coefficients.
    versions = {}
    for name, path in {"ns3": ns3, "ndnSIM": ns3 / "src/ndnSIM",
                       "NFD": ns3 / "src/ndnSIM/NFD", "ndn-cxx": ns3 / "src/ndnSIM/ndn-cxx"}.items():
        result = subprocess.run(["git", "-C", str(path), "rev-parse", "HEAD"], capture_output=True, text=True)
        versions[name] = result.stdout.strip() if result.returncode == 0 else "unavailable"
    import hashlib, shutil
    shutil.copy2(topology, output / "topology.txt")
    shutil.copy2(config, output / "energy.conf")
    # This digest covers the entry file only; headers and scripts are not included.
    versions["scenario_sha256"]=hashlib.sha256((root/"scratch/ccn-dtn-v2.cpp").read_bytes()).hexdigest()
    (output / "source_versions.json").write_text(json.dumps(versions, indent=2) + "\n")
    for mode, period, seed, path in jobs:
        path.mkdir()
        values = ["ccn-dtn-v2", f"--topology={topology}", f"--minPeriod={args.min_period}", f"--maxPeriod={args.max_period}", f"--horizon={args.horizon}", f"--mode={mode}", f"--workload={args.workload}", f"--seed={seed}",
                  f"--period={period}", f"--duration={args.duration}", f"--switchTime={args.switch_time}",
                  f"--energyConfig={config}", f"--out={path}"]
        # Quote each scenario argument for the command string parsed by waf.
        spec = " ".join(shlex.quote(x) for x in values)
        print(f"Running {path.name}", flush=True)
        # Preserve per-run diagnostics and stop the batch on a scenario failure.
        with (path / "run.log").open("w") as log:
            subprocess.run([str(ns3 / "waf"), "--run", spec], cwd=ns3,
                           stdout=log, stderr=subprocess.STDOUT, check=True)
    print(f"Completed {len(jobs)} experiments. Analyze with:\n"
          f"python3 {root / 'scripts/analyze.py'} {output}")

if __name__ == "__main__":
    main()
