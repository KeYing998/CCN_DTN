#!/usr/bin/env python3
# Recalculate savings across DT overhead scales while keeping recorded traffic and decisions fixed.
"""Post-hoc sensitivity of DT overhead. Fixed observed traffic/decisions, not reoptimization."""
import argparse,csv
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('results',type=Path);p.add_argument('--scales',nargs='+',type=float,default=[0.25,0.5,1,2,4]);a=p.parse_args()
if any(x<0 for x in a.scales):p.error('scales must be nonnegative')
with (a.results/'all_summaries.csv').open() as f:rows=list(csv.DictReader(f))
# Match each treatment to the static run with the same workload and seed.
bases={(r['workload'],r['seed']):float(r['total_j']) for r in rows if r['mode']=='static'}
out=[]
for r in rows:
    if r['mode'] in {'static','uneven','manual'}:continue
    base=bases.get((r['workload'],r['seed']))
    if base is None:continue
    # Keep business communication, cache operations, and node baseline costs unchanged.
    non_dt=sum(float(r[k]) for k in ['communication_j','cache_j','base_j'])
    for scale in a.scales:
        # Scale the complete DT cost without rerunning the simulation or changing decisions.
        total=non_dt+scale*float(r['dt_j'])
        out.append(dict(directory=r['directory'],mode=r['mode'],workload=r['workload'],seed=r['seed'],period_s=r['period_s'],dt_overhead_scale=scale,recomputed_total_j=total,saved_j=base-total))
if not out:p.error('No treatments with paired static baselines')
with (a.results/'sensitivity.csv').open('w',newline='') as f:
    w=csv.DictWriter(f,fieldnames=list(out[0]));w.writeheader();w.writerows(out)
print('Created sensitivity.csv; fixed-decision sensitivity, not new simulations.')
