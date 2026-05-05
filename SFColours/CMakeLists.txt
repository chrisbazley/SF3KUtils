cmake_minimum_required(VERSION 3.11)
project(SFColours C)

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
    Picker.c ColsIO.c ExpColFile.c ColMap.c Editor.c EditWin.c SFCInit.c
             SFCIconbar.c Utils.c SFCSaveBox.c DCS_dialogue.c SFCFileInfo.c
             Menus.c PreQuit.c
)

add_library(SFColours ${SOURCES})

target_link_libraries(SFColours PUBLIC
    CB
    CBDebug
    OptionalAcornC
    SF3K
)

target_compile_definitions(SFColours PUBLIC
    $<$<CONFIG:Debug>:DEBUG_OUTPUT>
    $<$<CONFIG:Release>:NDEBUG>
)

target_compile_options(SFColours PUBLIC
    $<$<CONFIG:Release>:-O3>
)

string(TOUPPER "${CMAKE_SYSTEM_NAME}" SYSTEM_NAME_UPPER)

if(SYSTEM_NAME_UPPER STREQUAL "RISCOS")
  # RISC OS: The only platform where it links and runs
  add_executable(SFColoursApp Main.c)
else()
  # Other platforms: Compile only (no linking) to catch errors
  add_library(SFColoursApp Main.c)
endif()

target_link_libraries(SFColoursApp PRIVATE
    SFColours
)

include(CTest)

if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests")
  add_subdirectory(tests)
endif()
