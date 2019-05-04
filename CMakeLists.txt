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
#target_link_libraries(y4mcreator PUBLIC ${AVFORMAT_LDFLAGS_OTHER})

set_target_properties(y4mcreator PROPERTIES LINK_FLAGS "-L/usr/local/lib -lavformat -lm -lbz2 -lz -Wl,-framework,CoreFoundation -Wl,-framework,Security -lavcodec -liconv -lm -llzma -lz -framework AudioToolbox -pthread -framework VideoToolbox -framework CoreFoundation -framework CoreMedia -framework CoreVideo -framework CoreServices -lswresample -lm -lavutil -pthread -lm -framework VideoToolbox -framework CoreFoundation -framework CoreMedia -framework CoreVideo -framework CoreServices")

#message( SEND_ERROR ${AVFORMAT_LDFLAGS} )

