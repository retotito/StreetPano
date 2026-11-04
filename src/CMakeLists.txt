add_library(streetpano_core STATIC
    core/lru_cache.hpp
    core/photo_listing.cpp
    core/photo_listing.hpp)
target_include_directories(streetpano_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(streetpano_core PUBLIC streetpano_options)

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
    ui/qt_ownership.hpp)
target_link_libraries(streetpano_ui PUBLIC streetpano_core Qt6::Widgets)
