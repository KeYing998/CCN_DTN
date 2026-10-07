#!/usr/bin/env python3
# Install sources in ns-3 scratch and back up destination files whose contents differ.
"""Copy the experiment sources into ns-3, backing up previous versions."""
import argparse
from pathlib import Path
import shutil
from datetime import datetime

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("ns3", type=Path, help="ndnSIM ns-3 directory containing waf")
    args = p.parse_args()
    ns3 = args.ns3.expanduser().resolve()
    if not (ns3 / "waf").is_file() or not (ns3 / "src/ndnSIM").is_dir():
        p.error("Expected an existing ndnSIM ns-3 directory containing waf and src/ndnSIM")
    source = Path(__file__).resolve().parents[1] / "scratch"
    dest = ns3 / "scratch"
    dest.mkdir(exist_ok=True)
    files = sorted(source.glob("*"))
    # Waf's ns-3 scratch discovery accepts .cc, not .cpp.
    targets = {f: dest / (f.stem + ".cc" if f.suffix == ".cpp" else f.name) for f in files}
    # Compare bytes so identical destination files do not create unnecessary backups.
    different = []
    for f in files:
        target = targets[f]
        if target.exists() and target.read_bytes() != f.read_bytes():
            different.append(target)
    # Save all differing destination files before overwriting any experiment source.
    if different:
        backup = ns3 / ("ccn-dtn-backup-" + datetime.now().strftime("%Y%m%d-%H%M%S-%f"))
        backup.mkdir()
        for target in different:
            shutil.copy2(target, backup / target.name)
        print(f"Previous source files backed up in {backup}")
    for f in files:
        shutil.copy2(f, targets[f])
    print(f"Installed {len(files)} files in {dest}")

if __name__ == "__main__":
    main()
