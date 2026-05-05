cmake_minimum_required(VERSION 3.11)
project(FednetCmp C)

include(FetchContent)

FetchContent_Declare(CBUtil GIT_REPOSITORY https://github.com/chrisbazley/CBUtilLib.git GIT_TAG main)
FetchContent_Declare(CB GIT_REPOSITORY https://github.com/chrisbazley/CBLibrary.git GIT_TAG main)
FetchContent_Declare(CBDebug GIT_REPOSITORY https://github.com/chrisbazley/CBDebugLib.git GIT_TAG main)
FetchContent_Declare(OptionalAcornC GIT_REPOSITORY https://github.com/chrisbazley/OptionalAcornC.git GIT_TAG main)
FetchContent_Declare(SF3K GIT_REPOSITORY https://github.com/chrisbazley/SF3KLib.git GIT_TAG main)

FetchContent_MakeAvailable(CBUtil CB CBDebug OptionalAcornC SF3K)

set(CMAKE_C_STANDARD 99)

if(MSVC)
    # MSVC equivalent of -Wall -Wextra is /W4
    add_compile_options(/W4)
else()
    # For GCC and Clang
    add_compile_options(-Wall -Wextra -Wsign-compare -pedantic)
endif()

set(SOURCES
    ParseArgs.c FNCInit.c FNCSaveBox.c SaveDir.c FNCIconbar.c FNCMenu.c Utils.c
    SaveFile.c SaveComp.c Scan.c PreQuit.c
)

add_library(FednetCmp ${SOURCES})

target_link_libraries(FednetCmp PUBLIC
    CB
    CBDebug
    OptionalAcornC
    SF3K
)

target_compile_definitions(FednetCmp PUBLIC
    $<$<CONFIG:Debug>:DEBUG_OUTPUT>
    $<$<CONFIG:Release>:NDEBUG>
)

target_compile_options(FednetCmp PUBLIC
    $<$<CONFIG:Release>:-O3>
)

string(TOUPPER "${CMAKE_SYSTEM_NAME}" SYSTEM_NAME_UPPER)

if(SYSTEM_NAME_UPPER STREQUAL "RISCOS")
  # RISC OS: The only platform where it links and runs
  add_executable(FednetCmpApp Main.c)
else()
  # Other platforms: Compile only (no linking) to catch errors
  add_library(FednetCmpApp Main.c)
endif()

target_link_libraries(FednetCmpApp PRIVATE
    FednetCmp
)

include(CTest)

if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests")
  add_subdirectory(tests)
endif()
