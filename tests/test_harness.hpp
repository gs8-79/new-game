#pragma once

#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace test {

using TestFunction = std::function<void()>;

inline std::vector<std::pair<std::string, TestFunction>>& registry() {
    static std::vector<std::pair<std::string, TestFunction>> tests;
    return tests;
}

class Registrar {
public:
    Registrar(std::string name, TestFunction function) {
        registry().emplace_back(std::move(name), std::move(function));
    }
};

inline void require(const bool condition, const char* expression, const char* file, const int line) {
    if (!condition) {
        std::ostringstream message;
        message << file << ':' << line << " REQUIRE failed: " << expression;
        throw std::runtime_error(message.str());
    }
}

} // namespace test

#define MUD_TEST_JOIN_INNER(a, b) a##b
#define MUD_TEST_JOIN(a, b) MUD_TEST_JOIN_INNER(a, b)
#define TEST_CASE(name) \
    static void MUD_TEST_JOIN(testFunction_, __LINE__)(); \
    static ::test::Registrar MUD_TEST_JOIN(testRegistrar_, __LINE__)(name, MUD_TEST_JOIN(testFunction_, __LINE__)); \
    static void MUD_TEST_JOIN(testFunction_, __LINE__)()
#define REQUIRE(expression) ::test::require(static_cast<bool>(expression), #expression, __FILE__, __LINE__)

