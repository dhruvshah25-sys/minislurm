#pragma once
// Fair Tree FairShare (Slurm's default since 19.05; what spit-hpc runs).
//
// The association tree on spit-hpc:
//
//   root
//   ├── root (user)
//   ├── hpc_admin       ── users...
//   ├── hpc_researcher  ── users...
//   └── hpc_student     ── users...
//
// Step 1 - LevelFS. At each level, for every association A among its siblings:
//     S = A.shares / sum(sibling shares)
//     U = A.usage  / sum(sibling usage)       (account usage = sum of subtree)
//     LevelFS = S / U                         (U == 0 => +infinity)
//   LevelFS > 1: used less than its share.  < 1: used more.
//
// Step 2 - rank. Walk the tree depth-first, visiting siblings in order of
// DESCENDING LevelFS. Each user gets a rank in the order reached, counting
// down from N (number of users). Tied users share a rank.
//     fairshare_factor = rank / N
//
// Consequence (visible on spit-hpc): accounts are compared first, so every
// researcher outranks every student no matter how much the researcher used,
// and FairShare points come in steps of 100000/29 ~= 3448.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace msl {

struct Assoc {
    std::string name;
    bool is_user = false;
    std::uint32_t raw_shares = 1;
    double raw_usage = 0.0;  // users: set directly. accounts: computed = sum of subtree.

    Assoc* parent = nullptr;
    std::vector<std::unique_ptr<Assoc>> children;

    // ---- outputs of AssocTree::compute() (names follow `sshare -l`) ----
    double norm_shares = 0.0;      // S, relative to siblings
    double effective_usage = 0.0;  // usage / root usage  (sshare EffectvUsage)
    double level_fs = 0.0;         // S / U at this level
    std::uint32_t rank = 0;        // users only
    double fairshare = 0.0;        // users only: rank / N

    std::string account() const { return parent ? parent->name : std::string{}; }
};

class AssocTree {
public:
    AssocTree();

    Assoc& root() { return *root_; }
    const Assoc& root() const { return *root_; }

    Assoc& add_account(Assoc& parent, std::string name, std::uint32_t shares = 1);
    Assoc& add_user(Assoc& account, std::string name, std::uint32_t shares = 1, double usage = 0.0);

    // Recompute account usage, LevelFS, ranks and fairshare factors.
    // Call after changing any usage (slurmctld does this every PriorityCalcPeriod).
    void compute();

    Assoc* find_user(const std::string& account, const std::string& user);
    const Assoc* find_user(const std::string& account, const std::string& user) const;

    // Users in the order Fair Tree visited them (highest rank first).
    const std::vector<const Assoc*>& rank_order() const { return rank_order_; }
    std::uint32_t user_count() const { return user_count_; }

private:
    double sum_usage(Assoc& a);
    void compute_level(std::vector<Assoc*>& siblings);
    void rank_level(std::vector<Assoc*> siblings);

    std::unique_ptr<Assoc> root_;
    std::uint32_t user_count_ = 0;

    // ranking state (Slurm calls these rank and rnt = "rank, no ties")
    std::uint32_t rank_ = 0;
    std::uint32_t rnt_ = 0;
    std::vector<const Assoc*> rank_order_;
};

}  // namespace msl
