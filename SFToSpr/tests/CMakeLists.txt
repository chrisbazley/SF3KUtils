set(CORESOURCES
    ConvTest.c
)

file(GLOB PUBLIC_HEADERS "*.h")

add_library(SFToSprCoreTestsLib OBJECT ${CORESOURCES} ${PUBLIC_HEADERS})
target_link_libraries(SFToSprCoreTestsLib PUBLIC SFToSpr)

set_target_properties(SFToSprCoreTestsLib PROPERTIES
    PUBLIC_HEADER "${PUBLIC_HEADERS}"
)

set(APPSOURCES
    AppTest.c
)

add_library(SFToSprAppTestsLib OBJECT ${APPSOURCES} ${PUBLIC_HEADERS})
target_link_libraries(SFToSprAppTestsLib PUBLIC SFToSpr)

set_target_properties(SFToSprAppTestsLib PROPERTIES
    PUBLIC_HEADER "${PUBLIC_HEADERS}"
)

target_include_directories(SFToSprAppTestsLib PRIVATE
    $<TARGET_PROPERTY:Stream,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:GKey,INTERFACE_INCLUDE_DIRECTORIES>
)

string(TOUPPER "${CMAKE_SYSTEM_NAME}" SYSTEM_NAME_UPPER)

add_executable(SFToSprTests Main.c)
target_link_libraries(SFToSprTests PRIVATE SFToSprCoreTestsLib)
add_test(NAME "SFToSprTests" COMMAND SFToSprTests)

if(SYSTEM_NAME_UPPER STREQUAL "RISCOS")
  target_compile_definitions(SFToSprTests PRIVATE ACORN_C)
  target_link_libraries(SFToSprTests PRIVATE SFToSprAppTestsLib)
endif()
