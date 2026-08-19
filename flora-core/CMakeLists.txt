add_library(flora-core
    src/environment/shadow_grid.cpp
    src/growth/simulation.cpp
    src/species/species.cpp
    src/species/species_io.cpp
    src/structure/stem_graph.cpp)

add_library(flora::core ALIAS flora-core)

target_include_directories(flora-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# Vendored nlohmann/json (MIT): implementation detail of species_io.cpp only.
target_include_directories(flora-core SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor)

target_link_libraries(flora-core PRIVATE flora_warnings)
