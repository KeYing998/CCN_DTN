#!/usr/bin/env python3
# Plot grouped energy components and per-run cache quota changes from experiment CSV outputs.
"""Optional plots; install matplotlib first."""
import argparse
import csv
from pathlib import Path
import matplotlib
# Render image files without requiring a desktop display.
matplotlib.use("Agg")
import matplotlib.pyplot as plt

def rows(path):
    with path.open(newline="") as f:
        return list(csv.DictReader(f))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("results",type=Path)
    args=p.parse_args()
    root=args.results.resolve()
    data=rows(root/"comparison.csv")
    labels=[f"{r['mode']}\np={r['period_s']}\n{r['workload']}" for r in data]
    fig,ax=plt.subplots(figsize=(max(7,len(data)*1.1),4.5))
    # Stack communication, cache operations, and DT costs; omit the common node baseline.
    bottoms=[0.0]*len(data)
    for key,label in [("communication_j_mean","Business communication"),("cache_j_mean","Cache operations"),
                       ("dt_j_mean","DT overhead")]:
        values=[float(r[key]) for r in data]
        ax.bar(range(len(data)),values,bottom=bottoms,label=label)
        bottoms=[a+b for a,b in zip(bottoms,values)]
    ax.set_xticks(range(len(data)));ax.set_xticklabels(labels)
    ax.set_ylabel("Modelled energy (J), excluding common node baseline")
    ax.legend();fig.tight_layout();fig.savefig(root/"energy_breakdown.png",dpi=180);plt.close(fig)
    for folder in sorted(root.glob("*")):
        if not (folder/"allocations.csv").exists():continue
        data=rows(folder/"allocations.csv")
        fig,ax=plt.subplots(figsize=(8,4))
        # Each quota remains constant from its recorded update until the next change.
        for name in ["R1","R2","R3"]:
            ax.step([float(r['time_s']) for r in data],[int(r[name]) for r in data],where="post",label=name)
        ax.set_xlabel("Simulation time (s)");ax.set_ylabel("Active cache quota (Data entries)")
        ax.legend();fig.tight_layout();fig.savefig(folder/"cache_quotas.png",dpi=180);plt.close(fig)
    print("Plots created.")

if __name__=="__main__":main()
