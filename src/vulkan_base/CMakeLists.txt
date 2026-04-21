file(GLOB BASE_SOURCE_FILES CONFIGURE_DEPENDS "*.cpp")
file(GLOB BASE_HEADER_FILES CONFIGURE_DEPENDS "*.hpp" "*.h")
file(GLOB IMGUI_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/external/imgui/*.cpp")

set(KTX_DIR ${CMAKE_SOURCE_DIR}/external/ktx)
set(KTX_SOURCES
    ${KTX_DIR}/lib/texture.c
    ${KTX_DIR}/lib/hashlist.c
    ${KTX_DIR}/lib/checkheader.c
    ${KTX_DIR}/lib/swap.c
    ${KTX_DIR}/lib/memstream.c
    ${KTX_DIR}/lib/filestream.c
    ${KTX_DIR}/lib/vkloader.c)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Header Files" FILES ${BASE_HEADER_FILES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Source Files" FILES ${BASE_SOURCE_FILES})
source_group("External/imgui" FILES ${IMGUI_SOURCES})
source_group("External/ktx" FILES ${KTX_SOURCES})

add_library(vulkan_base STATIC
    ${BASE_HEADER_FILES}
    ${BASE_SOURCE_FILES}
    ${IMGUI_SOURCES}
    ${KTX_SOURCES})
target_include_directories(vulkan_base
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/external/stb
        ${CMAKE_SOURCE_DIR}/external/imgui
        ${CMAKE_SOURCE_DIR}/external/tinygltf
        ${CMAKE_SOURCE_DIR}/external/glfw/include
        ${CMAKE_SOURCE_DIR}/external/glm
        ${CMAKE_SOURCE_DIR}/external/ktx/include
        ${Vulkan_INCLUDE_DIRS})

if(WIN32)
    target_link_libraries(vulkan_base ${Vulkan_LIBRARY} ${WINLIBS})
 else(WIN32)
    target_link_libraries(vulkan_base ${Vulkan_LIBRARY} ${XCB_LIBRARIES} ${WAYLAND_CLIENT_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
endif(WIN32)
