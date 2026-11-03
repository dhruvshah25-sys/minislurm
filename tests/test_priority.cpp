// The headline test: reproduce live `sprio -l` on spit-hpc (2026-10-15).
//     JOBID USER    PRIORITY  AGE  FAIRSHARE  JOBSIZE
//      1900 user_c      4104   85       3448      571
//      1901 user_c      4103   83       3448      571
//      1905 user_c      4082   63       3448      571
//
// The wait times below aren't guesses: the live output pins them down.
// FAIRSHARE + JOBSIZE = 3448.276 + 571.429 = 4019.705 (unrounded), so for
// job 1905, total 4082 needs age in [62.295, 63.295) and the AGE column 63
// needs age in [63, 64). Both hold only for age in [63, 63.295) points,
// i.e. a wait of 10h30m-10h35m. Same reasoning gives 1900 and 1901.
#include "../tools/spit_scenario.hpp"
#include "sched/priority.hpp"
#include "test.hpp"

using namespace msl;

static Priority prio(double eligible_s, std::uint32_t cpus = 32) {
    AssocTree t = spit::build_tree();
    const Assoc* c = t.find_user("hpc_student", "user_c");
    PendingJob j{0, "user_c", "hpc_student", 1, cpus, eligible_s};
    return compute_priority(j, c->fairshare, kSpitPriority);
}

TEST(PriorityLiveSprio1900) {
    Priority p = prio(51468.0);
    CHECK_EQ(p.age_col(), 85u);
    CHECK_EQ(p.fairshare_col(), 3448u);
    CHECK_EQ(p.jobsize_col(), 571u);
    CHECK_EQ(p.total, 4104u);
}

TEST(PriorityTotalIsNotSumOfColumns) {
    // Job 1901: columns sum to 4102 but sprio says 4103, because slurmctld
    // truncates the *sum* of doubles, not each term.
    Priority p = prio(50501.0);
    CHECK_EQ(p.age_col(), 83u);
    CHECK_EQ(p.age_col() + p.fairshare_col() + p.jobsize_col(), 4102u);
    CHECK_EQ(p.total, 4103u);
}

TEST(PriorityLiveSprio1905) {
    Priority p = prio(38163.0);
    CHECK_EQ(p.age_col(), 63u);
    CHECK_EQ(p.total, 4082u);
}

TEST(JobSizeTable) {
    // Brief §4.4.1
    auto pts = [](std::uint32_t cpus) {
        return static_cast<std::uint32_t>(1000 * jobsize_factor(1, cpus, 1, 224));
    };
    CHECK_EQ(pts(1), 502u);
    // 8 CPUs = 517.86: the brief prints 518. Truncate vs round is unverified
    // (see priority.hpp), so check the exact value instead of a column.
    CHECK_NEAR(1000 * jobsize_factor(1, 8, 1, 224), 517.857, 1e-3);
    CHECK_EQ(pts(32), 571u);
    CHECK_EQ(pts(112), 750u);
    CHECK_EQ(pts(224), 1000u);
}

TEST(AgeCapsAtSevenDays) {
    CHECK_NEAR(age_factor(3.5 * 86400, 7 * 86400), 0.5, 1e-12);
    CHECK_NEAR(age_factor(30 * 86400, 7 * 86400), 1.0, 1e-12);
}

TEST(OneRankStepBeatsMaxAgeAndSize) {
    // The "dominance argument": 100000/29 > 1000 (age) + 500 (size spread).
    const double step = kSpitPriority.weights.fairshare / 29.0;
    CHECK(step > 1000.0 + 500.0);
}
