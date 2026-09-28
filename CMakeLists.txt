cmake_minimum_required(VERSION 3.14)
project(SF3KUtils C)
enable_testing()

option(USE_OPTIONAL "Enable the _Optional qualifier" OFF)
option(USE_FORTIFY "Enable Fortify memory-allocation checking" OFF)
option(ENABLE_CLANG_TIDY "Run clang-tidy during compilation" OFF)
option(WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

include(FetchContent)

if(USE_OPTIONAL)
    if(NOT CMAKE_C_COMPILER_ID MATCHES "Clang|AppleClang")
        message(FATAL_ERROR
            "USE_OPTIONAL requires Christopher Bazley's Clang fork")
    endif()

    add_compile_definitions(USE_OPTIONAL)

    if(CMAKE_GENERATOR STREQUAL "Xcode")
        set(CMAKE_XCODE_ATTRIBUTE_RUN_CLANG_STATIC_ANALYZER "YES")
        set(CMAKE_XCODE_ATTRIBUTE_CLANG_ANALYZER_OTHER_FLAGS
            "-analyzer-checker optionality,core -analyzer-max-loop 32")
    endif()
endif()

if(USE_FORTIFY)
    FetchContent_Declare(Fortify
        GIT_REPOSITORY https://github.com/chrisbazley/Fortify.git
        GIT_TAG main
    )

    FetchContent_MakeAvailable(Fortify)
    link_libraries(Fortify)
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_XCODE_ATTRIBUTE_RUN_CLANG_STATIC_ANALYZER "YES")

add_subdirectory(SFToSpr)
add_subdirectory(SFSkyEdit)
add_subdirectory(FednetCmp)
add_subdirectory(SFColours)

# These targets belong to SF3KUtils. Do not pass the diagnostic policy to
# dependencies created by FetchContent inside the component directories.
if(WARNINGS_AS_ERRORS AND CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(OWN_TARGETS SFToSpr SFSkyEdit FednetCmp SFColours
        SFToSprApp SFSkyEditApp FednetCmpApp SFColoursApp
        SFToSprCoreTestsLib SFToSprAppTestsLib SFToSprTests
        SFSkyEditCoreTestsLib SFSkyEditAppTestsLib SFSkyEditTests
        SFColoursCoreTestsLib SFColoursAppTestsLib SFColoursTests)
    foreach(target IN LISTS OWN_TARGETS)
        if(TARGET ${target})
            if(MSVC)
                target_compile_options(${target} PRIVATE /WX)
            else()
                target_compile_options(${target} PRIVATE -Werror)
            endif()
        endif()
    endforeach()
endif()
