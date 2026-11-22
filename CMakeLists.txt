cmake_minimum_required(VERSION 3.28)

project(FootballDSS VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.12 REQUIRED COMPONENTS
    Core Gui Network Qml Quick QuickControls2 Sql Concurrent Graphs LinguistTools
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

set_source_files_properties(
    ui/theme/Theme.qml
    ui/components/Toasts.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(fdss_ui
    URI FootballDSS
    VERSION 1.0
    QML_FILES
        ui/Main.qml
        ui/theme/Theme.qml
        ui/components/AppDialog.qml
        ui/components/AppTabs.qml
        ui/components/AttendanceGrid.qml
        ui/components/AttendanceGridHeading.qml
        ui/components/AttendanceGridRow.qml
        ui/components/BenchList.qml
        ui/components/AvailabilityDialog.qml
        ui/components/CalendarWeekGrid.qml
        ui/components/Card.qml
        ui/components/ChartLegend.qml
        ui/components/ChartTooltip.qml
        ui/components/CheckBoxField.qml
        ui/components/DangerButton.qml
        ui/components/DashboardCard.qml
        ui/components/DashboardRow.qml
        ui/components/DataTable.qml
        ui/components/DataTableCell.qml
        ui/components/DateField.qml
        ui/components/EmptyState.qml
        ui/components/FieldBackground.qml
        ui/components/FocusFlickable.qml
        ui/components/FormField.qml
        ui/components/Icon.qml
        ui/components/ImportMappingTable.qml
        ui/components/ImportReportTable.qml
        ui/components/ImportStepIndicator.qml
        ui/components/IconButton.qml
        ui/components/LanguageThemeSwitch.qml
        ui/components/LineupIssuesPanel.qml
        ui/components/LineupPlayerList.qml
        ui/components/LoadChart.qml
        ui/components/LoadStatusBadge.qml
        ui/components/LockedState.qml
        ui/components/MatchClockBar.qml
        ui/components/MatchCorrectionDialog.qml
        ui/components/MatchEditorDialog.qml
        ui/components/MatchNoteDialog.qml
        ui/components/MatchPlayerTile.qml
        ui/components/MatchRatingsGrid.qml
        ui/components/MatchRatingsGridRow.qml
        ui/components/MatchStartDialog.qml
        ui/components/MatchStatusBadge.qml
        ui/components/MatchTimeline.qml
        ui/components/MetricDefinitionDialog.qml
        ui/components/MetricTrend.qml
        ui/components/MetricValueCell.qml
        ui/components/MetricValueGrid.qml
        ui/components/MetricValueGridRow.qml
        ui/components/MinutesCell.qml
        ui/components/MultiSelectField.qml
        ui/components/NumberField.qml
        ui/components/PageHeader.qml
        ui/components/PinField.qml
        ui/components/PinPad.qml
        ui/components/PlainToolTip.qml
        ui/components/PlayerEditorDialog.qml
        ui/components/PlayerLoadPanel.qml
        ui/components/PlayerToken.qml
        ui/components/PlayerStatusBadge.qml
        ui/components/PrimaryButton.qml
        ui/components/ProgressLine.qml
        ui/components/ProfileTile.qml
        ui/components/RpeCell.qml
        ui/components/SearchField.qml
        ui/components/SecondaryButton.qml
        ui/components/SegmentedControl.qml
        ui/components/SelectField.qml
        ui/components/SessionEditorDialog.qml
        ui/components/Sidebar.qml
        ui/components/SidebarButton.qml
        ui/components/SlotPickerDialog.qml
        ui/components/StatusBadge.qml
        ui/components/SuggestionField.qml
        ui/components/TacticsPitch.qml
        ui/components/TextAreaField.qml
        ui/components/TextInputField.qml
        ui/components/TimeField.qml
        ui/components/ToastHost.qml
        ui/components/Toasts.qml
        ui/components/TopBar.qml
        ui/views/CalendarView.qml
        ui/views/ComponentGalleryView.qml
        ui/views/DashboardView.qml
        ui/views/DatabaseErrorView.qml
        ui/views/ForcedPinChangeView.qml
        ui/views/ImportView.qml
        ui/views/LoadView.qml
        ui/views/MatchDayView.qml
        ui/views/MatchReportView.qml
        ui/views/MetricsView.qml
        ui/views/PlayerCardView.qml
        ui/views/PreMatchView.qml
        ui/views/ProfilePickerView.qml
        ui/views/SettingsView.qml
        ui/views/SetupWizardView.qml
        ui/views/SpikeChartsView.qml
        ui/views/SquadView.qml
        ui/views/TrainingView.qml
    SOURCES
        src/viewmodels/AppSettings.hpp
        src/viewmodels/AppSettings.cpp
        src/viewmodels/AppState.hpp
        src/viewmodels/AppState.cpp
        src/viewmodels/AvailabilityListModel.hpp
        src/viewmodels/AvailabilityListModel.cpp
        src/viewmodels/AvailabilityViewModel.hpp
        src/viewmodels/AvailabilityViewModel.cpp
        src/viewmodels/CalendarListModel.hpp
        src/viewmodels/CalendarListModel.cpp
        src/viewmodels/CalendarTexts.hpp
        src/viewmodels/CalendarTexts.cpp
        src/viewmodels/CalendarViewModel.hpp
        src/viewmodels/CalendarViewModel.cpp
        src/viewmodels/CalendarWeekModel.hpp
        src/viewmodels/CalendarWeekModel.cpp
        src/viewmodels/CapabilityModel.hpp
        src/viewmodels/CapabilityModel.cpp
        src/viewmodels/ChangePinViewModel.hpp
        src/viewmodels/ChangePinViewModel.cpp
        src/viewmodels/DashboardViewModel.hpp
        src/viewmodels/DashboardViewModel.cpp
        src/viewmodels/ErrorPresenter.hpp
        src/viewmodels/ErrorPresenter.cpp
        src/viewmodels/ImportActivityRows.hpp
        src/viewmodels/ImportActivityRows.cpp
        src/viewmodels/ImportTexts.hpp
        src/viewmodels/ImportTexts.cpp
        src/viewmodels/ImportWizardViewModel.hpp
        src/viewmodels/ImportWizardViewModel.cpp
        src/viewmodels/IsoDates.hpp
        src/viewmodels/JsGarbageCollector.hpp
        src/viewmodels/IsoDates.cpp
        src/viewmodels/LanguageManager.hpp
        src/viewmodels/LanguageManager.cpp
        src/viewmodels/LineupModels.hpp
        src/viewmodels/LineupModels.cpp
        src/viewmodels/LineupTexts.hpp
        src/viewmodels/LineupTexts.cpp
        src/viewmodels/LoadFilterModel.hpp
        src/viewmodels/LoadFilterModel.cpp
        src/viewmodels/LoadListModel.hpp
        src/viewmodels/LoadListModel.cpp
        src/viewmodels/LoadTexts.hpp
        src/viewmodels/LoadTexts.cpp
        src/viewmodels/LoadViewModel.hpp
        src/viewmodels/LoadViewModel.cpp
        src/viewmodels/LockCountdown.hpp
        src/viewmodels/LockCountdown.cpp
        src/viewmodels/MatchDayModels.hpp
        src/viewmodels/MatchDayModels.cpp
        src/viewmodels/MatchDayTexts.hpp
        src/viewmodels/MatchDayTexts.cpp
        src/viewmodels/MatchDayViewModel.hpp
        src/viewmodels/MatchDayViewModel.cpp
        src/viewmodels/MatchEditorViewModel.hpp
        src/viewmodels/MatchEditorViewModel.cpp
        src/viewmodels/MatchReportModels.hpp
        src/viewmodels/MatchReportModels.cpp
        src/viewmodels/MatchReportViewModel.hpp
        src/viewmodels/MatchReportViewModel.cpp
        src/viewmodels/MatchTimelineRows.hpp
        src/viewmodels/MatchTimelineRows.cpp
        src/viewmodels/MetricDefinitionListModel.hpp
        src/viewmodels/MetricDefinitionListModel.cpp
        src/viewmodels/MetricDefinitionsViewModel.hpp
        src/viewmodels/MetricDefinitionsViewModel.cpp
        src/viewmodels/MetricEntryViewModel.hpp
        src/viewmodels/MetricEntryViewModel.cpp
        src/viewmodels/MetricTexts.hpp
        src/viewmodels/MetricTexts.cpp
        src/viewmodels/NavigationModel.hpp
        src/viewmodels/NavigationModel.cpp
        src/viewmodels/OpponentListModel.hpp
        src/viewmodels/OpponentListModel.cpp
        src/viewmodels/PlayerCardViewModel.hpp
        src/viewmodels/PlayerCardViewModel.cpp
        src/viewmodels/PlayerEditorViewModel.hpp
        src/viewmodels/PlayerEditorViewModel.cpp
        src/viewmodels/PlayerLoadViewModel.hpp
        src/viewmodels/PlayerLoadViewModel.cpp
        src/viewmodels/PlayerMatchesModel.hpp
        src/viewmodels/PlayerMatchesModel.cpp
        src/viewmodels/PlayerMetricsViewModel.hpp
        src/viewmodels/PlayerMetricsViewModel.cpp
        src/viewmodels/QmlValues.hpp
        src/viewmodels/PreMatchViewModel.hpp
        src/viewmodels/PreMatchViewModel.cpp
        src/viewmodels/ProfilePickerViewModel.hpp
        src/viewmodels/ProfilePickerViewModel.cpp
        src/viewmodels/RowsModel.hpp
        src/viewmodels/RowsModel.cpp
        src/viewmodels/ServiceRegistry.hpp
        src/viewmodels/ServiceRegistry.cpp
        src/viewmodels/SessionEditorViewModel.hpp
        src/viewmodels/SessionEditorViewModel.cpp
        src/viewmodels/SessionPickerModel.hpp
        src/viewmodels/SessionPickerModel.cpp
        src/viewmodels/SetupWizardViewModel.hpp
        src/viewmodels/SetupWizardViewModel.cpp
        src/viewmodels/SquadFilterModel.hpp
        src/viewmodels/SquadFilterModel.cpp
        src/viewmodels/SquadListModel.hpp
        src/viewmodels/SquadListModel.cpp
        src/viewmodels/SquadTexts.hpp
        src/viewmodels/SquadTexts.cpp
        src/viewmodels/SquadViewModel.hpp
        src/viewmodels/SquadViewModel.cpp
        src/viewmodels/StaffListModel.hpp
        src/viewmodels/StaffListModel.cpp
        src/viewmodels/StaffViewModel.hpp
        src/viewmodels/StaffViewModel.cpp
        src/viewmodels/StartupViewModel.hpp
        src/viewmodels/StartupViewModel.cpp
        src/viewmodels/TeamSettingsViewModel.hpp
        src/viewmodels/TeamSettingsViewModel.cpp
        src/viewmodels/TrainingSessionViewModel.hpp
        src/viewmodels/TrainingSessionViewModel.cpp
)

# The templates of the import (packet P16), saved by ImportWizardViewModel::saveTemplate().
qt_add_resources(fdss_ui "fdss_import_templates"
    PREFIX "/fdss/import-templates"
    BASE ui/import-templates
    FILES
        ui/import-templates/matches.csv
        ui/import-templates/matches.xlsx
        ui/import-templates/squad.csv
        ui/import-templates/squad.xlsx
        ui/import-templates/training.csv
        ui/import-templates/training.xlsx
)

target_link_libraries(fdss_ui PUBLIC fdss_services Qt6::Quick Qt6::QuickControls2 Qt6::Graphs)
# The generated type registration includes viewmodel headers by file name.
target_include_directories(fdss_ui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodels")

# No line numbers in the .ts files: every moved line would otherwise show up there, and parallel
# packets would conflict over them.
qt_add_translations(fdss_ui
    TS_FILES
        i18n/FootballDSS_pl.ts
        i18n/FootballDSS_en.ts
    LUPDATE_OPTIONS
        -locations none
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
