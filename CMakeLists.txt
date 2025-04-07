cmake_minimum_required(VERSION 3.10)

project(FileshareClient)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK REQUIRED gtk4)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/extern/curl/include)
include_directories(${GTK_INCLUDE_DIRS})

add_executable(FileshareClient Main.c)
target_link_libraries(FileshareClient curl ${GTK_LIBRARIES})

target_compile_options(FileshareClient PRIVATE ${GTK_CFLAGS_OTHER})
