# Tests CMakeLists.txt

# Phase 1 test - Core foundation
add_executable(test_phase1
    test_phase1.cpp
)
target_link_libraries(test_phase1 PRIVATE finevk-core)
add_test(NAME test_phase1 COMMAND test_phase1)

# Phase 2 test - Device & Memory Management
add_executable(test_phase2
    test_phase2.cpp
)
target_link_libraries(test_phase2 PRIVATE finevk-core)
add_test(NAME test_phase2 COMMAND test_phase2)

# Phase 3 test - Rendering Infrastructure
add_executable(test_phase3
    test_phase3.cpp
)
target_link_libraries(test_phase3 PRIVATE finevk-core)
add_test(NAME test_phase3 COMMAND test_phase3)

# Phase 4 test - High-Level Abstractions
add_executable(test_phase4
    test_phase4.cpp
)
target_link_libraries(test_phase4 PRIVATE finevk-core)
add_test(NAME test_phase4 COMMAND test_phase4)

# Phase 5 test - Camera Double-Precision & Pipeline Conveniences
add_executable(test_phase5
    test_phase5.cpp
)
target_link_libraries(test_phase5 PRIVATE finevk-core finevk-engine)
add_test(NAME test_phase5 COMMAND test_phase5)

# InputManager test
add_executable(test_input_manager
    test_input_manager.cpp
)
target_link_libraries(test_input_manager PRIVATE finevk-core finevk-engine)
add_test(NAME test_input_manager COMMAND test_input_manager)

# Image readback test - headless, proves GPU -> CPU round-trip
add_executable(test_readback
    test_readback.cpp
)
target_link_libraries(test_readback PRIVATE finevk-core)
add_test(NAME test_readback COMMAND test_readback)

# Phase R1.2 test - Header self-containment, anisotropyEnabled, handle overloads, RawMesh StagingPool & DeletionQueue
add_executable(test_r1_2
    test_r1_2.cpp
)
target_link_libraries(test_r1_2 PRIVATE finevk-core)
add_test(NAME test_r1_2 COMMAND test_r1_2)

# Swap chain recreated at the same size - needs a display
add_executable(test_swapchain_recreate
    test_swapchain_recreate.cpp
)
target_link_libraries(test_swapchain_recreate PRIVATE finevk-core)
add_test(NAME test_swapchain_recreate COMMAND test_swapchain_recreate)

# Copy any required resources
# (none for Phase 1/2/3/4/5)
