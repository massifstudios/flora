# Viewer stack (M8): SDL3 window/input + fixed-function OpenGL with
# client-side vertex arrays. No extension loader, no vendored UI deps; the
# viewer is optional and skipped when SDL3/OpenGL are absent.
find_package(SDL3 CONFIG QUIET)
find_package(OpenGL QUIET)

if(NOT SDL3_FOUND OR NOT OpenGL_FOUND)
    message(STATUS "flora-viewer: SDL3 or OpenGL not found, skipping viewer target")
    return()
endif()

add_executable(flora-viewer src/main.cpp)

target_link_libraries(flora-viewer
    PRIVATE flora::core flora::geometry SDL3::SDL3 OpenGL::GL flora_warnings)

# Impostor bake writes its atlas with stb_image_write. Vendored rather than
# found: it is one public-domain header, and the viewer is the only thing in the
# tree that writes an image. SYSTEM so the repository's warning level, which the
# header does not compile clean under, stops at our own code.
target_include_directories(flora-viewer SYSTEM PRIVATE vendor)

# Colour maps for the solid view. Optional: without libpng the viewer runs
# untextured instead of dropping out of the build.
find_package(PNG QUIET)
if(PNG_FOUND)
    target_link_libraries(flora-viewer PRIVATE PNG::PNG)
    target_compile_definitions(flora-viewer PRIVATE FLORA_VIEWER_PNG)
else()
    message(STATUS "flora-viewer: libpng not found, PNG textures disabled")
endif()

find_package(JPEG QUIET)
if(JPEG_FOUND)
    target_link_libraries(flora-viewer PRIVATE JPEG::JPEG)
    target_compile_definitions(flora-viewer PRIVATE FLORA_VIEWER_JPEG)
else()
    message(STATUS "flora-viewer: libjpeg not found, JPEG textures disabled")
endif()

set_target_properties(flora-viewer PROPERTIES OUTPUT_NAME flora-viewer)
