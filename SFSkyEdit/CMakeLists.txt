cmake_minimum_required(VERSION 3.11)
project(SFSkyEdit C)

include(FetchContent)

FetchContent_Declare(CBUtil GIT_REPOSITORY https://github.com/chrisbazley/CBUtilLib.git GIT_TAG main)
FetchContent_Declare(CB GIT_REPOSITORY https://github.com/chrisbazley/CBLibrary.git GIT_TAG main)
FetchContent_Declare(CBDebug GIT_REPOSITORY https://github.com/chrisbazley/CBDebugLib.git GIT_TAG main)
FetchContent_Declare(OptionalAcornC GIT_REPOSITORY https://github.com/chrisbazley/OptionalAcornC.git GIT_TAG main)
FetchContent_Declare(SF3K GIT_REPOSITORY https://github.com/chrisbazley/SF3KLib.git GIT_TAG main)

FetchContent_MakeAvailable(CBUtil CB CBDebug OptionalAcornC SF3K)

set(CMAKE_C_STANDARD 99)
set(CMAKE_XCODE_ATTRIBUTE_RUN_CLANG_STATIC_ANALYZER "YES")

if(MSVC)
    # MSVC equivalent of -Wall -Wextra is /W4
    add_compile_options(/W4 /wd4200 /wd4996 /wd4130)
else()
    # For GCC and Clang
    add_compile_options(-Wall -Wextra -Wsign-compare -pedantic)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "Clang|AppleClang")
    find_program(CLANG_TIDY_EXE
        NAMES clang-tidy
        HINTS "/opt/homebrew/opt/llvm/bin"
    )

    if(CLANG_TIDY_EXE)
        set(CLANG_TIDY_ARGS "${CLANG_TIDY_EXE};-checks=-*,clang-analyzer-*,-clang-analyzer-security.insecureAPI.*")

        if(APPLE)
            execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE MAC_SDK_PATH OUTPUT_STRIP_TRAILING_WHITESPACE)
            list(APPEND CLANG_TIDY_ARGS "--extra-arg=-isysroot" "--extra-arg=${MAC_SDK_PATH}")
        endif()

        set(CMAKE_C_CLANG_TIDY "${CLANG_TIDY_ARGS}")
        message(STATUS "Clang-Tidy Static Analyzer enabled automatically.")
    else()
        message(STATUS "clang-tidy binary not found. Skipping analysis pass.")
    endif()
endif()

set(SOURCES
    Picker.c SkyIO.c EditWin.c SFSInit.c ParseArgs.c SFSIconbar.c Utils.c
    SFSSaveBox.c DCS_dialogue.c SFSFileInfo.c Menus.c Layout.c
    Sky.c Editor.c Export.c Interpolate.c Insert.c PreQuit.c Preview.c
    PrevUMenu.c SavePrev.c ScalePrev.c Goto.c OptsMenu.c
)

file(GLOB PRIVATE_HEADERS "*.h")

add_library(SFSkyEdit ${SOURCES} ${PRIVATE_HEADERS})

target_link_libraries(SFSkyEdit PUBLIC
    CB
    CBDebug
    OptionalAcornC
    SF3K
)

target_compile_definitions(SFSkyEdit PUBLIC
    $<$<CONFIG:Debug>:DEBUG_OUTPUT>
)

string(TOUPPER "${CMAKE_SYSTEM_NAME}" SYSTEM_NAME_UPPER)

if(SYSTEM_NAME_UPPER STREQUAL "RISCOS")
  # RISC OS: The only platform where it links and runs
  add_executable(SFSkyEditApp Main.c)
else()
  # Other platforms: Compile only (no linking) to catch errors
  add_library(SFSkyEditApp Main.c)
endif()

target_link_libraries(SFSkyEditApp PRIVATE
    SFSkyEdit
)

include(CTest)

if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/tests")
  add_subdirectory(tests)
endif()
