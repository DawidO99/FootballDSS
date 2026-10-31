#pragma once

#include <string>

namespace fdss::core {

struct Version {
    int major = 0;
    int minor = 0;
    int patch = 0;

    friend constexpr bool operator==(const Version&, const Version&) = default;
};

// Version of the application, injected from project(VERSION ...) at build time.
[[nodiscard]] Version currentVersion() noexcept;

// Formats a version as "major.minor.patch", e.g. "0.1.0".
[[nodiscard]] std::string toString(const Version& version);

} // namespace fdss::core
