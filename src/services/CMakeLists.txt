add_library(fdss_services STATIC
    AuthService.hpp
    AuthService.cpp
    CalendarService.hpp
    CalendarService.cpp
    Clock.hpp
    ServiceSupport.hpp
    Session.hpp
    Session.cpp
    SetupService.hpp
    SetupService.cpp
    DemoDataService.hpp
    DemoDataService.cpp
    SquadService.hpp
    SquadService.cpp
    TrainingService.hpp
    TrainingService.cpp
)

# Qt Network is here for QPasswordDigestor alone (ADR-007): the app opens no sockets. PRIVATE keeps
# it out of everything above this layer.
target_link_libraries(fdss_services
    PUBLIC fdss_data
    PRIVATE Qt6::Network
)
