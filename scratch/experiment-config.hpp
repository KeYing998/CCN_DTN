// Define experiment parameters, parse command-line options, and validate scenario constraints.
#pragma once
#include "ns3/core-module.h"
#include "dtn-core.hpp"
#include <string>
#include <stdexcept>

namespace dtn {
struct ExperimentConfig {

  // Files and experiment identity.
  std::string topology="topologies/tree.txt";
  std::string mode="shadow", workload="changing", out="results/dt", config="config/energy.conf";
  unsigned seed=1;
  // Business scenario: two consumers, content popularity and load swap.
  unsigned catalog=200, payload=1024;
  double duration=120, switchTime=60, high=40, low=10, zipf=1.0;
  // Cache constraints and candidate search.
  unsigned budget=150, minimum=10, maximum=100, step=10;
  double gainThreshold=0.01, horizon=30;
  // Model update and controller timing.
  double period=5, computeDelay=0.001, alpha=0.5;
  double minPeriod=1, maxPeriod=20, commandTtl=2;

  void Parse(int argc, char** argv) {
     ns3::CommandLine cmd;
    cmd.AddValue("mode","static|uneven|heuristic|shadow|adaptive|noop|manual",mode);
    cmd.AddValue("topology","TXT topology path",topology);
    cmd.AddValue("step","Quota transfer step",step);
    cmd.AddValue("gainThreshold","Minimum replay gain fraction",gainThreshold);
    cmd.AddValue("horizon","Replay history seconds",horizon);
    cmd.AddValue("minPeriod","Adaptive minimum seconds",minPeriod);
    cmd.AddValue("maxPeriod","Adaptive maximum seconds",maxPeriod);
    cmd.AddValue("workload","stable|changing",workload);
    cmd.AddValue("out","Existing output directory",out);
    cmd.AddValue("energyConfig","Energy coefficient file",config);
    cmd.AddValue("seed","Workload seed",seed);
     cmd.AddValue("catalog","Content count",catalog);
    cmd.AddValue("payload","Data payload bytes",payload);
    cmd.AddValue("duration","Request duration s",duration);
    cmd.AddValue("switchTime","Load swap time s",switchTime);
    cmd.AddValue("high","High request rate /s",high);
    cmd.AddValue("low","Low request rate /s",low);
    cmd.AddValue("zipf","Zipf exponent",zipf);
    cmd.AddValue("period","DT period s",period);
    cmd.AddValue("computeDelay","Modelled controller latency s",computeDelay);
    
    cmd.AddValue("alpha","EWMA weight",alpha);
    cmd.AddValue("budget","v2 total quota (150)",budget);
    cmd.AddValue("minimum","Per-router minimum quota",minimum);
    cmd.AddValue("maximum","Per-router maximum quota",maximum);
    cmd.Parse(argc,argv);
  }

  // Reject incompatible timing and quota settings before constructing the network.
  void Validate() const {
  if(mode!="static"&&mode!="uneven"&&mode!="heuristic"&&mode!="shadow"&&mode!="adaptive"&&mode!="noop"&&mode!="manual")
    throw std::invalid_argument("mode must be static, uneven, heuristic, shadow, adaptive, noop, or manual");
  if(workload!="stable"&&workload!="changing") throw std::invalid_argument("invalid workload");
  if(duration<=0||period<=0||catalog==0||payload==0||high<=0||low<=0||zipf<0||
     computeDelay<0||alpha<0||alpha>1||
     (workload=="changing"&&(switchTime<=0||switchTime>=duration)))
    throw std::invalid_argument("invalid timing/workload parameter");
  // Fixed baseline allocations require a total of 150 and support for quotas 20 and 80.
  if(budget!=150||minimum>20||maximum<80) throw std::invalid_argument("v1 requires budget=150, min<=20, max>=80");
  Allocate({{1,1,1}},budget,minimum,maximum);
  if(minPeriod<=0||maxPeriod<minPeriod||period<minPeriod||period>maxPeriod||horizon<=0||!step||gainThreshold<0||gainThreshold>=1||commandTtl<1.5)
    throw std::invalid_argument("invalid model/adaptive settings");
  }
};
} // namespace dtn
