# Tests CMakeLists.txt

# Phase 1 test - Core foundation
add_executable(test_phase1
    test_phase1.cpp
)

target_link_libraries(test_phase1 PRIVATE finevk-core)

# Copy any required resources
# (none for Phase 1)
