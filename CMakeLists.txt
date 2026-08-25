cmake_minimum_required(VERSION 3.20)
project(Snip LANGUAGES CXX)

add_executable(Snip WIN32 src/main.cpp src/app.rc)
target_compile_features(Snip PRIVATE cxx_std_17)
target_compile_definitions(Snip PRIVATE UNICODE _UNICODE NOMINMAX)
target_link_libraries(Snip PRIVATE user32 shell32)

if(MSVC)
  target_compile_options(Snip PRIVATE /W4 /permissive-)
else()
  target_compile_options(Snip PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_custom_command(TARGET Snip POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/snippets.txt
          $<TARGET_FILE_DIR:Snip>/snippets.txt)
