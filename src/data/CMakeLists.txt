include(FetchContent)

# QXlsx: Excel reading for the import readers. Linked only into the data layer.
# Pinned to the latest release tag (v1.5.1.1 = 8a13e1c); see docs/adr/ADR-006-spike-results.md.
#
# Patch: QXlsx links Qt6::GuiPrivate only for QZipReader/QZipWriter, which Qt 6.12 ships in
# QtCore. On Linux Qt6GuiPrivate also requires the XKB development headers, so the unpatched
# QXlsx fails to configure there. Link Qt6::Gui + Qt6::CorePrivate instead. Idempotent.
set(qxlsx_patch_script "${CMAKE_CURRENT_BINARY_DIR}/PatchQXlsx.cmake")
file(CONFIGURE OUTPUT "${qxlsx_patch_script}" @ONLY CONTENT [=[
file(READ QXlsx/CMakeLists.txt content)
string(REPLACE [[COMPONENTS GuiPrivate]] [[COMPONENTS CorePrivate]] content "${content}")
string(REPLACE [[Qt${QT_VERSION_MAJOR}::GuiPrivate]]
               [[Qt${QT_VERSION_MAJOR}::Gui Qt${QT_VERSION_MAJOR}::CorePrivate]]
               content "${content}")
file(WRITE QXlsx/CMakeLists.txt "${content}")
]=])

FetchContent_Declare(QXlsx
    GIT_REPOSITORY https://github.com/QtExcel/QXlsx.git
    GIT_TAG v1.5.1.1
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR QXlsx
    PATCH_COMMAND "${CMAKE_COMMAND}" -P "${qxlsx_patch_script}"
)
set(BUILD_SHARED_LIBS OFF)
# Qt6::CorePrivate is found inside the QXlsx directory; make it visible to the executables
# that finally link QXlsx (FootballDSS, fdss_tests, qml_smoke).
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL ON)
# Using private headers ties the build to this exact Qt version. Accepted: the Windows
# package ships the Qt it was built with (ADR-006).
set(QT_NO_PRIVATE_MODULE_WARNING ON)
FetchContent_MakeAvailable(QXlsx)
# QXlsx writes CMAKE_CXX_STANDARD=17 to the cache, which (policy CMP0126 OLD) only hides our
# C++23 on a fresh configure. Pin the standard upstream tests with, so every build is the same.
# Our own targets stay on C++23 (root CMakeLists.txt).
set_target_properties(QXlsx PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
# lupdate scans every target of the project; third-party sources stay out of our .ts files.
set_property(TARGET QXlsx PROPERTY QT_EXCLUDE_FROM_TRANSLATION ON)

add_library(fdss_data STATIC
    db/AppPaths.hpp
    db/AppPaths.cpp
    db/Backup.hpp
    db/Backup.cpp
    db/DatabaseManager.hpp
    db/DatabaseManager.cpp
    db/Migrations.hpp
    db/Migrations.cpp
    db/Transaction.hpp
    db/Transaction.cpp
    readers/TacticJson.hpp
    readers/TacticJson.cpp
    repositories/AvailabilityRepository.hpp
    repositories/AvailabilityRepository.cpp
    repositories/ClubRepository.hpp
    repositories/ClubRepository.cpp
    repositories/LineupRepository.hpp
    repositories/LineupRepository.cpp
    repositories/MatchRepository.hpp
    repositories/MatchRepository.cpp
    repositories/OpponentRepository.hpp
    repositories/OpponentRepository.cpp
    repositories/PlayerRepository.hpp
    repositories/PlayerRepository.cpp
    repositories/RepositorySupport.hpp
    repositories/RepositorySupport.cpp
    repositories/StaffRepository.hpp
    repositories/StaffRepository.cpp
    repositories/TrainingRepository.hpp
    repositories/TrainingRepository.cpp
)

# Migrations are embedded in the binary; see db/Migrations.hpp for the file format.
qt_add_resources(fdss_data "fdss_migrations"
    PREFIX "/fdss"
    FILES
        migrations/0001_initial.sql
)

target_link_libraries(fdss_data
    PUBLIC fdss_core Qt6::Core Qt6::Sql
    PRIVATE QXlsx::QXlsx
)
