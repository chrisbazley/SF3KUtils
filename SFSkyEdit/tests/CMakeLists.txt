set(CORESOURCES
    SkyTest.c
    EditorTest.c
)

file(GLOB PUBLIC_HEADERS "*.h")

add_library(SFSkyEditCoreTestsLib OBJECT ${CORESOURCES} ${PUBLIC_HEADERS})
target_link_libraries(SFSkyEditCoreTestsLib PUBLIC SFSkyEdit)

set_target_properties(SFSkyEditCoreTestsLib PROPERTIES
    PUBLIC_HEADER "${PUBLIC_HEADERS}"
)

set(APPSOURCES
    AppTest.c
)

add_library(SFSkyEditAppTestsLib OBJECT ${APPSOURCES} ${PUBLIC_HEADERS})
target_link_libraries(SFSkyEditAppTestsLib PUBLIC SFSkyEdit)

set_target_properties(SFSkyEditAppTestsLib PROPERTIES
    PUBLIC_HEADER "${PUBLIC_HEADERS}"
)

target_include_directories(SFSkyEditAppTestsLib PRIVATE
    $<TARGET_PROPERTY:Stream,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:GKey,INTERFACE_INCLUDE_DIRECTORIES>
)

string(TOUPPER "${CMAKE_SYSTEM_NAME}" SYSTEM_NAME_UPPER)

add_executable(SFSkyEditTests Main.c)
target_link_libraries(SFSkyEditTests PRIVATE SFSkyEditCoreTestsLib)
add_test(NAME "SFSkyEditTests" COMMAND SFSkyEditTests)

if(SYSTEM_NAME_UPPER STREQUAL "RISCOS")
  target_compile_definitions(SFSkyEditTests PRIVATE ACORN_C)
  target_link_libraries(SFSkyEditTests PRIVATE SFSkyEditAppTestsLib)
endif()
