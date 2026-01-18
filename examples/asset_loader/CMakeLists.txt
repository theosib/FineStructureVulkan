add_executable(asset_loader_example main.cpp)

target_link_libraries(asset_loader_example PRIVATE
    finevk-core
    finevk-engine
)

# Copy to output directory for easy execution
set_target_properties(asset_loader_example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Lifecycle test
add_executable(asset_loader_lifecycle_test test_lifecycle.cpp)

target_link_libraries(asset_loader_lifecycle_test PRIVATE
    finevk-core
    finevk-engine
)

set_target_properties(asset_loader_lifecycle_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
