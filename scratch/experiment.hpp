// Declare shared experiment state and coordinate network setup, control, decisions, and metrics.
#pragma once
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/ndnSIM-module.h"
#include "ns3/ndnSIM/utils/topology/annotated-topology-reader.hpp"
#include "ns3/ndnSIM/apps/ndn-app.hpp"
#include "ns3/ndnSIM/model/ndn-app-link-service.hpp"
#include "ns3/ndnSIM/NFD/daemon/fw/forwarder.hpp"
#include "dtn-core.hpp"
#include "twin-model.hpp"
#include "control-app.hpp"
#include "measured-lru.hpp"
#include "official-trace.hpp"
#include "ns3/ndnSIM/utils/tracers/ndn-l3-rate-tracer.hpp"
#include "ns3/ndnSIM/utils/tracers/ndn-app-delay-tracer.hpp"
#include "ns3/ndnSIM/utils/tracers/l2-rate-tracer.hpp"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "experiment-config.hpp"
#include "energy-config.hpp"
#include "replay-consumer.hpp"
#include "experiment-state.hpp"
#include <functional>
#include <set>
#include <stdexcept>

namespace dtn {
using namespace ns3;
class Experiment : public ExperimentConfig {
public:
  // Physical network and observed business state.
  Topology graph;
  Energy energy;
  // Node indices are C1, C2, R1, R2, R3, P, D; router arrays use R1, R2, R3.
  std::array<Ptr<Node>,7> nodes;
  std::array<Counters,7> stats;
  std::array<LogicalObject,3> los;
  std::array<std::vector<uint64_t>,7> networkFaces;
  std::array<Ptr<ReplayConsumer>,2> consumers;
  std::array<uint64_t,2> ingressFaces{{0,0}};
  // Output streams, independent official traces and cost totals.
  std::ofstream timeline, allocations, histories, deliveries, requestsFile, metadata;
  std::shared_ptr<std::ofstream> officialStream;
  std::vector<Ptr<ns3::ndn::L3RateTracer>> officialTracers;
  Delays officialDelays;
  double dtControl=0, dtCollection=0, dtCompute=0;
  unsigned rounds=0,collectionReports=0,computeRounds=0;
  std::array<uint64_t,3> previousHits{{0,0,0}},previousMisses{{0,0,0}};
  bool HasDT() const {return mode=="heuristic"||mode=="shadow"||mode=="adaptive"||mode=="noop";
  }
  // Controller state persists across rounds; all simulation callbacks share it.
  unsigned stableWindows=0;
  double nextPeriod=5;
  std::array<Ptr<ControlApp>,3> poApps;
  Ptr<ControlApp> controller;
  std::array<TwinSnapshot,3> twinBatch;
  std::array<double,3> lastSample{{0,0,0}};
  std::array<uint64_t,3> poVersion{{0,0,0}},reportRound{{0,0,0}};
  std::array<std::string,3> cachedReportName,cachedReport;
  std::deque<Event> past;
  // Update controller-known quotas through received snapshots and confirmed command replies.
  Quotas known{{50,50,50}};
  std::vector<std::pair<unsigned,unsigned>> commands;
  size_t commandIndex=0;
  std::string pendingRpc;
  unsigned retry=0,rpcNode=0;
  // Changing this token invalidates callbacks for superseded RPC timeouts.
  uint64_t rpcToken=0;
  std::string reportBuffer;
  unsigned reportSegment=0;
  bool collecting=false;
  unsigned failedRounds=0;
  uint64_t modelOperations=0;
  std::ofstream controls,predictions;
  double rpcStarted=0,roundStarted=0;
  Choice selected;
  std::array<double,3> poChanged{{0,0,0}};
  std::map<std::string,double> previousDistribution;
  std::array<double,3> expectedHit{{-1,-1,-1}};
  std::array<uint64_t,3> expectedVersion{{0,0,0}};
  // Closed loop: collect -> evaluate -> apply -> confirm -> schedule next round.
  std::string PoServe(unsigned k,const ::ndn::Name& name);
  void BeginRound();
  void SendRpc(const std::string& name);
  void RpcTimeout(uint64_t token);
  void GotRpc(const std::string& name,const std::string& text);
  void Evaluate();
  void NextCommand();
  void EndRound(bool ok);
  // Network observations and measurement helpers.
  double ControlEnergy(unsigned i) const;
  nfd::cs::Cs& Cs(unsigned i);
  bool IsNetwork(unsigned node, const nfd::Face& face) const;
  void InterestIn(unsigned i,const ::ndn::Interest& p,const nfd::Face& f);
  void InterestOut(unsigned i,const ::ndn::Interest& p,const nfd::Face& f);
  void DataIn(unsigned i,const ::ndn::Data& p,const nfd::Face& f);
  void DataOut(unsigned i,const ::ndn::Data& p,const nfd::Face& f);
  void NackIn(unsigned i,const ::ndn::lp::Nack& p,const nfd::Face& f);
  void NackOut(unsigned i,const ::ndn::lp::Nack& p,const nfd::Face& f);
  // Setup stages, called in the order documented in experiment-network.hpp.
  void Setup();
  void OpenOutputs();
  void BuildNetwork();
  void InstallBusiness();
  void InstallTracers();
  void ScheduleExperiment();
  void FinishOfficialTraffic();
  void Apply(Quotas q,const std::string& reason);
  double Communication(unsigned i) const;
  double CacheEnergy(unsigned i) const;
  void Log();
  void Finish();

};
} // namespace dtn

// One scenario translation unit: definitions follow the shared declaration.
#include "experiment-metrics.hpp"
#include "experiment-network.hpp"
#include "experiment-control.hpp"
#include "experiment-decision.hpp"
