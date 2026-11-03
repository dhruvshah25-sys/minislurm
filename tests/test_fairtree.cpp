#include <cmath>

#include "../tools/spit_scenario.hpp"
#include "sched/fairtree.hpp"
#include "test.hpp"

using namespace msl;

static const Assoc* child(const Assoc& a, const char* name) {
    for (const auto& c : a.children)
        if (c->name == name && !c->is_user) return c.get();
    return nullptr;
}

TEST(FairTreeAccountLevelFSMatchesLiveSshare) {
    AssocTree t = spit::build_tree();
    const Assoc* res = child(t.root(), "hpc_researcher");
    const Assoc* stu = child(t.root(), "hpc_student");
    CHECK(res && stu);
    // sshare -l, 2026-10-15
    CHECK_NEAR(res->norm_shares, 0.25, 1e-12);
    CHECK_NEAR(res->effective_usage, 0.191822, 1e-6);
    CHECK_NEAR(res->level_fs, 1.303290, 1e-5);
    CHECK_NEAR(stu->effective_usage, 0.808177, 1e-6);
    CHECK_NEAR(stu->level_fs, 0.309338, 1e-5);
}

TEST(FairTreeRanksAreMultiplesOfOneOverN) {
    AssocTree t = spit::build_tree();
    CHECK_EQ(t.user_count(), 29u);
    for (const Assoc* u : t.rank_order()) {
        const double r = u->fairshare * 29.0;
        CHECK_NEAR(r, std::round(r), 1e-9);
    }
    // Heaviest student is last: 1/29 -> 3448 FairShare points.
    CHECK_EQ(t.find_user("hpc_student", "user_c")->rank, 1u);
}

TEST(FairTreeEveryResearcherBeatsEveryStudent) {
    AssocTree t = spit::build_tree();
    std::uint32_t worst_researcher = 1000, best_student = 0;
    for (const Assoc* u : t.rank_order()) {
        if (u->account() == "hpc_researcher") worst_researcher = std::min(worst_researcher, u->rank);
        if (u->account() == "hpc_student") best_student = std::max(best_student, u->rank);
    }
    CHECK(worst_researcher > best_student);
    CHECK_EQ(best_student, 18u);  // 18/29 = 0.620690, seen in live sshare
}

TEST(FairTreeTiedUsersShareRank) {
    AssocTree t = spit::build_tree();
    // admin_2 and admin_3 both have 0 usage => LevelFS = inf => same rank.
    const Assoc* a2 = t.find_user("hpc_admin", "admin_2");
    const Assoc* a3 = t.find_user("hpc_admin", "admin_3");
    const Assoc* a1 = t.find_user("hpc_admin", "admin_1");
    CHECK_EQ(a2->rank, a3->rank);
    CHECK_EQ(a2->rank, 28u);  // 28/29 = 0.965517, seen in live sshare
    CHECK_EQ(a1->rank, 26u);  // ties consume rank slots (competition ranking)
}

TEST(FairTreeTiedAccountsMergeChildren) {
    // Two accounts with identical usage: their users are ranked as one pool.
    AssocTree t;
    Assoc& a = t.add_account(t.root(), "a");
    Assoc& b = t.add_account(t.root(), "b");
    t.add_user(a, "a_heavy", 1, 90);
    t.add_user(a, "a_light", 1, 10);
    t.add_user(b, "b_mid1", 1, 50);
    t.add_user(b, "b_mid2", 1, 50);
    t.compute();
    // Pool sorted by LevelFS: a_light (0.5/0.1=5) > b_mid* (1) > a_heavy (0.56)
    CHECK_EQ(t.find_user("a", "a_light")->rank, 4u);
    CHECK_EQ(t.find_user("b", "b_mid1")->rank, 3u);
    CHECK_EQ(t.find_user("b", "b_mid2")->rank, 3u);
    CHECK_EQ(t.find_user("a", "a_heavy")->rank, 1u);
}

TEST(FairTreeUsageChangesRank) {
    AssocTree t = spit::build_tree();
    Assoc* c = t.find_user("hpc_student", "user_c");
    c->raw_usage = 0;  // user_c stops for a long time (usage decayed away)
    t.compute();
    CHECK_EQ(c->rank, 18u);  // now the best student... still below every researcher
}
