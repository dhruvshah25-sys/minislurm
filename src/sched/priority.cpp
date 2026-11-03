#include "sched/priority.hpp"

#include <algorithm>

namespace msl {

double age_factor(double eligible_seconds, double max_age_seconds) {
    if (max_age_seconds <= 0.0 || eligible_seconds <= 0.0) return 0.0;
    return std::min(1.0, eligible_seconds / max_age_seconds);
}

double jobsize_factor(std::uint32_t req_nodes, std::uint32_t req_cpus,
                      std::uint32_t cluster_nodes, std::uint32_t cluster_cpus) {
    if (cluster_nodes == 0 || cluster_cpus == 0) return 0.0;
    const double nf = std::min(1.0, static_cast<double>(req_nodes) / cluster_nodes);
    const double cf = std::min(1.0, static_cast<double>(req_cpus) / cluster_cpus);
    return 0.5 * (nf + cf);
}

Priority compute_priority(const PendingJob& job, double fairshare_factor, const PriorityConfig& cfg) {
    Priority p;
    p.age = cfg.weights.age * age_factor(job.eligible_seconds, cfg.max_age_seconds);
    p.fairshare = cfg.weights.fairshare * fairshare_factor;
    p.jobsize = cfg.weights.jobsize *
                jobsize_factor(job.req_nodes, job.req_cpus, cfg.cluster_nodes, cfg.cluster_cpus);

    const double sum = p.age + p.fairshare + p.jobsize;
    // Priority 0 means "held" in Slurm, so a runnable job is at least 1.
    p.total = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(sum));
    return p;
}

}  // namespace msl
