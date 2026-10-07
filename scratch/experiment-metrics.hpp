// Record outputs, reconcile traffic and delay counters, and calculate estimated experiment energy.
#pragma once

namespace dtn {
inline double Experiment::ControlEnergy(unsigned i) const {return stats[i].ctrlTx*energy.tx+stats[i].ctrlRx*energy.rx;
}

inline void Experiment::FinishOfficialTraffic() {
  // Print the remaining partial window exactly once. Periodic Print already
  // resets each complete window. Only Raw columns are used for energy.
  for(auto tracer:officialTracers)tracer->Print(*officialStream);
  officialStream->flush();
  if(!*officialStream)throw std::runtime_error("official L3 trace write failed");
  ns3::ndn::AppDelayTracer::Destroy();
  ns3::L2RateTracer::Destroy();
  std::ifstream delayInput(out+"/app-delay-trace.txt");
  officialDelays=ReadOfficialDelays(delayInput);
  std::set<std::pair<std::string,std::string>> faces;
  for(unsigned i=0;i<7;++i)for(auto id:networkFaces[i])
    faces.insert({Names::FindName(nodes[i]),std::to_string(id)});
  std::ifstream input(out+"/l3-rate-trace.txt");
  auto traffic=ReadOfficialTraffic(input,faces);
  std::ofstream audit(out+"/traffic-audit.csv");
  audit<<"node,event_tx_bytes,event_rx_bytes,official_tx_bytes,official_rx_bytes\n"<<std::setprecision(12);
  for(unsigned i=0;i<7;++i) {
    auto name=Names::FindName(nodes[i]);
    auto t=traffic.at(name);
    auto& s=stats[i];
    audit<<name<<','<<s.tx+s.ctrlTx<<','<<s.rx+s.ctrlRx<<','<<t.tx<<','<<t.rx<<'\n';
    if(std::abs(t.tx-s.tx-s.ctrlTx)>std::max(1.0,(s.tx+s.ctrlTx)*1e-8)||
       std::abs(t.rx-s.rx-s.ctrlRx)>std::max(1.0,(s.rx+s.ctrlRx)*1e-8))
      throw std::runtime_error("official/event traffic mismatch at "+name+"; see traffic-audit.csv");
    // Final namespace totals are verified against the official tracer output.
    // Official total audited against the namespace-separated event totals.
    // Official tracer cannot separate names; business/control split uses event trace.
  }
}

inline double Experiment::Communication(unsigned i) const {return stats[i].tx*energy.tx+stats[i].rx*energy.rx;
}

inline double Experiment::CacheEnergy(unsigned i) const {
  auto& s=stats[i];
  return (s.hits+s.misses)*energy.lookup+s.inserts*energy.insert+
    s.refreshes*energy.refresh+s.evictions*energy.evict;
}

// Timeline counters are cumulative; adjacent samples can be differenced for interval totals.
inline void Experiment::Log() {
  double t=Simulator::Now().GetSeconds();
  for(unsigned i=0;i<7;++i) {
    auto& s=stats[i];
    timeline<<t<<','<<Names::FindName(nodes[i])<<','<<Cs(i).getLimit()<<','<<Cs(i).size()<<','
      <<s.tx<<','<<s.rx<<','<<s.interests<<','<<s.hits<<','<<s.misses<<','<<s.inserts<<','
      <<s.refreshes<<','<<s.evictions<<','<<Communication(i)<<','<<CacheEnergy(i)<<','<<s.ctrlTx<<','<<s.ctrlRx<<'\n';
  }
  if(t+1<duration+2.01) Simulator::Schedule(Seconds(1),&Experiment::Log,this);
}

inline void Experiment::Finish() {
  uint64_t sent=0,received=0,timeouts=0,tx=0,rx=0,hits=0,misses=0;
  double delays=0,comm=0,cache=0;
  for(auto c:consumers) {sent+=c->sent;
  received+=c->received;
  timeouts+=c->timeouts;
  delays+=c->delaySum;
  }
  if(officialDelays.count!=received || std::abs(officialDelays.sum-delays)>std::max(1e-6,delays*1e-5))
    throw std::runtime_error("official AppDelayTracer does not match completed requests");
  delays=officialDelays.sum;
  for(unsigned i=0;i<7;++i) {tx+=stats[i].tx;
  rx+=stats[i].rx;
  comm+=Communication(i);
  cache+=CacheEnergy(i);
  }
  for(unsigned i=2;i<=4;++i) {hits+=stats[i].hits;
  misses+=stats[i].misses;
  }
  // Charge six business nodes for the full run, with D charged separately in DT modes.
  const double base=6*energy.base*(duration+2.01);
  const double controllerBase=HasDT()?energy.controllerBase*(duration+2.01):0;
  dtControl=0;
  for(unsigned i=0;i<7;++i)dtControl+=ControlEnergy(i);
  // DT overhead combines control traffic, report collection, model work, and controller baseline.
  const double dt=dtControl+dtCollection+dtCompute+controllerBase;
  const double total=comm+cache+base+dt;
  std::ofstream f(out+"/summary.csv");
   f<<std::setprecision(12);
  f<<"mode,workload,seed,period_s,requests,successes,timeouts,success_ratio,mean_delay_s,router_hit_ratio,tx_bytes,rx_bytes,communication_j,cache_j,base_j,dt_control_j,dt_collection_j,dt_compute_j,dt_base_j,dt_j,total_j,j_per_success,rounds,failed_rounds,model_operations,control_tx_bytes,control_rx_bytes,collection_reports,compute_rounds\n";
  f<<mode<<','<<workload<<','<<seed<<','<<period<<','<<sent<<','<<received<<','<<timeouts<<','
   <<(sent?double(received)/sent:0)<<','<<(received?delays/received:0)<<','
   <<(hits+misses?double(hits)/(hits+misses):0)<<','<<tx<<','<<rx<<','<<comm<<','<<cache<<','<<base<<','
   <<dtControl<<','<<dtCollection<<','<<dtCompute<<','<<controllerBase<<','<<dt<<','<<total<<',';
  // Energy per success is undefined when no request completes.
  if(received) f<<total/received;
   else f<<"nan";
  uint64_t ctx=0,crx=0;
  for(const auto&s:stats){ctx+=s.ctrlTx;
  crx+=s.ctrlRx;
  }
  f<<','<<rounds<<','<<failedRounds<<','<<modelOperations<<','<<ctx<<','<<crx<<','<<collectionReports<<','<<computeRounds<<'\n';
  if (!f) throw std::runtime_error("failed to write summary.csv");
  std::cout<<"mode="<<mode<<" success="<<received<<'/'<<sent<<" total_j="<<total
    <<" dt_j="<<dt<<" output="<<out<<std::endl;
}

inline void Experiment::OpenOutputs() {
  auto open=[&](std::ofstream& f,const std::string& name,const std::string& header){
    f.open(out+"/"+name);
     if(!f) throw std::runtime_error("create output directory first: "+out);
    f<<std::setprecision(12)<<header<<'\n';
  };
  open(timeline,"timeline.csv","time_s,node,capacity,occupancy,tx_bytes,rx_bytes,in_interests,hits,misses,inserts,refreshes,evictions,communication_j,cache_j,control_tx_bytes,control_rx_bytes");
  open(allocations,"allocations.csv","time_s,reason,R1,R2,R3");
  open(histories,"lo_history.csv","received_at_s,node,snapshot_at_s,requests,distinct_names,hits,misses,capacity,occupancy,predicted_rate,weight");
  open(controls,"control.csv","time_s,round,event,node,version,quota,detail");
  open(predictions,"predictions.csv","time_s,round,node,predicted_hit_ratio,observed_hit_ratio,absolute_error,valid_same_version");
  open(deliveries,"deliveries.csv","consumer,request_id,name,issued_at_s,received_at_s,delay_s,status");
  open(requestsFile,"requests.csv","consumer,request_id,time_s,content");
  metadata.open(out+"/parameters.txt");
  metadata<<std::setprecision(12)<<"mode="<<mode<<"\nworkload="<<workload<<"\nseed="<<seed
    <<"\ncatalog="<<catalog<<"\npayload="<<payload<<"\nduration="<<duration<<"\nswitchTime="<<switchTime
    <<"\nhigh="<<high<<"\nlow="<<low<<"\nzipf="<<zipf<<"\nperiod="<<period
    <<"\ncomputeDelay="<<computeDelay
    <<"\nalpha="<<alpha<<"\nbudget="<<budget<<"\nminimum="<<minimum<<"\nmaximum="<<maximum
    <<"\nenergyConfig="<<config<<"\nenergy_values=ILLUSTRATIVE_UNLESS_CALIBRATED\n"
    <<"traffic_source=namespace_event_trace_audited_against_official_L3RateTracer\ndelay_source=official_AppDelayTracer\n";
  std::ifstream configCopy(config);
   metadata<<"\n[energy config copy]\n"<<configCopy.rdbuf();
}

inline void Experiment::InstallTracers() {
  officialStream=std::make_shared<std::ofstream>(out+"/l3-rate-trace.txt");
  if(!*officialStream)throw std::runtime_error("cannot create official L3 trace");
  *officialStream<<std::setprecision(15);
  // Official per-node Install overload allows stream precision and an explicit
  // final partial-window Print, without modifying the tracer's implementation.
  std::ofstream facesFile(out+"/network-faces.csv");
  facesFile<<"node,face_id\n";
  for(unsigned i=0;i<7;++i) {
    auto tracer=ns3::ndn::L3RateTracer::Install(nodes[i],officialStream,Seconds(1));
    if(i==0){tracer->PrintHeader(*officialStream);
    *officialStream<<'\n';
    }
    officialTracers.push_back(tracer);
    for(auto id:networkFaces[i])facesFile<<Names::FindName(nodes[i])<<','<<id<<'\n';
  }
  ns3::ndn::AppDelayTracer::InstallAll(out+"/app-delay-trace.txt");
  ns3::L2RateTracer::InstallAll(out+"/l2-drop-trace.txt",Seconds(1));
}
} // namespace dtn
