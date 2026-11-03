#include "sched/decay.hpp"

#include <cmath>

namespace msl {

double decay_factor(double dt_seconds, double half_life_seconds) {
    if (half_life_seconds <= 0.0) return 1.0;  // PriorityDecayHalfLife=0 => no decay
    return std::pow(0.5, dt_seconds / half_life_seconds);
}

void UsageAccumulator::tick(double dt_seconds, double billing) {
    // Order matters slightly: Slurm decays the old usage, then adds the
    // usage accrued during the period (un-decayed). For 5-minute periods
    // against a 7-day half-life the difference is ~0.03%.
    usage_ = usage_ * decay_factor(dt_seconds, half_life_) + billing * dt_seconds;
}

}  // namespace msl
