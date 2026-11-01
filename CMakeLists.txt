cmake_minimum_required(VERSION 3.28)

project(FootballDSS VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.12 REQUIRED COMPONENTS
    Core Gui Qml Quick QuickControls2 Sql Concurrent Graphs LinguistTools
)
qt_standard_project_setup(REQUIRES 6.12)

add_subdirectory(src/core)
add_subdirectory(src/data)
add_subdirectory(src/services)

# QML modules go to build/<preset>/qml, so the module directory "FootballDSS"
# does not collide with the executable of the same name.
set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/qml")

# Backing library of the QML module: viewmodels, QML, theme and translations.
# Shared by the app and qml_smoke.
qt_add_library(fdss_ui STATIC)

set_source_files_properties(ui/theme/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(fdss_ui
    URI FootballDSS
    VERSION 1.0
    QML_FILES
        ui/Main.qml
        ui/theme/Theme.qml
        ui/components/EmptyState.qml
        ui/components/Icon.qml
        ui/components/PageHeader.qml
        ui/components/PrimaryButton.qml
        ui/components/Sidebar.qml
        ui/components/SidebarButton.qml
        ui/components/StatusBadge.qml
        ui/components/TopBar.qml
        ui/views/CalendarView.qml
        ui/views/DashboardView.qml
        ui/views/DatabaseErrorView.qml
        ui/views/ImportView.qml
        ui/views/LoadView.qml
        ui/views/MatchDayView.qml
        ui/views/PreMatchView.qml
        ui/views/SettingsView.qml
        ui/views/SpikeChartsView.qml
        ui/views/SquadView.qml
        ui/views/TrainingView.qml
    SOURCES
        src/viewmodels/AppSettings.hpp
        src/viewmodels/AppSettings.cpp
        src/viewmodels/LanguageManager.hpp
        src/viewmodels/LanguageManager.cpp
        src/viewmodels/NavigationModel.hpp
        src/viewmodels/NavigationModel.cpp
        src/viewmodels/StartupViewModel.hpp
        src/viewmodels/StartupViewModel.cpp
)

target_link_libraries(fdss_ui PUBLIC fdss_services Qt6::Quick Qt6::QuickControls2 Qt6::Graphs)
# The generated type registration includes viewmodel headers by file name.
target_include_directories(fdss_ui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodels")

qt_add_translations(fdss_ui
    TS_FILES
        i18n/FootballDSS_pl.ts
        i18n/FootballDSS_en.ts
)

qt_add_executable(FootballDSS WIN32 MACOSX_BUNDLE
    src/main.cpp
)
target_link_libraries(FootballDSS PRIVATE fdss_ui fdss_uiplugin)

enable_testing()
add_subdirectory(tests)

# AGENTS.md section 4: the core layer never depends on Qt.
get_target_property(fdss_core_links fdss_core LINK_LIBRARIES)
if(fdss_core_links MATCHES "Qt")
    message(FATAL_ERROR "fdss_core must not link any Qt target (found: ${fdss_core_links})")
endif()
