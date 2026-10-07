// Generate reproducible content requests for two consumers under stable or changing request rates.
#pragma once
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

namespace dtn {
struct Request { double time;
  unsigned content;
  };

// Requests are generated BEFORE the simulator starts. Controller code never
// accesses these future schedules. Identical seed => identical schedule in all modes.
inline std::vector<Request> MakeRequests(unsigned consumer, unsigned seed, unsigned catalog,
                                         double duration, double switchTime, double high, double low,
                                         double exponent, bool changing) {
  std::mt19937 rng(seed * 997u + consumer * 7919u + 23u);
  std::vector<double> weights(catalog);
  // Sample content ranks with Zipf weights while request times remain regularly spaced.
  for (unsigned i=0;i<catalog;++i) weights[i]=1/std::pow(i+1.0,exponent);
  std::discrete_distribution<unsigned> choose(weights.begin(),weights.end());
  std::vector<Request> result;
  auto phase=[&](double begin,double end,double rate) {
    for (uint64_t k=0;;++k) {
      // Offset consumer schedules slightly to avoid simultaneous initial transmissions.
      double t=begin+0.001+consumer*0.002+static_cast<double>(k)/rate;
      if (t>=end) break;
      result.push_back({t,choose(rng)});
    }
  };
  const double first=consumer==0?high:low;
  // Swap consumer rates at switchTime without changing the content distribution.
  if (changing) { phase(0,switchTime,first);
   phase(switchTime,duration,consumer==0?low:high);
   }
  else phase(0,duration,first);
  return result;
}
} // namespace dtn
