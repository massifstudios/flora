add_library(flora-core
    src/structure/stem_graph.cpp)

add_library(flora::core ALIAS flora-core)

target_include_directories(flora-core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(flora-core PRIVATE flora_warnings)
