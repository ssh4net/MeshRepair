# Tests for MeshRepair

# Simple test that just runs the executable with --help
add_test(
    NAME test_help
    COMMAND mesh_hole_filler --help
)

# Test that checks for missing arguments
add_test(
    NAME test_no_args
    COMMAND mesh_hole_filler
)
set_tests_properties(test_no_args PROPERTIES WILL_FAIL TRUE)

# TODO: Add more tests with actual test meshes
# add_test(
#     NAME test_simple_cube
#     COMMAND mesh_hole_filler
#         ${CMAKE_CURRENT_SOURCE_DIR}/test_data/cube_with_hole.obj
#         ${CMAKE_CURRENT_BINARY_DIR}/cube_repaired.obj
# )
