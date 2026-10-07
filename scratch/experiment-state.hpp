// Store node counters, sampled router statistics, and bounded logical-object histories.
#pragma once
#include "twin-model.hpp"
#include <deque>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace dtn {
struct Counters {
  uint64_t tx=0, rx=0, interests=0, hits=0, misses=0, inserts=0, refreshes=0, evictions=0;
  // Control bytes are cumulative; windowRequests, dropped, and events reset when sampled.
  uint64_t ctrlTx=0,ctrlRx=0,windowRequests=0,dropped=0;
  std::vector<Event> events;
  std::map<std::string,uint64_t> names;
};
struct Snapshot {
  double time=0, interval=0;
  uint64_t requests=0, hits=0, misses=0;
  unsigned occupancy=0, capacity=0;
  std::map<std::string,uint64_t> names;
};
struct LogicalObject {
  std::deque<Snapshot> history;
  double predictedRate=0;
  void Update(const Snapshot& s,double alpha) {
    const double rate=s.requests/s.interval;
    // Initialize from the first sample to avoid an artificial zero-rate warm-up.
    predictedRate=history.empty()?rate:PredictRate(predictedRate,rate,alpha);
    history.push_back(s);
     // Retain the twelve most recent snapshots for each logical router.
     if (history.size()>12) history.pop_front();
  }
};
} // namespace dtn
