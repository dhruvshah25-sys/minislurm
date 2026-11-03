#pragma once
// Multifactor priority (priority/multifactor).
//
//   P = W_fs * F_fs + W_age * F_age + W_size * F_size  (+ QOS, partition, ... = 0 here)
//
// spit-hpc:  W_fs = 100000, W_age = 1000, W_size = 1000
//   F_fs   = Fair Tree rank / N_users                       (fairtree.hpp)
//   F_age  = min(1, time_eligible / PriorityMaxAge)         PriorityMaxAge = 7 days
//   F_size = 0.5 * (nodes_req / nodes_total + cpus_req / cpus_total)
//            (Slurm 22.05, PriorityFavorSmall=No, no SMALL_RELATIVE_TO_TIME)
//
// Subtle detail worth knowing: slurmctld sums the factors as doubles and
// truncates ONCE. sprio prints each column truncated separately. So the
// columns don't always add up to PRIORITY:
//     job 1901 on spit-hpc: 83 + 3448 + 571 = 4102, but PRIORITY = 4103.
#include <cstdint>
#include <string>

namespace msl {

struct PriorityWeights {
    std::uint32_t fairshare = 0;
    std::uint32_t age = 0;
    std::uint32_t jobsize = 0;
};

struct PriorityConfig {
    PriorityWeights weights;
    double max_age_seconds = 7 * 86400.0;  // PriorityMaxAge (default 7-0)
    std::uint32_t cluster_nodes = 1;
    std::uint32_t cluster_cpus = 1;
};

inline const PriorityConfig kSpitPriority{{100000, 1000, 1000}, 7 * 86400.0, 1, 224};

struct PendingJob {
    std::uint32_t id = 0;
    std::string user;
    std::string account;
    std::uint32_t req_nodes = 1;
    std::uint32_t req_cpus = 1;
    double eligible_seconds = 0.0;  // time since the job became eligible (not since submit!)
};

struct Priority {
    double age = 0.0;        // weighted points, unrounded
    double fairshare = 0.0;
    double jobsize = 0.0;
    std::uint32_t total = 0; // what squeue sorts by

    // What `sprio -l` prints in each column.
    std::uint32_t age_col() const { return static_cast<std::uint32_t>(age); }
    std::uint32_t fairshare_col() const { return static_cast<std::uint32_t>(fairshare); }
    std::uint32_t jobsize_col() const { return static_cast<std::uint32_t>(jobsize); }
};

double age_factor(double eligible_seconds, double max_age_seconds);
double jobsize_factor(std::uint32_t req_nodes, std::uint32_t req_cpus,
                      std::uint32_t cluster_nodes, std::uint32_t cluster_cpus);

Priority compute_priority(const PendingJob& job, double fairshare_factor, const PriorityConfig& cfg);

}  // namespace msl
