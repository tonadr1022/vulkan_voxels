add_subdirectory(tvk)
project(engine)

find_package(Vulkan REQUIRED)
# set(CMAKE_POSITION_INDEPENDENT_CODE ON)
# set(BUILD_SHARED_LIBS ON)

add_library(${PROJECT_NAME}
application/Renderer.cpp
application/Window.cpp
application/Camera.cpp
application/ThreadPool.cpp
application/CVar.cpp
application/Util.cpp
)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_SOURCE_DIR}/${PROJECT_NAME}")
target_compile_definitions(${PROJECT_NAME} PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_compile_definitions(${PROJECT_NAME} PRIVATE WORKING_DIR="${CMAKE_SOURCE_DIR}")
target_precompile_headers(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/${PROJECT_NAME}/pch.hpp")

target_link_libraries(${PROJECT_NAME} PUBLIC
    tvk
    VulkanMemoryAllocator
    fmt::fmt
    glm
    Vulkan::Vulkan
    stb_image
    SDL3::SDL3
    vk-bootstrap::vk-bootstrap
    imgui_backend
    tracy
    spirv_reflect
    bs_thread_pool
)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${PROJECT_NAME} PUBLIC -Wall -Wextra -Werror -Wno-deprecated-declarations -Wno-unused-function)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(${PROJECT_NAME} PUBLIC /W4 /WX)
    endif()
endif()
