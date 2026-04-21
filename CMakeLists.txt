cmake_minimum_required(VERSION 3.15)

project(VK_DDGI LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)


IF(WIN32)
	IF (NOT Vulkan_FOUND)
		find_library(Vulkan_LIBRARY NAMES vulkan-1 vulkan PATHS ${CMAKE_SOURCE_DIR}/libs/vulkan)
		IF (Vulkan_LIBRARY)
			set(Vulkan_FOUND ON)
			MESSAGE("Using bundled Vulkan library version")
		ENDIF()
	ENDIF()
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DVK_USE_PLATFORM_WIN32_KHR")
ENDIF(WIN32)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

find_package(Vulkan REQUIRED)
message(STATUS "Vulkan found: ${Vulkan_FOUND}")
find_program(GLSLC_EXECUTABLE glslc REQUIRED)
message(STATUS "glslc found: ${GLSLC_EXECUTABLE}")

set(SHADER_SOURCE_DIR ${CMAKE_SOURCE_DIR}/shaders/glsl)
file(GLOB_RECURSE SHADER_SOURCES CONFIGURE_DEPENDS
    "${SHADER_SOURCE_DIR}/*.vert"
    "${SHADER_SOURCE_DIR}/*.frag"
    "${SHADER_SOURCE_DIR}/*.comp"
    "${SHADER_SOURCE_DIR}/*.rgen"
    "${SHADER_SOURCE_DIR}/*.rmiss"
    "${SHADER_SOURCE_DIR}/*.rchit"
    "${SHADER_SOURCE_DIR}/*.rahit"
)
foreach(SHADER ${SHADER_SOURCES})
    set(SHADER_SPIRV "${SHADER}.spv")

    add_custom_command(
        OUTPUT ${SHADER_SPIRV}
        COMMAND ${GLSLC_EXECUTABLE} --target-env=vulkan1.3 ${SHADER} -o ${SHADER_SPIRV}
        DEPENDS ${SHADER}
        COMMENT "Compiling ${SHADER} to SPIR-V"
    )

    list(APPEND SPIRV_FILES ${SHADER_SPIRV})
endforeach()

add_custom_target(
    Shaders ALL
    DEPENDS ${SPIRV_FILES}
    SOURCES ${SHADER_SOURCES}
)

link_directories(${CMAKE_SOURCE_DIR}/external/glfw/lib)

include_directories(${CMAKE_SOURCE_DIR}/external/stb)
include_directories(${CMAKE_SOURCE_DIR}/external/imgui)
include_directories(${CMAKE_SOURCE_DIR}/external/tinygltf)
include_directories(${CMAKE_SOURCE_DIR}/external/glfw/include)
include_directories(${CMAKE_SOURCE_DIR}/external/glm)
include_directories(${CMAKE_SOURCE_DIR}/external/ktx/include)
include_directories(${CMAKE_SOURCE_DIR}/src/vulkan_base)
include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${Vulkan_INCLUDE_DIRS})

file(GLOB_RECURSE HEADER_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/include/*.hpp"
    "${CMAKE_SOURCE_DIR}/include/*.h")
file(GLOB_RECURSE SOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/*.cpp")
list(FILTER SOURCE_FILES EXCLUDE REGEX ".*/src/vulkan_base/.*")

source_group(TREE "${CMAKE_SOURCE_DIR}/include" PREFIX "Header Files" FILES ${HEADER_FILES})
source_group(TREE "${CMAKE_SOURCE_DIR}/src" PREFIX "Source Files" FILES ${SOURCE_FILES})
source_group(TREE "${SHADER_SOURCE_DIR}" PREFIX "Shaders" FILES ${SHADER_SOURCES})

add_definitions(-DVKM_ASSETS_DIR=\"${CMAKE_SOURCE_DIR}/assets/\")
add_definitions(-DVKM_SHADERS_DIR=\"${CMAKE_SOURCE_DIR}/shaders/\")

if (MSVC)
    add_compile_options(/std:c++latest)
endif()

add_subdirectory(src/vulkan_base)
if(WIN32)
	add_executable(${PROJECT_NAME} WIN32 ${HEADER_FILES} ${SOURCE_FILES})
	target_link_libraries(${PROJECT_NAME} vulkan_base ${Vulkan_LIBRARY} ${WINLIBS} glfw3.lib)
else(WIN32)
	add_executable(${PROJECT_NAME} ${HEADER_FILES} ${SOURCE_FILES})
		target_link_libraries(${PROJECT_NAME} vulkan_base glfw3.lib)
endif(WIN32)
add_dependencies(${PROJECT_NAME} Shaders)
