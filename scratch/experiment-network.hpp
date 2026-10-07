// Build the NDN topology, connect traffic counters, install applications, and schedule the experiment.
#pragma once

namespace dtn {
// Free callbacks make binding the node index explicit and type-safe.
static Experiment* active=nullptr;
void InterestIn(unsigned i,const ::ndn::Interest& p,const nfd::Face& f){active->InterestIn(i,p,f);
 }
void InterestOut(unsigned i,const ::ndn::Interest& p,const nfd::Face& f){active->InterestOut(i,p,f);
 }
void DataIn(unsigned i,const ::ndn::Data& p,const nfd::Face& f){active->DataIn(i,p,f);
 }
void DataOut(unsigned i,const ::ndn::Data& p,const nfd::Face& f){active->DataOut(i,p,f);
 }
void NackIn(unsigned i,const ::ndn::lp::Nack& p,const nfd::Face& f){active->NackIn(i,p,f);
 }
void NackOut(unsigned i,const ::ndn::lp::Nack& p,const nfd::Face& f){active->NackOut(i,p,f);
 }


inline nfd::cs::Cs& Experiment::Cs(unsigned i) {
  return nodes[i]->GetObject<ns3::ndn::L3Protocol>()->getForwarder()->getCs();
}

inline bool Experiment::IsNetwork(unsigned node, const nfd::Face& face) const {
  const auto& ids=networkFaces[node];
  return std::find(ids.begin(),ids.end(),face.getId())!=ids.end();
}

inline void Experiment::InterestIn(unsigned i,const ::ndn::Interest& p,const nfd::Face& f) {
  if (!IsNetwork(i,f)) return;
  if(p.getName().toUri().find("/dtn/")==0){stats[i].ctrlRx+=p.wireEncode().size();
  return;
  }
  stats[i].rx+=p.wireEncode().size();
   ++stats[i].interests;
  // Record replay events only on consumer-facing ingress links to avoid counting upstream copies.
  if (i>=2&&i<=4&&p.getName().toUri().find("/catalog/")==0) {++stats[i].windowRequests;
   ++stats[i].names[p.getName().toUri()];
    if((i==2||i==3)&&f.getId()==ingressFaces[i-2]){if(stats[i].events.size()<8192)stats[i].events.push_back({Simulator::Now().GetSeconds(),i-2,p.getName().toUri()});
    else ++stats[i].dropped;
    }}
}

inline void Experiment::InterestOut(unsigned i,const ::ndn::Interest& p,const nfd::Face& f) {
  if (IsNetwork(i,f)){if(p.getName().toUri().find("/dtn/")==0)stats[i].ctrlTx+=p.wireEncode().size();
  else stats[i].tx+=p.wireEncode().size();
  }
}

inline void Experiment::DataIn(unsigned i,const ::ndn::Data& p,const nfd::Face& f) {
  if (IsNetwork(i,f)){if(p.getName().toUri().find("/dtn/")==0)stats[i].ctrlRx+=p.wireEncode().size();
  else stats[i].rx+=p.wireEncode().size();
  }
}

inline void Experiment::DataOut(unsigned i,const ::ndn::Data& p,const nfd::Face& f) {
  if (IsNetwork(i,f)){if(p.getName().toUri().find("/dtn/")==0)stats[i].ctrlTx+=p.wireEncode().size();
  else stats[i].tx+=p.wireEncode().size();
  }
}

// Account for Nacks using the enclosed Interest size, matching the traffic tracer convention.
inline void Experiment::NackIn(unsigned i,const ::ndn::lp::Nack& p,const nfd::Face& f) {
  if (IsNetwork(i,f)){if(p.getInterest().getName().toUri().find("/dtn/")==0)stats[i].ctrlRx+=p.getInterest().wireEncode().size();
  else stats[i].rx+=p.getInterest().wireEncode().size();
  }
}

inline void Experiment::NackOut(unsigned i,const ::ndn::lp::Nack& p,const nfd::Face& f) {
  if (IsNetwork(i,f)){if(p.getInterest().getName().toUri().find("/dtn/")==0)stats[i].ctrlTx+=p.getInterest().wireEncode().size();
  else stats[i].tx+=p.getInterest().wireEncode().size();
  }
}

inline void Experiment::Apply(Quotas q,const std::string& reason) {
  if (q[0]+q[1]+q[2]!=budget) throw std::logic_error("budget changed");
  for (auto x:q) if(x<minimum||x>maximum) throw std::logic_error("quota outside bounds");
  // Shrink first: the active budget is never transiently exceeded.
  for(unsigned k=0;k<3;++k) if(q[k]<Cs(k+2).getLimit()) Cs(k+2).setLimit(q[k]);
  for(unsigned k=0;k<3;++k) if(q[k]>Cs(k+2).getLimit()) Cs(k+2).setLimit(q[k]);
  allocations<<Simulator::Now().GetSeconds()<<','<<reason<<','<<q[0]<<','<<q[1]<<','<<q[2]<<'\n';
}

inline void Experiment::BuildNetwork() {
  // Official reader creates links, mobility positions and Names registrations.
  // No second TXT parser and no manually constructed network nodes/links.
  AnnotatedTopologyReader topologyReader("", 1.0);
  topologyReader.SetFileName(topology);
  const NodeContainer topologyNodes=topologyReader.Read();
  if(topologyNodes.GetN()!=7)throw std::runtime_error("v2 requires exactly seven named nodes");
  const char* names[]={"C1","C2","R1","R2","R3","P","D"};
  std::map<std::string,std::string> roles;
  for(unsigned i=0;i<7;++i){
    nodes[i]=Names::Find<Node>(names[i]);
    if(!nodes[i])throw std::runtime_error("missing required topology node: "+std::string(names[i]));
    roles[names[i]]=i<2?"consumer":i<5?"router":i==5?"producer":"digital";
  }
  std::vector<Edge> modelEdges;
  for(const auto& link:topologyReader.GetLinks()){
    Edge edge;
    edge.a=Names::FindName(link.GetFromNode());
    edge.b=Names::FindName(link.GetToNode());
    const std::string metric=link.GetAttribute("OSPF");
    size_t used=0;
    const unsigned long parsed=std::stoul(metric,&used);
    if(used!=metric.size()||!parsed||parsed>65535)throw std::runtime_error("OSPF metric must be 1..65535");
    edge.metric=static_cast<unsigned>(parsed);
    edge.rate=link.GetAttribute("DataRate");
    std::string delay;
    if(link.GetAttributeFailSafe("Delay",delay))edge.delay=delay;
    modelEdges.push_back(edge);
  }
  // LO model gets the SAME graph the official reader constructed.
  graph=Topology::FromGraph(roles,modelEdges);
  std::ifstream topoCopy(topology);
  std::ofstream topoOut(out+"/topology.txt");
  topoOut<<topoCopy.rdbuf();
  metadata<<"\ntopology="<<topology<<"\ntopology_loader=AnnotatedTopologyReader\nmodel=joint_LRU_replay\nstep="<<step<<"\ngainThreshold="<<gainThreshold
    <<"\nhorizon="<<horizon<<"\nminPeriod="<<minPeriod<<"\nmaxPeriod="<<maxPeriod<<"\n";
  ns3::ndn::StackHelper stack;
   stack.setCsSize(50);
   stack.setPolicy("nfd::cs::lru");
  stack.InstallAll();
  topologyReader.ApplyOspfMetric(); // Requires the NDN stack/faces to exist.
  for(unsigned i=0;i<7;++i) {
    auto l3=nodes[i]->GetObject<ns3::ndn::L3Protocol>();
    auto forwarder=l3->getForwarder();
    // Only R1, R2, and R3 participate in business caching.
    if(i<2||i>=5) {Cs(i).enableAdmit(false);
    Cs(i).enableServe(false);
    Cs(i).setLimit(1);
    }
    else {
      std::unique_ptr<MeasuredLru> policy(new MeasuredLru());
      policy->inserted=[this,i](const std::string&){++stats[i].inserts;
      };
      policy->refreshed=[this,i](const std::string&){++stats[i].refreshes;
      };
      policy->evicted=[this,i](const std::string&){++stats[i].evictions;
      };
      Cs(i).setPolicy(std::move(policy));
      forwarder->afterCsHit.connect([this,i](const ::ndn::Interest& p,const ::ndn::Data&){if(p.getName().toUri().find("/catalog/")==0)++stats[i].hits;});
      forwarder->afterCsMiss.connect([this,i](const ::ndn::Interest& p){if(p.getName().toUri().find("/catalog/")==0)++stats[i].misses;});
    }
    // Identify physical faces to exclude local application traffic from link byte totals.
    for(unsigned j=0;j<nodes[i]->GetNDevices();++j) {
      auto face=l3->getFaceByNetDevice(nodes[i]->GetDevice(j));
      if(face) {
        networkFaces[i].push_back(face->getId());
        if(i==2||i==3){auto channel=nodes[i]->GetDevice(j)->GetChannel();
          for(unsigned d=0;d<channel->GetNDevices();++d)
            if(channel->GetDevice(d)->GetNode()==nodes[i-2])ingressFaces[i-2]=face->getId();
            }
      }
    }
    if(networkFaces[i].empty()) throw std::runtime_error("no network face found");
    bool connected=true;
    connected &= l3->TraceConnectWithoutContext("InInterests",MakeBoundCallback(&dtn::InterestIn,i));
    connected &= l3->TraceConnectWithoutContext("OutInterests",MakeBoundCallback(&dtn::InterestOut,i));
    connected &= l3->TraceConnectWithoutContext("InData",MakeBoundCallback(&dtn::DataIn,i));
    connected &= l3->TraceConnectWithoutContext("OutData",MakeBoundCallback(&dtn::DataOut,i));
    connected &= l3->TraceConnectWithoutContext("InNack",MakeBoundCallback(&dtn::NackIn,i));
    connected &= l3->TraceConnectWithoutContext("OutNack",MakeBoundCallback(&dtn::NackOut,i));
    if(!connected) throw std::runtime_error("NDN trace source connection failed");
  }
  ns3::ndn::StrategyChoiceHelper::InstallAll("/","/localhost/nfd/strategy/best-route");
  // Deterministic single next hops from TXT metrics; replay uses the same paths.
  auto route=[&](const std::string& prefix,const std::string& destination){
    for(const auto& kv:graph.roles)if(kv.first!=destination){auto path=graph.Path(kv.first,destination);
      ns3::ndn::FibHelper::AddRoute(Names::Find<Node>(kv.first),::ndn::Name(prefix),Names::Find<Node>(path[1]),1);
      }
  };
  route("/catalog","P");
  if(HasDT()){
    for(unsigned k=0;k<3;++k){const std::string node="R"+std::to_string(k+1),prefix="/dtn/po/"+node;
      route(prefix,node);
      auto app=CreateObject<ControlApp>();
      app->prefix=prefix;
      app->serve=[this,k](const ::ndn::Name& name){return PoServe(k,name);
      };
      nodes[k+2]->AddApplication(app);
      app->SetStartTime(Seconds(0));
      app->SetStopTime(Seconds(duration+2.005));
      poApps[k]=app;
    }
    controller=CreateObject<ControlApp>();
    controller->delivered=[this](const std::string&n,const std::string&s){GotRpc(n,s);
    };
    nodes[6]->AddApplication(controller);
    controller->SetStartTime(Seconds(0));
    controller->SetStopTime(Seconds(duration+2.005));
  }
}

inline void Experiment::InstallBusiness() {
  ns3::ndn::AppHelper producer("ns3::ndn::Producer");
  producer.SetPrefix("/catalog");
  producer.SetAttribute("PayloadSize",UintegerValue(payload));
  producer.SetAttribute("Freshness",TimeValue(Seconds(duration+10)));
  producer.Install(nodes[5]);
  for(unsigned i=0;i<2;++i) {
    auto app=CreateObject<ReplayConsumer>();
    app->consumer=i;
    app->requests=MakeRequests(i,seed,catalog,duration,switchTime,high,low,zipf,workload=="changing");
    for(size_t j=0;j<app->requests.size();++j) requestsFile<<i<<','<<j<<','<<app->requests[j].time<<','<<app->requests[j].content<<'\n';
    app->result=[this](unsigned c,uint64_t id,const std::string& name,double issued,double received){
      deliveries<<c<<','<<id<<','<<name<<','<<issued<<','<<received<<','
        <<(received<0?-1:received-issued)<<','<<(received<0?"timeout":"ok")<<'\n';
    };
    nodes[i]->AddApplication(app);
     app->SetStartTime(Seconds(0));
    app->SetStopTime(Seconds(duration+2.005));
    consumers[i]=app;
  }
}

inline void Experiment::ScheduleExperiment() {
  Apply(mode=="uneven"?Quotas{{80,20,50}}:Quotas{{50,50,50}},"initial");
  if(HasDT()) Simulator::Schedule(Seconds(period),&Experiment::BeginRound,this);
  if(mode=="manual") {
    if(duration>30) Simulator::Schedule(Seconds(30),&Experiment::Apply,this,Quotas{{80,20,50}},std::string("manual"));
    if(duration>60) Simulator::Schedule(Seconds(60),&Experiment::Apply,this,Quotas{{20,80,50}},std::string("manual"));
  }
  Log();
}

// Create outputs and network state before applications, tracers, and initial events use them.
inline void Experiment::Setup() {
  Validate();
  energy.Load(config);
  nextPeriod=period;
  active=this;
  OpenOutputs();
  BuildNetwork();
  InstallBusiness();
  InstallTracers();
  ScheduleExperiment();
}
} // namespace dtn
