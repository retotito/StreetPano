include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.8.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(core_tests
    core/lru_cache_test.cpp
    core/photo_listing_test.cpp
    core/patch_test.cpp
    core/seam_match_test.cpp
    core/stitch_plan_test.cpp
    core/facade_surface_test.cpp)
target_link_libraries(core_tests PRIVATE streetpano_core Catch2::Catch2WithMain)
add_test(NAME core_tests COMMAND core_tests)

function(add_ui_test name)
    add_executable(${name} ui/${name}.cpp)
    target_link_libraries(${name} PRIVATE streetpano_ui Qt6::Test)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 120)
endfunction()

add_ui_test(image_loader_test)
add_ui_test(photo_view_test)
add_ui_test(main_window_test)
add_ui_test(decisions_test)
add_ui_test(project_test)
add_ui_test(pose_file_test)
add_ui_test(stitcher_test)
add_ui_test(editing_test)

add_executable(app_screenshot ui/app_screenshot.cpp)
target_link_libraries(app_screenshot PRIVATE streetpano_ui Qt6::Test)
