// Every expected value here is from the spit-hpc scheduling brief (§4.2.2)
// or the KT deck, i.e. what the real cluster recorded in sacct.
#include "sched/billing.hpp"
#include "test.hpp"

using namespace msl;

static std::uint64_t bill(std::uint32_t cpus, std::uint64_t gb, std::uint32_t gpus) {
    return compute_billing(Tres{cpus, gb * 1024, gpus}, kSpitWeights, BillingMode::MaxTres).billed;
}

TEST(BillingGpuDominant) {
    CHECK_EQ(bill(32, 80, 1), 112u);   // GPU wins over 73.6 mem
    CHECK_EQ(bill(8, 16, 1), 112u);
    CHECK_EQ(bill(8, 32, 2), 224u);    // two GPUs = whole node
}

TEST(BillingMemDominantTruncates) {
    CHECK_EQ(bill(32, 128, 1), 117u);  // 117.76 -> 117
    CHECK_EQ(bill(32, 64, 0), 58u);    // 58.88 -> 58
    CHECK_EQ(bill(2, 8, 0), 7u);       // test job 1906: 7.36 -> 7
}

TEST(BillingWholeNodeIsAbout224) {
    // RealMemory=250000 MB is "worth" ~224 CPUs, by design of the 0.92 weight.
    auto b = compute_billing(Tres{0, 250000, 0}, kSpitWeights, BillingMode::MaxTres);
    CHECK_NEAR(b.mem_part, 224.6, 0.1);
}

TEST(BillingDefaultIsCpuCount) {
    // Pre-Oct-2026 behaviour: no weights => billing = CPUs, GPUs free.
    auto b = compute_billing(Tres{8, 16 * 1024, 1}, BillingWeights{}, BillingMode::Sum);
    CHECK_EQ(b.billed, 8u);
}

TEST(BillingSumModeAddsUp) {
    auto b = compute_billing(Tres{32, 80 * 1024, 1}, kSpitWeights, BillingMode::Sum);
    CHECK_EQ(b.billed, 217u);  // 32 + 73.6 + 112 = 217.6
}
