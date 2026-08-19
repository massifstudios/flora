add_library(flora-geometry
    src/branch_mesh.cpp
    src/foliage_mesh.cpp
    src/mesh.cpp)

add_library(flora::geometry ALIAS flora-geometry)

target_include_directories(flora-geometry PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(flora-geometry
    PUBLIC flora::core
    PRIVATE flora_warnings)
