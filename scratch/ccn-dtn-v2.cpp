// Run the CCN digital-twin simulation and write final traffic and energy metrics.
#include "experiment.hpp"

int main(int argc,char** argv) {
  try {
    dtn::Experiment e;
    e.Parse(argc,argv);
    // Fix the base RNG seed and select an experiment-specific run stream.
    ns3::RngSeedManager::SetSeed(1);
    ns3::RngSeedManager::SetRun(e.seed);
    e.Setup();
    // Allow the last requests to complete their two-second timeout before final accounting.
    ns3::Simulator::Stop(ns3::Seconds(e.duration+2.01));
    ns3::Simulator::Run();
    e.FinishOfficialTraffic();
    e.Finish();
    ns3::Simulator::Destroy();
    return 0;
  } catch(const std::exception& ex) {std::cerr<<"ERROR: "<<ex.what()<<std::endl;
  return 1;
  }
}
