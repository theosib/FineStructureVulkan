# Hello Triangle example
# This is a placeholder - will be expanded when Layer 3 is complete

add_executable(hello_triangle
    main.cpp
)

target_link_libraries(hello_triangle PRIVATE finevk-core)

# Compile shaders for this example
# compile_shaders(hello_triangle
#     ${CMAKE_CURRENT_SOURCE_DIR}/shaders
#     ${CMAKE_CURRENT_BINARY_DIR}/shaders
# )
