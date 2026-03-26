cmake_minimum_required(VERSION 3.20)

project(minimon LANGUAGES C)

option(MONITOR_BUILD_EXAMPLE "Build interactive monitor example" ON)
option(MONITOR_BUILD_TESTS "Build monitor smoke tests" OFF)

add_library(minimon STATIC
    src/monitor.c
)

target_include_directories(minimon
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(minimon PUBLIC c_std_11)

if(MSVC)
    target_compile_options(minimon PRIVATE /W4 /WX)
else()
    target_compile_options(minimon PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Wstrict-prototypes
    )
endif()

if(MONITOR_BUILD_EXAMPLE)
    add_executable(monitor_example
        examples/monitor_example.c
    )

    target_link_libraries(monitor_example PRIVATE minimon)
    target_compile_features(monitor_example PRIVATE c_std_11)

    if(MSVC)
        target_compile_options(monitor_example PRIVATE /W4 /WX)
    else()
        target_compile_options(monitor_example PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
            -Wstrict-prototypes
        )
    endif()
endif()

if(MONITOR_BUILD_TESTS)
    enable_testing()

    add_executable(monitor_smoke_test
        tests/monitor_smoke_test.c
    )

    target_link_libraries(monitor_smoke_test PRIVATE minimon)
    target_compile_features(monitor_smoke_test PRIVATE c_std_11)

    if(MSVC)
        target_compile_options(monitor_smoke_test PRIVATE /W4 /WX)
    else()
        target_compile_options(monitor_smoke_test PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wshadow
            -Wstrict-prototypes
        )
    endif()

    add_test(NAME monitor_smoke_test COMMAND monitor_smoke_test)
endif()
