add_library(streetpano_core STATIC
    core/lru_cache.hpp
    core/photo_listing.cpp
    core/photo_listing.hpp
    core/stitch_plan.cpp
    core/stitch_plan.hpp
    core/street_frame.cpp
    core/street_frame.hpp
    core/vec3.hpp)
target_include_directories(streetpano_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(streetpano_core PUBLIC streetpano_options)

add_library(streetpano_stitch STATIC
    stitch/pose_file.cpp
    stitch/pose_file.hpp
    stitch/stitcher.cpp
    stitch/stitcher.hpp)
target_link_libraries(streetpano_stitch PUBLIC streetpano_core Qt6::Gui)

add_library(streetpano_ui STATIC
    ui/film_strip_delegate.cpp
    ui/film_strip_delegate.hpp
    ui/image_loader.cpp
    ui/image_loader.hpp
    ui/main_window.cpp
    ui/main_window.hpp
    ui/photo_list_model.cpp
    ui/photo_list_model.hpp
    ui/photo_view.cpp
    ui/photo_view.hpp
    ui/qt_ownership.hpp
    ui/stitch_job.cpp
    ui/stitch_job.hpp)
target_link_libraries(streetpano_ui PUBLIC streetpano_stitch Qt6::Widgets)
