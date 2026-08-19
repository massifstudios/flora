add_executable(flora-bench bench_main.cpp)

target_link_libraries(flora-bench PRIVATE flora::core flora::geometry flora_warnings)
