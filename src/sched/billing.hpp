#pragma once
// Billing: turning an allocation into a single "cost per second" number.
//
// spit-hpc (since 2026-10-15):
//   TRESBillingWeights="CPU=1.0,Mem=0.92G,GRES/gpu=112"
//   PriorityFlags=MAX_TRES
//
// Every weight is chosen so the whole node is worth ~224 "CPU-equivalents":
//   224 CPUs x 1.0 = 224,  ~244 GB x 0.92 = ~224,  2 GPUs x 112 = 224.
// With MAX_TRES a job pays for its *dominant* resource, not the sum.
// Slurm stores billing as an integer and truncates (117.76 -> 117).
#include <cstdint>

#include "sched/tres.hpp"

namespace msl {

struct BillingWeights {
    double cpu        = 0.0;  // per CPU
    double mem_per_gb = 0.0;  // per GB ("0.92G" in slurm.conf); GB = MB / 1024
    double gpu        = 0.0;  // per GPU

    bool configured() const { return cpu != 0.0 || mem_per_gb != 0.0 || gpu != 0.0; }
};

enum class BillingMode {
    Sum,      // default Slurm: billing = sum of weighted TRES
    MaxTres,  // PriorityFlags=MAX_TRES: billing = max of weighted TRES
};

struct Billing {
    double cpu_part = 0.0;
    double mem_part = 0.0;
    double gpu_part = 0.0;
    double raw      = 0.0;         // before truncation
    std::uint64_t billed = 0;      // what sacct shows as billing=
};

// No weights configured => Slurm falls back to billing = allocated CPU count.
// (This is exactly why, before Oct 2026, GPUs were "free" for FairShare.)
Billing compute_billing(const Tres& alloc, const BillingWeights& w, BillingMode mode);

// The live spit-hpc policy, so tests and tools share one definition.
inline constexpr BillingWeights kSpitWeights{1.0, 0.92, 112.0};

}  // namespace msl
