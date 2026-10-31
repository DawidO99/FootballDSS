#include "core/util/Version.hpp"

#include <format>

#if !defined(FDSS_VERSION_MAJOR) || !defined(FDSS_VERSION_MINOR) || !defined(FDSS_VERSION_PATCH)
#error "FDSS_VERSION_MAJOR/MINOR/PATCH must be defined by the build system"
#endif

namespace fdss::core {

Version currentVersion() noexcept {
    return {FDSS_VERSION_MAJOR, FDSS_VERSION_MINOR, FDSS_VERSION_PATCH};
}

std::string toString(const Version& version) {
    return std::format("{}.{}.{}", version.major, version.minor, version.patch);
}

} // namespace fdss::core
