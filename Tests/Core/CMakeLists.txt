file(GLOB_RECURSE RB_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

add_executable(BilliardsCoreTests ${RB_TEST_SOURCES})
target_link_libraries(BilliardsCoreTests PRIVATE BilliardsCore)
target_include_directories(BilliardsCoreTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_test(NAME BilliardsCoreTests COMMAND BilliardsCoreTests)
