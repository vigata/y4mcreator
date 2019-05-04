cmake_minimum_required(VERSION 2.6 FATAL_ERROR)
project(Y4MCREATOR)

ADD_DEFINITIONS("-std=c99")
set(CMAKE_BUILD_TYPE Release)
set(CMAKE_VERBOSE_MAKEFILE on)


find_package(PkgConfig REQUIRED)
pkg_check_modules(AVFORMAT REQUIRED "libavformat")


include_directories( ${AVFORMAT_INCLUDE_DIRS})
link_directories( ${AVFORMAT_LIBRARY_DIRS})

add_executable(y4mcreator y4mcreator.c)
target_link_libraries(y4mcreator PUBLIC ${AVFORMAT_LIBRARIES})
target_compile_options(y4mcreator PUBLIC ${AVFORMAT_CFLAGS})

#we are sending link flags directly and we need to substitute ';' for ' '. Cmake lists are ';' separated. 
string (REPLACE ";" " " LD_FLAGS_STR "${AVFORMAT_LDFLAGS}")
set_target_properties(y4mcreator PROPERTIES LINK_FLAGS "${LD_FLAGS_STR}" )


