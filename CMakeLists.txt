cmake_minimum_required(VERSION 3.20)
project(WeatherSystem)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMakePredefinedTargets")

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw)

set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/glm)

add_library(glad STATIC third_party/opengl/glad.c)
target_include_directories(glad PUBLIC third_party/opengl)

add_library(imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC third_party/imgui third_party/imgui/backends)
target_link_libraries(imgui PUBLIC glfw glad)

set_target_properties(glfw glad imgui update_mappings PROPERTIES FOLDER "ThirdParty")

include_directories(src)

file(GLOB_RECURSE SOURCES "src/*.cpp")
file(GLOB_RECURSE HEADERS "src/*.h")

add_executable(WeatherSystem ${SOURCES} ${HEADERS})

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT WeatherSystem)

source_group(TREE ${CMAKE_SOURCE_DIR} FILES ${SOURCES} ${HEADERS})

target_link_libraries(WeatherSystem PRIVATE glfw glm::glm imgui glad legacy_stdio_definitions pdh)