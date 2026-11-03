#pragma once
// Usage decay: FairShare's "memory".
//
//   U_eff(t) = U_raw * 0.5^(dt / half_life)          PriorityDecayHalfLife=7-0
//
// slurmctld does NOT decay each job separately. Every PriorityCalcPeriod
// (5 min on spit-hpc) it walks the association tree and does
//
//   usage = usage * decay_factor(period) + billing * period_seconds
//
// for every running job. Because 0.5^(a) * 0.5^(b) = 0.5^(a+b), doing many
// small decay steps gives exactly the same answer as one big one. That is
// what lets a daemon keep a single running number per user instead of the
// full job history. (Test: DecayComposes.)
#include <cstdint>

namespace msl {

inline constexpr double kSecondsPerDay = 86400.0;

double decay_factor(double dt_seconds, double half_life_seconds);

class UsageAccumulator {
public:
    explicit UsageAccumulator(double half_life_seconds) : half_life_(half_life_seconds) {}

    // Advance time by dt with `billing` units allocated during that window
    // (0 if the user had nothing running). Mirrors one PriorityCalcPeriod tick.
    void tick(double dt_seconds, double billing);

    // Just decay (no running jobs).
    void decay(double dt_seconds) { tick(dt_seconds, 0.0); }

    // Charge usage instantly (e.g. when replaying a finished job).
    void add(double billing_seconds) { usage_ += billing_seconds; }

    double value() const { return usage_; }

private:
    double half_life_;
    double usage_ = 0.0;
};

}  // namespace msl
