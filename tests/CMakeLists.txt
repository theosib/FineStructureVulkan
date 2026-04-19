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

# Phase 4 test - High-Level Abstractions
add_executable(test_phase4
    test_phase4.cpp
)

target_link_libraries(test_phase4 PRIVATE finevk-core)

# Phase 5 test - Camera Double-Precision & Pipeline Conveniences
add_executable(test_phase5
    test_phase5.cpp
)

target_link_libraries(test_phase5 PRIVATE finevk-core finevk-engine)

# InputManager test
add_executable(test_input_manager
    test_input_manager.cpp
)

target_link_libraries(test_input_manager PRIVATE finevk-core finevk-engine)

# Image readback test - headless, proves GPU -> CPU round-trip
add_executable(test_readback
    test_readback.cpp
)

target_link_libraries(test_readback PRIVATE finevk-core)

# Copy any required resources
# (none for Phase 1/2/3/4/5)
