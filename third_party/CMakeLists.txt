find_package(Vulkan REQUIRED)
include(ExternalProject)

add_library(vkbootstrap STATIC)
add_library(glm INTERFACE)
add_library(vma INTERFACE)

add_library(stb_image INTERFACE)
add_library(json INTERFACE)

add_library(tinyobjloader STATIC)
add_library(spirv_reflect STATIC)

add_library(lz4 STATIC)

add_subdirectory(gltf)

ExternalProject_Add(nvtools
SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/nvidia-texture-tools"
BINARY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/nvlibs"
CMAKE_ARGS -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=${PROJECT_SOURCE_DIR}/libs -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=${PROJECT_SOURCE_DIR}/libs -DCMAKE_CXX_STANDARD=17
)
add_library(nvtt INTERFACE)
target_include_directories(nvtt INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/nvidia-texture-tools/src/nvtt")

#set(nvttfolder "${PROJECT_SOURCE_DIR}/libs/Release")

set(nvttfolder "${PROJECT_SOURCE_DIR}/libs/$<IF:$<CONFIG:Debug>,Debug,Release>")

target_link_libraries(nvtt INTERFACE "${nvttfolder}/nvtt.lib")
target_link_libraries(nvtt INTERFACE "${nvttfolder}/bc6h.lib")
target_link_libraries(nvtt INTERFACE "${nvttfolder}/bc7.lib")
target_link_libraries(nvtt INTERFACE "${nvttfolder}/EtcLib.lib")
target_link_libraries(nvtt INTERFACE "${nvttfolder}/nvcore.lib")
target_link_libraries(nvtt INTERFACE "${nvttfolder}/nvimage.lib")
target_link_libraries(nvtt INTERFACE "${nvttfolder}/nvmath.lib")
target_link_libraries(nvtt INTERFACE "${nvttfolder}/nvsquish.lib")
target_link_libraries(nvtt INTERFACE "${nvttfolder}/nvthread.lib")
target_link_libraries(nvtt INTERFACE "${nvttfolder}/posh.lib")
target_link_libraries(nvtt INTERFACE "${nvttfolder}/rg_etc1.lib")

set(TRACY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tracy" CACHE FILEPATH "Path to Tracy")
add_library(tracy OBJECT "${TRACY_DIR}/TracyClient.cpp")
target_include_directories(tracy PUBLIC "${TRACY_DIR}")
target_compile_definitions(tracy PUBLIC TRACY_ENABLE)

target_sources(vkbootstrap PRIVATE 
    vkbootstrap/VkBootstrap.h
    vkbootstrap/VkBootstrap.cpp
    )

target_include_directories(vkbootstrap PUBLIC vkbootstrap)
target_link_libraries(vkbootstrap PUBLIC Vulkan::Vulkan $<$<BOOL:UNIX>:${CMAKE_DL_LIBS}>)

#both vma and glm and header only libs so we only need the include path
target_include_directories(vma INTERFACE vma)
target_include_directories(glm INTERFACE glm)

target_sources(lz4 PRIVATE 
    "${CMAKE_CURRENT_SOURCE_DIR}/lz4/lz4.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/lz4/lz4.c"
)

target_include_directories(lz4 PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/lz4" )

target_sources(spirv_reflect PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/spirv_reflect/spirv_reflect.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/spirv_reflect/spirv_reflect.c"
)

target_include_directories(spirv_reflect PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/spirv_reflect" )
target_include_directories(spirv_reflect PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/spirv_reflect/include" )

target_sources(tinyobjloader PRIVATE 
    tinyobjloader/tiny_obj_loader.h
    tinyobjloader/tiny_obj_loader.cc
    )

target_include_directories(tinyobjloader PUBLIC tinyobjloader)

target_include_directories(json INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/nlohmann_json" )


add_library(sdl2 INTERFACE)
set(sdl2_DIR "SDL_PATH" CACHE PATH "Path to SDL2")

if (WIN32)
target_include_directories(sdl2 INTERFACE ${sdl2_DIR}/include ${sdl2_image_DIR}/include)
target_link_directories(sdl2 INTERFACE ${sdl2_DIR}/lib/x64 ${sdl2_image_DIR}/lib/x64)
elseif (UNIX)
target_include_directories(sdl2 INTERFACE ${sdl2_DIR}/include/SDL2 ${sdl2_image_DIR}/include/SDL2)
target_link_directories(sdl2 INTERFACE ${sdl2_DIR}/lib ${sdl2_image_DIR}/lib)
endif()

target_link_libraries(sdl2 INTERFACE SDL2 SDL2main)

add_library(imgui STATIC)

target_include_directories(imgui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/imgui")

target_sources(imgui PRIVATE 
    imgui/imgui.h
    imgui/imgui.cpp

    imgui/imgui_stdlib.h
    imgui/imgui_stdlib.cpp
    
    imgui/imgui_demo.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_tables.cpp

    imgui/imgui_impl_vulkan.cpp
    imgui/imgui_impl_sdl.cpp
    )
target_link_libraries(imgui PUBLIC Vulkan::Vulkan sdl2)

target_include_directories(stb_image INTERFACE stb_image)


add_library(fmt_lib STATIC)
target_include_directories(fmt_lib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/fmt_lib")
file(GLOB FMT_FILES "${CMAKE_CURRENT_SOURCE_DIR}/fmt_lib/fmt/*")
target_sources(fmt_lib PRIVATE ${FMT_FILES})


add_library(assimp INTERFACE)

set(assimp_dir "${CMAKE_CURRENT_SOURCE_DIR}/assimp_build")
set(assimp_foldername "$<IF:$<CONFIG:Debug>,Debug,Release>")
set(assimp_filename "assimp-vc142-mt$<IF:$<CONFIG:Debug>,d,>")
set(assimp_dll "${CMAKE_CURRENT_SOURCE_DIR}/assimp_build/bin/${assimp_foldername}/${assimp_filename}.dll")
set(assimp_lib "${CMAKE_CURRENT_SOURCE_DIR}/assimp_build/lib/${assimp_foldername}/${assimp_filename}.lib")
target_include_directories(assimp INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/assimp/include")
target_link_libraries(assimp INTERFACE "${assimp_lib}" )