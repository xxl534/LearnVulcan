
# Add source to this project's executable.
file(GLOB ENGINE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_executable(vulkan_guide ${ENGINE_FILES})


set_property(TARGET vulkan_guide PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:vulkan_guide>")

target_include_directories(vulkan_guide PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(vulkan_guide PUBLIC TRACY_ENABLE)
target_compile_definitions(vulkan_guide PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FOR_LEFT_HANDED)

target_precompile_headers(vulkan_guide PUBLIC "vk_types.h" "<unordered_map>" "<vector>" "<iostream>" "<fstream>" "<string>")

target_link_libraries(vulkan_guide vkbootstrap vma glm tinyobjloader imgui stb_image)

target_link_libraries(vulkan_guide Vulkan::Vulkan sdl2)

add_dependencies(vulkan_guide Shaders)
