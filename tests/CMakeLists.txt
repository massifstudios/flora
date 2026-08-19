add_executable(flora-tests
    test_main.cpp
    test_species.cpp
    test_branch_mesh.cpp
    test_growth.cpp
    test_rng.cpp
    test_stem_graph.cpp)

target_link_libraries(flora-tests PRIVATE flora::core flora::geometry flora_warnings)

# SYSTEM include: vendored doctest must not trip project warnings-as-errors.
target_include_directories(flora-tests SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor)

add_test(NAME flora-tests COMMAND flora-tests)
