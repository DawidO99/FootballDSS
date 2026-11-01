include(FetchContent)
include(GoogleTest)

# Third-party code is not Qt code: keep the Qt code generators away from it.
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
    GIT_SHALLOW TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(fdss_tests
    test_main.cpp
    core/AvailabilityTest.cpp
    core/EnumsTest.cpp
    core/ErrorTest.cpp
    core/PermissionsTest.cpp
    core/ValidationTest.cpp
    core/VersionTest.cpp
    data/AppPathsTest.cpp
    data/AvailabilityRepositoryTest.cpp
    data/BackupTest.cpp
    data/ClubRepositoryTest.cpp
    data/DatabaseManagerTest.cpp
    data/EnumsSchemaTest.cpp
    data/MatchRepositoryTest.cpp
    data/MigrationsTest.cpp
    data/OpponentRepositoryTest.cpp
    data/PlayerRepositoryTest.cpp
    data/RepositoryFixture.hpp
    data/RepositorySupportTest.cpp
    data/SchemaTest.cpp
    data/StaffRepositoryTest.cpp
    data/TestDatabase.hpp
    data/TrainingRepositoryTest.cpp
    data/TransactionTest.cpp
    data/XlsxSpikeTest.cpp
)
# QXlsx is linked here only to prove the library itself (P03 spike); app code reaches it
# through fdss_data.
target_link_libraries(fdss_tests PRIVATE fdss_data QXlsx::QXlsx GTest::gtest)
target_compile_definitions(fdss_tests PRIVATE
    FDSS_EXPECTED_VERSION="${PROJECT_VERSION}"
    FDSS_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
# Test sources contain Polish string literals.
if(MSVC)
    target_compile_options(fdss_tests PRIVATE /utf-8)
endif()
gtest_discover_tests(fdss_tests DISCOVERY_MODE PRE_TEST)

qt_add_executable(qml_smoke
    qml_smoke/main.cpp
)
target_link_libraries(qml_smoke PRIVATE fdss_ui fdss_uiplugin)
add_test(NAME qml_smoke COMMAND qml_smoke)
set_tests_properties(qml_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
