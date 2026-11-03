#pragma once
// A reconstruction of spit-hpc's association tree as of 2026-10-15, built
// from the numbers in the KT and the scheduling brief:
//   - 29 user associations
//   - account RawUsage: hpc_admin 22, hpc_researcher 5,565,529, hpc_student 23,448,447
//   - ~18 approved students; user_c is the heaviest student (rank 1 of 29)
// Per-user splits are invented (the real ones are in `sshare -l`), but the
// account totals are the live ones, so LevelFS matches the brief.
//
// Exercise: replace the invented numbers with real `sshare -l -P` output
// from the cluster and check that every FairShare value comes out the same.
#include <array>
#include <cstdint>
#include <numeric>
#include <string>

#include "sched/fairtree.hpp"

namespace msl::spit {

inline constexpr double kAdminUsage = 22;
inline constexpr double kResearcherUsage = 5565529;
inline constexpr double kStudentUsage = 23448447;

inline AssocTree build_tree() {
    AssocTree t;
    Assoc& root = t.root();
    t.add_user(root, "root", 1, 0);

    Assoc& admin = t.add_account(root, "hpc_admin");
    t.add_user(admin, "admin_1", 1, kAdminUsage);
    t.add_user(admin, "admin_2", 1, 0);
    t.add_user(admin, "admin_3", 1, 0);

    Assoc& res = t.add_account(root, "hpc_researcher");
    const std::array<double, 6> r{2100000, 1300000, 900000, 600000, 400000, 200000};
    const double r_rest = kResearcherUsage - std::accumulate(r.begin(), r.end(), 0.0);
    for (std::size_t i = 0; i < r.size(); ++i)
        t.add_user(res, "researcher_" + std::to_string(i + 1), 1, r[i]);
    t.add_user(res, "researcher_7", 1, r_rest);

    Assoc& stu = t.add_account(root, "hpc_student");
    constexpr double user_c = 4500000;  // heaviest student
    const std::array<double, 16> s{3100000, 2600000, 2200000, 1900000, 1600000, 1400000,
                                   1200000, 1000000, 800000,  650000,  500000,  380000,
                                   250000,  150000,  80000,   30000};
    const double s_rest = kStudentUsage - user_c - std::accumulate(s.begin(), s.end(), 0.0);
    t.add_user(stu, "user_c", 1, user_c);
    for (std::size_t i = 0; i < s.size(); ++i)
        t.add_user(stu, "student_" + std::to_string(i + 1), 1, s[i]);
    t.add_user(stu, "student_17", 1, s_rest);

    t.compute();
    return t;
}

}  // namespace msl::spit
