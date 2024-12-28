find_package(Vulkan REQUIRED)
project(tvk)

# set(CMAKE_POSITION_INDEPENDENT_CODE ON)

add_library(${PROJECT_NAME}
Barrier.cpp
DeletionQueue.cpp
Descriptors.cpp
Error.cpp
Initializers.cpp
Image.cpp
Pipeline.cpp
Resource.cpp
SamplerCache.cpp
Shader.cpp
ShaderWatcher.cpp
Swapchain.cpp
Util.cpp
VMA.cpp
)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 20)
set_source_files_properties(VMA.cpp PROPERTIES COMPILE_FLAGS "-w")

target_link_libraries(${PROJECT_NAME} PUBLIC
fmt::fmt
VulkanMemoryAllocator
glm
Vulkan::Vulkan
SDL3::SDL3
vk-bootstrap::vk-bootstrap
tracy
spirv_reflect
bs_thread_pool
)

target_compile_definitions(${PROJECT_NAME} PRIVATE WORKING_DIR="${CMAKE_SOURCE_DIR}")
target_precompile_headers(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.hpp)
target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_PROJECT_DIR}")
target_compile_definitions(${PROJECT_NAME} PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)
