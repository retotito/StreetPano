add_executable(streetpano_app MACOSX_BUNDLE WIN32 main.cpp)
set_target_properties(streetpano_app PROPERTIES
    OUTPUT_NAME StreetPano
    MACOSX_BUNDLE_BUNDLE_NAME StreetPano
    MACOSX_BUNDLE_GUI_IDENTIFIER io.github.retotito.streetpano
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION})
target_link_libraries(streetpano_app PRIVATE streetpano_ui)
