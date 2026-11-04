add_library(streetpano_options INTERFACE)

if(MSVC)
    target_compile_options(streetpano_options INTERFACE /W4 /permissive- /utf-8)
    if(STREETPANO_WERROR)
        target_compile_options(streetpano_options INTERFACE /WX)
    endif()
else()
    target_compile_options(streetpano_options INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
        -Wnon-virtual-dtor -Wold-style-cast -Woverloaded-virtual)
    if(STREETPANO_WERROR)
        target_compile_options(streetpano_options INTERFACE -Werror)
    endif()
endif()

if(STREETPANO_SANITIZE)
    target_compile_options(streetpano_options INTERFACE
        -fsanitize=${STREETPANO_SANITIZE} -fno-omit-frame-pointer -fno-sanitize-recover=all)
    target_link_options(streetpano_options INTERFACE -fsanitize=${STREETPANO_SANITIZE})
endif()

if(STREETPANO_COVERAGE)
    target_compile_options(streetpano_options INTERFACE --coverage -O0)
    target_link_options(streetpano_options INTERFACE --coverage)
endif()
