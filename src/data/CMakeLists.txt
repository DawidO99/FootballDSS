add_library(fdss_data STATIC
    DataPlaceholder.cpp
)

target_link_libraries(fdss_data PUBLIC fdss_core)
