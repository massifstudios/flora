add_executable(flora-cli src/main.cpp)

target_link_libraries(flora-cli PRIVATE flora::core flora_warnings)

set_target_properties(flora-cli PROPERTIES OUTPUT_NAME flora)
