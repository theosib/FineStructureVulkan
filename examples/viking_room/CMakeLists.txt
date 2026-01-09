# Viking Room example - MSAA demo

add_executable(viking_room
    main.cpp
)

target_link_libraries(viking_room PRIVATE finevk-core)

# Compile shaders for this example
compile_shaders(viking_room
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders
    ${CMAKE_CURRENT_BINARY_DIR}/shaders
)

# Copy assets to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
