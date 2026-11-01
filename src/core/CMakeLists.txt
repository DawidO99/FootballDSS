add_library(fdss_core STATIC
    domain/Appearance.hpp
    domain/Attendance.hpp
    domain/Availability.hpp
    domain/Availability.cpp
    domain/Club.hpp
    domain/DateTypes.hpp
    domain/Enums.hpp
    domain/Enums.cpp
    domain/Ids.hpp
    domain/Match.hpp
    domain/MatchEvent.hpp
    domain/Opponent.hpp
    domain/Permissions.hpp
    domain/Permissions.cpp
    domain/Player.hpp
    domain/StaffUser.hpp
    domain/Team.hpp
    domain/TrainingSession.hpp
    domain/Validation.hpp
    domain/Validation.cpp
    util/Error.hpp
    util/Error.cpp
    util/Version.hpp
    util/Version.cpp
)

target_include_directories(fdss_core PUBLIC "${PROJECT_SOURCE_DIR}/src")

target_compile_definitions(fdss_core PRIVATE
    FDSS_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    FDSS_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    FDSS_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

# The core layer is plain C++23: no Qt code generators, warnings are errors.
set_target_properties(fdss_core PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

if(MSVC)
    target_compile_options(fdss_core PRIVATE /W4 /WX /utf-8 /permissive-)
else()
    target_compile_options(fdss_core PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
