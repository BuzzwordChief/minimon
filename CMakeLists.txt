cmake_minimum_required(VERSION 3.20)

project(minimon LANGUAGES C)

option(MONITOR_BUILD_EXAMPLE "Build interactive monitor example" OFF)
option(MONITOR_BUILD_TESTS "Build monitor tests" OFF)
option(MONITOR_ENABLE_FLOAT_SUPPORT "Enable float trace and shell support" OFF)
option(MONITOR_ENABLE_SHELL_FEATURES "Enable interactive shell features (echo, history, tab completion)" ON)
option(MONITOR_ENABLE_SIZE_OPTIMIZATIONS "Enable section-based dead-stripping for release builds" ON)

add_library(minimon STATIC src/monitor.c)

target_include_directories(minimon PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_compile_features(minimon PUBLIC c_std_11)
target_compile_definitions(minimon PUBLIC
        MONITOR_ENABLE_FLOAT_SUPPORT=$<BOOL:${MONITOR_ENABLE_FLOAT_SUPPORT}>
        MONITOR_ENABLE_SHELL_FEATURES=$<BOOL:${MONITOR_ENABLE_SHELL_FEATURES}>
)

if(MONITOR_ENABLE_SIZE_OPTIMIZATIONS)
    if(MSVC)
        target_compile_options(minimon PRIVATE
            $<$<CONFIG:Release>:/Gw>
            $<$<CONFIG:Release>:/Gy>
            $<$<CONFIG:MinSizeRel>:/Gw>
            $<$<CONFIG:MinSizeRel>:/Gy>
        )
        target_link_options(minimon INTERFACE
            $<$<CONFIG:Release>:/OPT:REF>
            $<$<CONFIG:Release>:/OPT:ICF>
            $<$<CONFIG:MinSizeRel>:/OPT:REF>
            $<$<CONFIG:MinSizeRel>:/OPT:ICF>
        )
    else()
        target_compile_options(minimon PRIVATE
            $<$<CONFIG:Release>:-ffunction-sections>
            $<$<CONFIG:Release>:-fdata-sections>
            $<$<CONFIG:MinSizeRel>:-ffunction-sections>
            $<$<CONFIG:MinSizeRel>:-fdata-sections>
        )

        if(APPLE)
            target_link_options(minimon INTERFACE
                $<$<CONFIG:Release>:-Wl,-dead_strip>
                $<$<CONFIG:MinSizeRel>:-Wl,-dead_strip>
            )
        else()
            target_link_options(minimon INTERFACE
                $<$<CONFIG:Release>:-Wl,--gc-sections>
                $<$<CONFIG:MinSizeRel>:-Wl,--gc-sections>
            )
        endif()
    endif()
endif()

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

    function(monitor_add_test test_name)
        add_executable(${test_name}
            tests/${test_name}.c
        )

        target_link_libraries(${test_name} PRIVATE minimon)
        target_compile_features(${test_name} PRIVATE c_std_11)

        if(MSVC)
            target_compile_options(${test_name} PRIVATE /W4 /WX)
        else()
            target_compile_options(${test_name} PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wshadow
                -Wstrict-prototypes
            )
        endif()

        add_test(NAME ${test_name} COMMAND ${test_name})
    endfunction()

    if(MONITOR_ENABLE_SHELL_FEATURES)
        monitor_add_test(monitor_shell_features_test)
    else()
        monitor_add_test(monitor_smoke_test)
        monitor_add_test(monitor_shell_test)
        monitor_add_test(monitor_buffer_test)
        monitor_add_test(monitor_registry_test)
    endif()
endif()
