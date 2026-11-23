add_library(flora-geometry
    src/branch_mesh.cpp
    src/foliage_cards.cpp
    src/foliage_mesh.cpp
    src/lod.cpp
    src/material.cpp
    src/mesh.cpp)

add_library(flora::geometry ALIAS flora-geometry)

target_include_directories(flora-geometry PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# Vendored nlohmann/json (MIT), shared with flora-core: material files only.
target_include_directories(flora-geometry SYSTEM PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../flora-core/vendor)

target_link_libraries(flora-geometry
    PUBLIC flora::core
    PRIVATE flora_warnings)
