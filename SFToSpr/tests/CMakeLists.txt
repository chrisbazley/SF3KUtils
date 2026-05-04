file(GLOB SOURCES "*.c")

string(TOUPPER "${CMAKE_SYSTEM_NAME}" SYSTEM_NAME_UPPER)

if(SYSTEM_NAME_UPPER STREQUAL "RISCOS")
  # RISC OS: The only platform where it links and runs
  add_executable(SFToSprTests ${SOURCES})
  add_test(NAME "tests" COMMAND CBTests)
else()
  # Other platforms: Compile only (no linking) to catch errors
  add_library(SFToSprTests OBJECT ${SOURCES})
endif()

target_include_directories(SFToSprTests PRIVATE
    $<TARGET_PROPERTY:Stream,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:GKey,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(SFToSprTests PRIVATE SFToSpr)
