#include "sched/billing.hpp"

#include <algorithm>
#include <cmath>

namespace msl {

Billing compute_billing(const Tres& alloc, const BillingWeights& w, BillingMode mode) {
    Billing b;
    if (!w.configured()) {
        b.cpu_part = static_cast<double>(alloc.cpus);
        b.raw      = b.cpu_part;
        b.billed   = alloc.cpus;
        return b;
    }

    const double mem_gb = static_cast<double>(alloc.mem_mb) / 1024.0;
    b.cpu_part = static_cast<double>(alloc.cpus) * w.cpu;
    b.mem_part = mem_gb * w.mem_per_gb;
    b.gpu_part = static_cast<double>(alloc.gpus) * w.gpu;

    b.raw = (mode == BillingMode::MaxTres)
                ? std::max({b.cpu_part, b.mem_part, b.gpu_part})
                : b.cpu_part + b.mem_part + b.gpu_part;

    // Slurm keeps billing as uint64 and truncates toward zero.
    b.billed = static_cast<std::uint64_t>(std::floor(b.raw));
    return b;
}

}  // namespace msl
