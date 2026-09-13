#include "test_harness.hpp"

#include <exception>
#include <iostream>

int main() {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    int failures = 0;
    for (const auto& [name, function] : test::registry()) {
        std::cout << "[RUN ] " << name << '\n';
        try {
            function();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        } catch (...) {
            ++failures;
            std::cerr << "[FAIL] " << name << ": unknown exception\n";
        }
    }

    std::cout << test::registry().size() - static_cast<std::size_t>(failures) << '/' << test::registry().size()
              << " tests passed\n";
    return failures == 0 ? 0 : 1;
}
