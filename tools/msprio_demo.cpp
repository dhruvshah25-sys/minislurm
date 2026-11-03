// msprio-demo: prints sshare/sprio/billing views of the reconstructed
// spit-hpc state, computed by minislurm's own scheduler math.
// Compare the output with the real commands on the cluster.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "sched/billing.hpp"
#include "sched/priority.hpp"
#include "spit_scenario.hpp"

using namespace msl;

static void print_sshare(const Assoc& a, int depth) {
    if (depth > 0) {
        std::string indent(static_cast<std::size_t>(depth - 1), ' ');
        const std::string acct = a.is_user ? a.account() : indent + a.name;
        std::printf("%-18s %-14s %10.6f %12.0f %12.6f %12.4g", acct.c_str(),
                    a.is_user ? a.name.c_str() : "", a.norm_shares, a.raw_usage,
                    a.effective_usage, a.level_fs);
        if (a.is_user) std::printf("  %2u/29 = %.6f", a.rank, a.fairshare);
        std::printf("\n");
    }
    for (const auto& c : a.children) print_sshare(*c, depth + 1);
}

int main() {
    AssocTree tree = spit::build_tree();

    std::printf("== sshare -l  (Fair Tree) ==\n");
    std::printf("%-18s %-14s %10s %12s %12s %12s  %s\n", "Account", "User", "NormShares",
                "RawUsage", "EffectvUsage", "LevelFS", "FairShare");
    print_sshare(tree.root(), 0);

    std::printf("\n== sprio -l ==\n");
    const Assoc* c = tree.find_user("hpc_student", "user_c");
    struct { std::uint32_t id; double eligible_s; } jobs[] = {
        {1900, 51468.0}, {1901, 50501.0}, {1905, 38163.0}};  // ~14.3h, 14.0h, 10.6h
    std::printf("%8s %-9s %-8s %9s %6s %10s %8s\n", "JOBID", "PARTITION", "USER", "PRIORITY",
                "AGE", "FAIRSHARE", "JOBSIZE");
    for (auto j : jobs) {
        PendingJob pj{j.id, "user_c", "hpc_student", 1, 32, j.eligible_s};
        Priority p = compute_priority(pj, c->fairshare, kSpitPriority);
        std::printf("%8u %-9s %-8s %9u %6u %10u %8u   (cols sum to %u)\n", j.id, "general",
                    "user_c", p.total, p.age_col(), p.fairshare_col(), p.jobsize_col(),
                    p.age_col() + p.fairshare_col() + p.jobsize_col());
    }

    std::printf("\n== billing (TRESBillingWeights=CPU=1.0,Mem=0.92G,GRES/gpu=112, MAX_TRES) ==\n");
    struct { const char* shape; Tres t; } shapes[] = {
        {"32 CPU, 80 GB, 1 GPU", {32, 80 * 1024, 1}},
        {"32 CPU, 128 GB, 1 GPU", {32, 128 * 1024, 1}},
        {"8 CPU, 16 GB, 1 GPU", {8, 16 * 1024, 1}},
        {"32 CPU, 64 GB, no GPU", {32, 64 * 1024, 0}},
        {"2 GPU, 8 CPU, 32 GB", {8, 32 * 1024, 2}},
        {"1 core (2 thr), 8 GB", {2, 8 * 1024, 0}},
    };
    std::printf("%-24s %8s %8s %8s %10s %10s\n", "shape", "cpu", "mem", "gpu", "billing",
                "old(cpu)");
    for (auto& s : shapes) {
        Billing now = compute_billing(s.t, kSpitWeights, BillingMode::MaxTres);
        Billing old = compute_billing(s.t, BillingWeights{}, BillingMode::Sum);
        std::printf("%-24s %8.2f %8.2f %8.2f %10llu %10llu\n", s.shape, now.cpu_part,
                    now.mem_part, now.gpu_part, static_cast<unsigned long long>(now.billed),
                    static_cast<unsigned long long>(old.billed));
    }
    return 0;
}
