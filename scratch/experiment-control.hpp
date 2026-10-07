// Collect router snapshots and apply versioned cache commands with retries and acknowledgements.
#pragma once

namespace dtn {
std::string Experiment::PoServe(unsigned k,const ::ndn::Name& name) {
  const std::string op=name.at(3).toUri();
  auto& s=stats[k+2];
  if(op=="stats") {
    const std::string uri=name.getPrefix(5).toUri();
    const size_t segment=std::stoul(name.at(5).toUri());
    const uint64_t r=std::stoull(name.at(4).toUri());
    if(r<reportRound[k])return "ERROR obsolete round";
    // Freeze one snapshot per round so all segments and retries return consistent data.
    if(uri!=cachedReportName[k]){reportRound[k]=r;
    TwinSnapshot b;
    b.sampled=Simulator::Now().GetSeconds();
    b.interval=b.sampled-lastSample[k];
    b.capacity=Cs(k+2).getLimit();
    b.occupancy=Cs(k+2).size();
    b.version=poVersion[k];
    b.requests=s.windowRequests;
    b.hits=s.hits-previousHits[k];
    b.misses=s.misses-previousMisses[k];
    b.dropped=s.dropped;
    b.events.swap(s.events);
    b.lastChange=poChanged[k];
    previousHits[k]=s.hits;
    previousMisses[k]=s.misses;
    lastSample[k]=b.sampled;
    s.windowRequests=0;
    s.names.clear();
    s.dropped=0;
    dtCollection+=energy.collect;
    ++collectionReports;
    cachedReportName[k]=uri;
    cachedReport[k]=b.Encode();
    }
    // Split the serialized snapshot into payloads of at most 6000 bytes.
    const size_t chunks=(cachedReport[k].size()+5999)/6000;
    if(segment>=chunks)return "ERROR invalid segment";
    return std::to_string(segment)+" "+std::to_string(chunks)+"\n"+cachedReport[k].substr(segment*6000,6000);
  }
  if(op=="set") {
    const uint64_t v=std::stoull(name.at(4).toUri());
    const unsigned quota=std::stoul(name.at(5).toUri());
    const double issued=std::stod(name.at(6).toUri())/1000.;
    const double now=Simulator::Now().GetSeconds();
    // Apply only newer, unexpired commands; duplicates return the current state.
    if(v>poVersion[k]&&quota>=minimum&&quota<=maximum&&now-issued<=commandTtl&&issued<=now){
      if(quota!=Cs(k+2).getLimit())poChanged[k]=now;
      Cs(k+2).setLimit(quota);
      poVersion[k]=v;
      allocations<<now<<",rpc_apply,"<<Cs(2).getLimit()<<','<<Cs(3).getLimit()<<','<<Cs(4).getLimit()<<'\n';
      controls<<now<<','<<v<<",po_execute,R"<<k+1<<','<<v<<','<<quota<<",applied\n";
    }
    std::ostringstream ack;
    ack<<poVersion[k]<<' '<<Cs(k+2).getLimit()<<' '<<std::setprecision(17)<<now;
    return ack.str();
  }
  return "ERROR unknown operation";
}
void Experiment::BeginRound() {
  if(Simulator::Now().GetSeconds()>=duration)return;
  ++rounds;
  roundStarted=Simulator::Now().GetSeconds();
  collecting=true;
  rpcNode=0;
  reportBuffer.clear();
  reportSegment=0;
  controls<<roundStarted<<','<<rounds<<",round_start,D,0,0,"<<nextPeriod<<'\n';
  SendRpc("/dtn/po/R1/stats/"+std::to_string(rounds)+"/0");
}
void Experiment::SendRpc(const std::string& name) {
  pendingRpc=name;
  retry=0;
  rpcStarted=Simulator::Now().GetSeconds();
  ++rpcToken;
  controller->Send(name,static_cast<unsigned>(rpcToken*7919+seed));
  Simulator::Schedule(Seconds(0.5),&Experiment::RpcTimeout,this,rpcToken);
}
void Experiment::RpcTimeout(uint64_t token) {
  // Ignore timeout callbacks for an RPC that has already completed or changed.
  if(token!=rpcToken||pendingRpc.empty())return;
  // Retry twice after 0.5-second waits, keeping the name but changing the nonce.
  if(retry<2){++retry;
  controller->Send(pendingRpc,static_cast<unsigned>(rpcToken*7919+retry*101+seed));
    Simulator::Schedule(Seconds(0.5),&Experiment::RpcTimeout,this,token);
    return;
    }
  controls<<Simulator::Now().GetSeconds()<<','<<rounds<<",rpc_timeout,R"<<rpcNode+1<<",0,0,"<<pendingRpc<<'\n';
  pendingRpc.clear();
  ++rpcToken;
  EndRound(false);
}
void Experiment::GotRpc(const std::string& name,const std::string& text) {
  if(name!=pendingRpc)return;
  pendingRpc.clear();
  ++rpcToken;
  const double now=Simulator::Now().GetSeconds();
  controls<<now<<','<<rounds<<",rpc_data,R"<<rpcNode+1<<",0,0,"<<now-rpcStarted<<'\n';
  if(collecting){
    const auto newline=text.find('\n');
    unsigned segment=0,chunks=0;
    std::istringstream header(text.substr(0,newline));
    header>>segment>>chunks;
    if(newline==std::string::npos||!header||segment!=reportSegment||!chunks||chunks>128){EndRound(false);
    return;
    }
    reportBuffer+=text.substr(newline+1);
    if(++reportSegment<chunks){SendRpc("/dtn/po/R"+std::to_string(rpcNode+1)+"/stats/"+std::to_string(rounds)+"/"+std::to_string(reportSegment));
    return;
    }
    auto b=TwinSnapshot::Decode(reportBuffer);
    reportBuffer.clear();
    reportSegment=0;
    // Stale snapshots or dropped history events cannot support a complete decision round.
    if(now-b.sampled>commandTtl||b.dropped){EndRound(false);
    return;
    }
    twinBatch[rpcNode]=b;
    known[rpcNode]=b.capacity;
    if(++rpcNode<3){SendRpc("/dtn/po/R"+std::to_string(rpcNode+1)+"/stats/"+std::to_string(rounds)+"/0");
    return;
    }
    collecting=false;
    Simulator::Schedule(Seconds(computeDelay),&Experiment::Evaluate,this);
    return;
  }
  uint64_t v=0;
  unsigned quota=0;
  double applied=0;
  std::istringstream ack(text);
  ack>>v>>quota>>applied;
  // Advance only after the router confirms this round and the requested quota.
  if(!ack||v!=rounds||quota!=commands.at(commandIndex).second){EndRound(false);
  return;
  }
  known[rpcNode]=quota;
  controls<<now<<','<<rounds<<",ack,R"<<rpcNode+1<<','<<v<<','<<quota<<",confirmed\n";
  ++commandIndex;
  NextCommand();
}
void Experiment::NextCommand() {
  if(commandIndex==commands.size()){EndRound(true);
  return;
  }
  rpcNode=commands[commandIndex].first;
  const unsigned q=commands[commandIndex].second;
  // Check the acknowledged allocation before expanding a router cache.
  if(q>known[rpcNode]&&known[0]+known[1]+known[2]-known[rpcNode]+q>budget){EndRound(false);
  return;
  }
  const auto issued=static_cast<uint64_t>(Simulator::Now().GetSeconds()*1000);
  SendRpc("/dtn/po/R"+std::to_string(rpcNode+1)+"/set/"+std::to_string(rounds)+"/"+std::to_string(q)+"/"+std::to_string(issued));
}
void Experiment::EndRound(bool ok) {
  if(!ok)++failedRounds;
  controls<<Simulator::Now().GetSeconds()<<','<<rounds<<",round_end,D,0,0,"<<(ok?"ok":"failed")<<'\n';
  // Failed RPC may still be in flight: wait beyond command TTL before resynchronizing.
  double next=std::max(Simulator::Now().GetSeconds()+(ok?0.01:commandTtl+0.01),roundStarted+nextPeriod);
  if(next<duration)Simulator::Schedule(Seconds(next-Simulator::Now().GetSeconds()),&Experiment::BeginRound,this);
}
} // namespace dtn
