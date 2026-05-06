set(CORESOURCES
    SkyTest.c
    EditorTest.c
)

add_library(SFSkyEditCoreTestsLib OBJECT ${CORESOURCES})
target_link_libraries(SFSkyEditCoreTestsLib PUBLIC SFSkyEdit)

set(APPSOURCES
    AppTest.c
)

add_library(SFSkyEditAppTestsLib OBJECT ${APPSOURCES})
target_link_libraries(SFSkyEditAppTestsLib PUBLIC SFSkyEdit)

target_include_directories(SFSkyEditAppTestsLib PRIVATE
    $<TARGET_PROPERTY:Stream,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:GKey,INTERFACE_INCLUDE_DIRECTORIES>
)

string(TOUPPER "${CMAKE_SYSTEM_NAME}" SYSTEM_NAME_UPPER)

add_executable(SFSkyEditTests Main.c)
target_link_libraries(SFSkyEditTests PRIVATE SFSkyEditCoreTestsLib)
add_test(NAME "tests" COMMAND SFSkyEditTests)

if(SYSTEM_NAME_UPPER STREQUAL "RISCOS")
  target_compile_definitions(SFSkyEditTests PRIVATE ACORN_C)
  target_link_libraries(SFSkyEditTests PRIVATE SFSkyEditAppTestsLib)
endif()
