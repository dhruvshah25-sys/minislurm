// Brief §4.2.4: 1000 units on day 0 with PriorityDecayHalfLife=7-0.
#include "sched/decay.hpp"
#include "test.hpp"

using namespace msl;

static constexpr double kHalfLife = 7 * kSecondsPerDay;

TEST(DecayTable) {
    auto at = [](double days) { return 1000.0 * decay_factor(days * kSecondsPerDay, kHalfLife); };
    CHECK_NEAR(at(0), 1000.0, 1e-9);
    CHECK_NEAR(at(1), 905.7, 0.05);
    CHECK_NEAR(at(3), 743.0, 0.05);
    CHECK_NEAR(at(7), 500.0, 1e-9);
    CHECK_NEAR(at(14), 250.0, 1e-9);
    CHECK_NEAR(at(21), 125.0, 1e-9);
    CHECK_NEAR(at(28), 62.5, 1e-9);
}

TEST(DecayComposes) {
    // 4 weeks of 5-minute PriorityCalcPeriod ticks == one 28-day decay.
    UsageAccumulator u(kHalfLife);
    u.add(1000.0);
    const int ticks = 28 * 24 * 12;
    for (int i = 0; i < ticks; ++i) u.decay(300.0);
    CHECK_NEAR(u.value(), 62.5, 1e-6);
}

TEST(DecaySteadyState) {
    // A user who runs a constant `b` billing forever converges to
    // b * period / (1 - decay(period)), roughly b * half_life / ln 2.
    UsageAccumulator u(kHalfLife);
    const double b = 112.0;  // one GPU, always busy
    for (int i = 0; i < 365 * 24 * 12; ++i) u.tick(300.0, b);
    const double expected = b * 300.0 / (1.0 - decay_factor(300.0, kHalfLife));
    CHECK_NEAR(u.value() / expected, 1.0, 1e-6);
}
