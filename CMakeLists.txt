cmake_minimum_required(VERSION 3.10)
project(SF3KUtils C)
enable_testing()

option(USE_OPTIONAL "Enable the _Optional qualifier" OFF)

if(USE_OPTIONAL)
    add_compile_definitions(USE_OPTIONAL)

    if(CMAKE_GENERATOR STREQUAL "Xcode")
        set(CMAKE_XCODE_ATTRIBUTE_RUN_CLANG_STATIC_ANALYZER "YES")
        set(CMAKE_XCODE_ATTRIBUTE_CLANG_ANALYZER_OTHER_FLAGS
            "-analyzer-checker optionality,core \
             -analyzer-disable-checker=security.ArrayBound \
             -analyzer-max-loop 32")
    endif()
endif()

add_subdirectory(SFToSpr)
add_subdirectory(SFSkyEdit)
add_subdirectory(FednetCmp)
add_subdirectory(SFColours)
