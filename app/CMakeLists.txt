find_package(Vulkan REQUIRED)

project(voxel_app)

add_executable(${PROJECT_NAME}
main.cpp
voxels/Terrain.cpp
Util.cpp
VoxelRenderer.cpp
voxels/Mesher.cpp
voxels/Chunk.cpp
ChunkMeshManager.cpp
StagingBufferPool.cpp
)

target_compile_definitions(${PROJECT_NAME} PRIVATE WORKING_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_precompile_headers(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/pch.hpp)

# target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_SOURCE_DIR}/engine")
#
# target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(${PROJECT_NAME} PRIVATE engine FastNoise tracy)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${PROJECT_NAME} PUBLIC -Wall -Wextra -Werror -Wno-deprecated-declarations -Wno-unused-function -Wnan-infinity-disabled)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(${PROJECT_NAME} PUBLIC /W4 /WX)
    endif()
endif()
