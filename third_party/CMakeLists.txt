find_package(Vulkan REQUIRED)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(BUILD_SHARED_LIBS ON)


# TODO: only during debug include Tracy
add_compile_definitions(TRACY_ENABLE)
option(TRACY_ENABLE "" ON)
add_library(tracy STATIC tracy/public/TracyClient.cpp)
target_include_directories(tracy PUBLIC tracy/public)
target_compile_definitions(tracy PUBLIC TRACY_ENABLE TRACY_ONLY_LOCALHOST)
# if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
# endif()
get_target_property(tracy_include_dirs tracy INTERFACE_INCLUDE_DIRECTORIES)
set_target_properties(tracy PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${tracy_include_dirs}")

add_subdirectory(glm)
add_subdirectory(vk-bootstrap)
add_subdirectory(SDL)
add_subdirectory(VulkanMemoryAllocator)
add_subdirectory(fmt)

set(FASTNOISE2_NOISETOOL OFF CACHE BOOL "Build Noise Tool" FORCE)
add_subdirectory(FastNoise2)

add_library(bs_thread_pool INTERFACE)
target_include_directories(bs_thread_pool SYSTEM INTERFACE bs_thread_pool)
add_library(stb_image INTERFACE)
target_include_directories(stb_image SYSTEM INTERFACE stb_image)
add_library(spirv_reflect STATIC spirv-reflect/spirv_reflect.c)
target_include_directories(spirv_reflect SYSTEM INTERFACE spirv-reflect)

set(IMGUI_BACKEND_SRC
    imgui/backends/imgui_impl_vulkan.cpp
    imgui/backends/imgui_impl_sdl3.cpp
    imgui/misc/cpp/imgui_stdlib.cpp
)
set(IMGUI_BACKEND_HEADERS
    imgui/backends/imgui_impl_vulkan.h
    imgui/backends/imgui_impl_sdl3.h
    imgui/misc/cpp/imgui_stdlib.h
)
add_library(imgui_backend STATIC
    ${IMGUI_BACKEND_SRC}
    ${IMGUI_BACKEND_HEADERS}
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_tables.cpp
)
target_include_directories(imgui_backend PUBLIC
    imgui
    imgui/backends
    imgui/misc/cpp
)

target_link_libraries(imgui_backend PUBLIC
    Vulkan::Vulkan
    SDL3::SDL3
)
