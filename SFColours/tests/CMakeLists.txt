set(CORESOURCES
    ColmapTest.c
    EditorTest.c
)

file(GLOB PUBLIC_HEADERS "*.h")

add_library(SFColoursCoreTestsLib OBJECT ${CORESOURCES} ${PUBLIC_HEADERS})
target_link_libraries(SFColoursCoreTestsLib PUBLIC SFColours)

set_target_properties(SFColoursCoreTestsLib PROPERTIES
    PUBLIC_HEADER "${PUBLIC_HEADERS}"
)

set(APPSOURCES
    AppTest.c
)

add_library(SFColoursAppTestsLib OBJECT ${APPSOURCES} ${PUBLIC_HEADERS})
target_link_libraries(SFColoursAppTestsLib PUBLIC SFColours)

set_target_properties(SFColoursAppTestsLib PROPERTIES
    PUBLIC_HEADER "${PUBLIC_HEADERS}"
)

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
