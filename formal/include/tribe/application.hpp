#pragma once

#include <cstddef>
#include <filesystem>
#include <iosfwd>

namespace tribe {

int runApplication(std::istream &input, std::ostream &output, const std::filesystem::path &saveRoot,
                   bool interactive, bool ansiEnabled, std::size_t terminalWidth = 0U);

} // namespace tribe
