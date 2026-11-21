add_library(fdss_services STATIC
    AuthService.hpp
    AuthService.cpp
    CalendarService.hpp
    CalendarService.cpp
    CapabilityService.hpp
    CapabilityService.cpp
    DashboardService.hpp
    DashboardService.cpp
    Clock.hpp
    ServiceSupport.hpp
    Session.hpp
    Session.cpp
    SetupService.hpp
    SetupService.cpp
    DemoDataService.hpp
    DemoDataService.cpp
    LineupService.hpp
    LineupService.cpp
    MatchDayService.hpp
    MatchDayService.cpp
    MatchRecompute.hpp
    MatchRecompute.cpp
    MatchReportService.hpp
    MatchReportService.cpp
    LoadService.hpp
    LoadService.cpp
    SquadService.hpp
    SquadService.cpp
    TrainingService.hpp
    TrainingService.cpp
)

# Qt Network is here for QPasswordDigestor alone (ADR-007): the app opens no sockets. PRIVATE keeps
# it out of everything above this layer. Qt Concurrent runs the load computation (AGENTS section 5).
target_link_libraries(fdss_services
    PUBLIC fdss_data
    PRIVATE Qt6::Network Qt6::Concurrent
)
