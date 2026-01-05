# Tests CMakeLists.txt

# Phase 1 test - Core foundation
add_executable(test_phase1
    test_phase1.cpp
)

target_link_libraries(test_phase1 PRIVATE finevk-core)

# Phase 2 test - Device & Memory Management
add_executable(test_phase2
    test_phase2.cpp
)

target_link_libraries(test_phase2 PRIVATE finevk-core)

# Phase 3 test - Rendering Infrastructure
add_executable(test_phase3
    test_phase3.cpp
)

target_link_libraries(test_phase3 PRIVATE finevk-core)

# Copy any required resources
# (none for Phase 1/2/3)
