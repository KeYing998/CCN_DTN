// Update logical router models, select cache quotas, and adapt the controller update period.
#pragma once

namespace dtn {
void Experiment::Evaluate() {
  const double now=Simulator::Now().GetSeconds();
  if(now>=duration){EndRound(false);
  return;
  }
  std::array<double,3> w;
  double change=0;
  std::map<std::string,double> distribution;
  for(unsigned k=0;k<3;++k){const auto& b=twinBatch[k];
    if(now-b.sampled>commandTtl){EndRound(false);
    return;
    }
    const double rate=b.requests/b.interval,old=los[k].predictedRate;
    if(!los[k].history.empty())change=std::max(change,std::abs(rate-old)/std::max(1.,old));
    Snapshot s;
    s.time=b.sampled;
    s.interval=b.interval;
    s.requests=b.requests;
    s.hits=b.hits;
    s.misses=b.misses;
    s.capacity=b.capacity;
    s.occupancy=b.occupancy;
    for(const auto&e:b.events)++s.names[e.content];
    los[k].Update(s,alpha);
    w[k]=los[k].predictedRate*graph.RouterPath(k).size();
    histories<<now<<",R"<<k+1<<','<<b.sampled<<','<<b.requests<<','<<s.names.size()<<','<<b.hits<<','<<b.misses<<','<<b.capacity<<','<<b.occupancy<<','<<los[k].predictedRate<<','<<w[k]<<'\n';
    // Score prediction error only when the window used the expected quota version throughout.
    const bool valid=expectedHit[k]>=0&&b.version==expectedVersion[k]&&b.lastChange<=b.sampled-b.interval+1e-9;
    const double observed=b.hits+b.misses?double(b.hits)/(b.hits+b.misses):0;
    predictions<<now<<','<<rounds<<",R"<<k+1<<','<<expectedHit[k]<<','<<observed<<','<<(valid?std::abs(expectedHit[k]-observed):-1)<<','<<valid<<'\n';
    for(const auto&e:b.events){past.push_back(e);
    const unsigned rank=std::stoul(e.content.substr(9));
      // Track popular names separately and merge ranks 10 and above into an ingress-specific tail.
      ++distribution[std::to_string(e.ingress)+(rank<10?e.content:"/catalog/tail")];
      }
  }
  double count=0;
  for(const auto&kv:distribution)count+=kv.second;
  if(count>0){for(auto&kv:distribution)kv.second/=count;
    if(!previousDistribution.empty()){std::set<std::string> keys;
    for(const auto&kv:distribution)keys.insert(kv.first);
    for(const auto&kv:previousDistribution)keys.insert(kv.first);
      double tv=0;
      for(const auto&key:keys)tv+=std::abs(distribution[key]-previousDistribution[key]);
      // Combine relative rate change with total variation of the observed content distribution.
      change=std::max(change,tv/2);
      }
    previousDistribution=distribution;
  }
  std::sort(past.begin(),past.end(),[](const Event&a,const Event&b){return a.time<b.time||(a.time==b.time&&a.ingress<b.ingress);});
  // Bound replay input by the time horizon and a maximum of 12000 historical events.
  while(!past.empty()&&(past.front().time<now-horizon||past.size()>12000))past.pop_front();
  std::vector<Event> events(past.begin(),past.end());
  auto heuristic=Allocate(w,budget,minimum,maximum);
  selected=Choice{};
  selected.quota=heuristic;
  if(mode!="heuristic")selected=Choose(events,known,heuristic,graph,minimum,maximum,step,gainThreshold);
  if(mode=="noop")selected.quota={{50,50,50}};
  // Recovery after lost ACK: include a full-budget feasible target from fresh snapshots.
  if(known[0]+known[1]+known[2]!=budget)selected.quota=heuristic;
  // Count consecutive low-change rounds (at most 0.10) before lengthening the period.
  if(mode=="adaptive"){stableWindows=change<=0.10?stableWindows+1:0;
    nextPeriod=NextPeriod(nextPeriod,change,stableWindows,minPeriod,maxPeriod);
    }
  // Estimate replay work from candidates, events, and three routers rather than CPU measurements.
  const uint64_t ops=mode=="heuristic"?0:(selected.evaluated+2)*events.size()*3;
  modelOperations+=ops;
  ++computeRounds;
  dtCompute+=energy.compute+ops*energy.modelOp;
  // Warm caches over the first half of the history time span and score the second half.
  auto estimate=(events.empty()||mode=="heuristic")?Estimate{}:Replay(events,selected.quota,graph,events.front().time+(events.back().time-events.front().time)*0.5);
  for(unsigned k=0;k<3;++k){expectedHit[k]=estimate.lookups[k]?double(estimate.hits[k])/estimate.lookups[k]:-1;
    expectedVersion[k]=selected.quota[k]==known[k]?twinBatch[k].version:rounds;
    }
  controls<<now<<','<<rounds<<",model_evaluate,D,0,0,"<<selected.before<<"->"<<selected.after<<";candidates="<<selected.evaluated<<";events="<<events.size()<<'\n';
  commands=PlanChanges(known,selected.quota);
  commandIndex=0;
  // noop still sends three unchanged commands, so it includes control round trips.
  if(mode=="noop")for(unsigned k=0;k<3;++k)commands.emplace_back(k,50);
  NextCommand();
}
} // namespace dtn
