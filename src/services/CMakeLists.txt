add_library(fdss_services STATIC
    ServicesPlaceholder.cpp
)

target_link_libraries(fdss_services PUBLIC fdss_data)
