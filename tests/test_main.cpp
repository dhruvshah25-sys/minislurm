#include "test.hpp"

int main() {
    int ran = 0;
    for (const auto& c : msltest::registry()) {
        const int before = msltest::failures();
        c.fn();
        ++ran;
        std::printf("%s %s\n", msltest::failures() == before ? "[ ok ]" : "[FAIL]", c.name);
    }
    std::printf("\n%d tests, %d failed checks\n", ran, msltest::failures());
    return msltest::failures() == 0 ? 0 : 1;
}
