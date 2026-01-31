# Overlay2D Demo example
# Demonstrates 2D overlay rendering for UI elements

add_executable(overlay_demo
    main.cpp
)

# Link against both core and engine libraries
target_link_libraries(overlay_demo PRIVATE finevk-core finevk-engine)

# Copy assets (fonts) to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/assets DESTINATION ${CMAKE_CURRENT_BINARY_DIR})
