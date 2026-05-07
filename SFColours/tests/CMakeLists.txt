set(CORESOURCES
    ColmapTest.c
    EditorTest.c
)

add_library(SFColoursCoreTestsLib OBJECT ${CORESOURCES})
target_link_libraries(SFColoursCoreTestsLib PUBLIC SFColours)

set(APPSOURCES
    AppTest.c
)

add_library(SFColoursAppTestsLib OBJECT ${APPSOURCES})
target_link_libraries(SFColoursAppTestsLib PUBLIC SFColours)

target_include_directories(SFColoursAppTestsLib PRIVATE
    $<TARGET_PROPERTY:Stream,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:GKey,INTERFACE_INCLUDE_DIRECTORIES>
)

string(TOUPPER "${CMAKE_SYSTEM_NAME}" SYSTEM_NAME_UPPER)

add_executable(SFColoursTests Main.c)
target_link_libraries(SFColoursTests PRIVATE SFColoursCoreTestsLib)
add_test(NAME "SFColoursTests" COMMAND SFColoursTests)

if(SYSTEM_NAME_UPPER STREQUAL "RISCOS")
  target_compile_definitions(SFColoursTests PRIVATE ACORN_C)
  target_link_libraries(SFColoursTests PRIVATE SFColoursAppTestsLib)
endif()
