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
    core/ActivityImportTest.cpp
    core/AvailabilityTest.cpp
    core/CapabilitiesTest.cpp
    core/DemoGeneratorTest.cpp
    core/EnumsTest.cpp
    core/ErrorTest.cpp
    core/FormationsTest.cpp
    core/ImportParsingTest.cpp
    core/LineupTest.cpp
    core/LineupValidatorTest.cpp
    core/LiveShapeTest.cpp
    core/LoadDemoTest.cpp
    core/LoadTest.cpp
    core/MetricTest.cpp
    core/MatchMinutesTest.cpp
    core/PermissionsTest.cpp
    core/SquadImportTest.cpp
    core/ValidationTest.cpp
    core/VersionTest.cpp
    data/AppPathsTest.cpp
    data/AvailabilityRepositoryTest.cpp
    data/BackupTest.cpp
    data/CapabilityRepositoryTest.cpp
    data/ClubRepositoryTest.cpp
    data/DatabaseManagerTest.cpp
    data/ImportLogRepositoryTest.cpp
    data/EnumsSchemaTest.cpp
    data/LineupRepositoryTest.cpp
    data/MatchLiveRepositoryTest.cpp
    data/MatchNoteRepositoryTest.cpp
    data/MatchRepositoryTest.cpp
    data/MatchTacticRepositoryTest.cpp
    data/MetricRepositoryTest.cpp
    data/MigrationsTest.cpp
    data/MigrationUpgradeTest.cpp
    data/OpponentRepositoryTest.cpp
    data/PlayerRepositoryTest.cpp
    data/RepositoryFixture.hpp
    data/RepositorySupportTest.cpp
    data/SchemaTest.cpp
    data/StaffRepositoryTest.cpp
    data/SupportedDatesTest.cpp
    data/TacticJsonTest.cpp
    data/TestDatabase.hpp
    data/TrainingRepositoryTest.cpp
    data/TransactionTest.cpp
    data/XlsxReaderTest.cpp
    data/XlsxSpikeTest.cpp
    services/AppLogTest.cpp
    services/AuthServiceTest.cpp
    services/CalendarServiceTest.cpp
    services/CapabilityServiceTest.cpp
    services/DashboardServiceTest.cpp
    services/DemoDataServiceTest.cpp
    services/FakeClock.hpp
    services/ActivityImportServiceTest.cpp
    services/ImportServiceTest.cpp
    services/LineupServiceTest.cpp
    services/LoadServiceTest.cpp
    services/MatchDayServiceTest.cpp
    services/MatchReportServiceTest.cpp
    services/MetricServiceTest.cpp
    services/ServiceFixture.hpp
    services/SessionTest.cpp
    services/SetupServiceTest.cpp
    services/SingleInstanceTest.cpp
    services/SquadServiceTest.cpp
    services/TrainingServiceTest.cpp
)
# QXlsx is linked here only to prove the library itself (P03 spike); app code reaches it
# through fdss_data.
target_link_libraries(fdss_tests PRIVATE fdss_services QXlsx::QXlsx Qt6::Concurrent GTest::gtest)
target_compile_definitions(fdss_tests PRIVATE
    FDSS_EXPECTED_VERSION="${PROJECT_VERSION}"
    FDSS_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    FDSS_IMPORT_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/import"
    FDSS_IMPORT_TEMPLATES_DIR="${PROJECT_SOURCE_DIR}/ui/import-templates"
)
# Test sources contain Polish string literals.
if(MSVC)
    target_compile_options(fdss_tests PRIVATE /utf-8)
endif()
gtest_discover_tests(fdss_tests DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 120)

add_executable(fdss_vm_tests
    test_main.cpp
    services/FakeClock.hpp
    viewmodels/AppStateTest.cpp
    viewmodels/ImportActivityWizardTest.cpp
    viewmodels/AvailabilityViewModelTest.cpp
    viewmodels/CalendarViewModelTest.cpp
    viewmodels/ChangePinViewModelTest.cpp
    viewmodels/DashboardViewModelTest.cpp
    viewmodels/ErrorPresenterTest.cpp
    viewmodels/ImportWizardViewModelTest.cpp
    viewmodels/LoadViewModelTest.cpp
    viewmodels/MatchDayViewModelTest.cpp
    viewmodels/MatchEditorViewModelTest.cpp
    viewmodels/MatchReportViewModelTest.cpp
    viewmodels/MetricViewModelsTest.cpp
    viewmodels/NavigationModelTest.cpp
    viewmodels/PlayerCardViewModelTest.cpp
    viewmodels/PlayerEditorViewModelTest.cpp
    viewmodels/PreMatchViewModelTest.cpp
    viewmodels/ProfilePickerViewModelTest.cpp
    viewmodels/ServiceRegistryTest.cpp
    viewmodels/SessionEditorViewModelTest.cpp
    viewmodels/SetupWizardViewModelTest.cpp
    viewmodels/SquadViewModelTest.cpp
    viewmodels/StaffListModelTest.cpp
    viewmodels/StaffViewModelTest.cpp
    viewmodels/TeamSettingsViewModelTest.cpp
    viewmodels/TrainingSessionViewModelTest.cpp
    viewmodels/ViewModelFixture.hpp
)
target_link_libraries(fdss_vm_tests PRIVATE fdss_ui GTest::gtest)
target_compile_definitions(fdss_vm_tests PRIVATE
    FDSS_IMPORT_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/import"
)
# Test sources contain Polish string literals.
if(MSVC)
    target_compile_options(fdss_vm_tests PRIVATE /utf-8)
endif()
# Everything that links fdss_ui has the CTest label "ui"; the sanitizer job builds fdss_tests only and
# runs `ctest -LE ui`. A plain add_test on purpose: for a target that was not built,
# gtest_discover_tests registers a <target>_NOT_BUILT test without any properties, which -LE would
# then run and fail.
add_test(NAME fdss_vm_tests COMMAND fdss_vm_tests)
set_tests_properties(fdss_vm_tests PROPERTIES LABELS ui)

qt_add_executable(qml_smoke
    qml_smoke/main.cpp
)
target_link_libraries(qml_smoke PRIVATE fdss_ui fdss_uiplugin)
target_compile_definitions(qml_smoke PRIVATE
    FDSS_IMPORT_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data/import"
)
add_test(NAME qml_smoke COMMAND qml_smoke)
set_tests_properties(qml_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS ui)

find_package(Qt6 REQUIRED COMPONENTS QuickTest)
qt_add_executable(qml_components
    qml_components/main.cpp
)
target_link_libraries(qml_components PRIVATE fdss_ui fdss_uiplugin Qt6::QuickTest)
target_compile_definitions(qml_components PRIVATE
    FDSS_QML_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml_components"
)
add_test(NAME qml_components COMMAND qml_components)
set_tests_properties(qml_components PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS ui)
