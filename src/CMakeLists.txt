add_library(streetpano_core STATIC
    core/lru_cache.hpp
    core/photo_adjustment.cpp
    core/photo_adjustment.hpp
    core/photo_listing.cpp
    core/photo_listing.hpp
    core/patch.cpp
    core/patch.hpp
    core/pixels.hpp
    core/seam_match.cpp
    core/seam_match.hpp
    core/stitch_plan.cpp
    core/stitch_plan.hpp
    core/camera_pose.hpp
    core/facade_surface.cpp
    core/facade_surface.hpp
    core/vec3.hpp)
target_include_directories(streetpano_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(streetpano_core PUBLIC streetpano_options)

add_library(streetpano_stitch STATIC
    stitch/alignment.cpp
    stitch/alignment.hpp
    stitch/decisions.cpp
    stitch/decisions.hpp
    stitch/pose_file.cpp
    stitch/pose_file.hpp
    stitch/project.cpp
    stitch/project.hpp
    stitch/stitcher.cpp
    stitch/stitcher.hpp)
target_link_libraries(streetpano_stitch PUBLIC streetpano_core Qt6::Gui)

add_library(streetpano_ui STATIC
    ui/align_job.cpp
    ui/align_job.hpp
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
    ui/recent_projects.cpp
    ui/recent_projects.hpp
    ui/stitch_job.cpp
    ui/stitch_job.hpp)
target_link_libraries(streetpano_ui PUBLIC streetpano_stitch Qt6::Widgets)
