// Allocate bounded cache quotas and calculate smoothed request rates and link energy.
#pragma once
#include <array>
#include <cmath>
#include <stdexcept>

namespace dtn {
using Quotas = std::array<unsigned, 3>;
inline Quotas Allocate(const std::array<double, 3>& weights, unsigned budget,
                       unsigned lower, unsigned upper)
{
  if (lower == 0 || lower > upper || budget < 3 * lower || budget > 3 * upper)
    throw std::invalid_argument("infeasible cache budget/bounds");
  for (double w : weights)
    if (!std::isfinite(w) || w < 0) throw std::invalid_argument("invalid weight");
  // Reserve each router minimum before distributing the remaining entries.
  Quotas q{{lower, lower, lower}};
  const bool empty = weights[0] + weights[1] + weights[2] == 0;
  for (unsigned remaining = budget - 3 * lower; remaining; --remaining) {
    int best = -1;
    double score = -1;
    for (int i = 0; i < 3; ++i) {
      if (q[i] == upper) continue;
      // Diminishing priority spreads entries by weight; equal scores favor the first router.
      const double s = (empty ? 1.0 : weights[i]) / (q[i] - lower + 1.0);
      if (s > score) { score = s; best = i; }
    }
    if (best < 0) throw std::logic_error("allocation exhausted");
    ++q[best];
  }
  return q;
}
inline double PredictRate(double previous, double observed, double alpha)
{
  if (alpha < 0 || alpha > 1 || observed < 0 || previous < 0)
    throw std::invalid_argument("invalid prediction input");
  // Larger alpha gives the latest observation more influence on the predicted rate.
  return alpha * observed + (1 - alpha) * previous;
}
inline double LinkEnergy(double bytes, double txJPerByte, double rxJPerByte)
{
  // Each transferred byte incurs both transmitter and receiver energy.
  return bytes * (txJPerByte + rxJPerByte);
}
} // namespace dtn
