file(GLOB SOURCES "*.c")
file(GLOB PRIVATE_HEADERS "*.h")

string(TOUPPER "${CMAKE_SYSTEM_NAME}" SYSTEM_NAME_UPPER)

if(SYSTEM_NAME_UPPER STREQUAL "RISCOS")
  # RISC OS: The only platform where it links and runs
  add_executable(SFToSprTests ${SOURCES} ${PRIVATE_HEADERS})
  add_test(NAME "SFToSprTests" COMMAND SFToSprTests)
else()
  # Other platforms: Compile only (no linking) to catch errors
  add_library(SFToSprTests OBJECT ${SOURCES} ${PRIVATE_HEADERS})
endif()

target_include_directories(SFToSprTests PRIVATE
    $<TARGET_PROPERTY:Stream,INTERFACE_INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:GKey,INTERFACE_INCLUDE_DIRECTORIES>
)

target_link_libraries(SFToSprTests PRIVATE SFToSpr)
