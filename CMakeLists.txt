cmake_minimum_required(VERSION 2.8.8)
project(Steam++)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")

find_package(Protobuf REQUIRED)
find_package(CryptoPP REQUIRED)
find_package(LibArchive REQUIRED)

set(STEAMRE $ENV{SteamRE} CACHE PATH "Where you cloned SteamRE")
set(PROTOBUF_IMPORT_DIRS ${STEAMRE}/Resources/Protobufs)
PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS
	${PROTOBUF_IMPORT_DIRS}/steamclient/steammessages_clientserver.proto
	${PROTOBUF_IMPORT_DIRS}/steamclient/steammessages_base.proto
	${PROTOBUF_IMPORT_DIRS}/steamclient/encrypted_app_ticket.proto
)

set(CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS}")

include_directories(
	${PROTOBUF_INCLUDE_DIRS}
	${CRYPTOPP_INCLUDE_DIR}
	${LibArchive_INCLUDE_DIRS}
	${CMAKE_BINARY_DIR}
)

add_library(steam++
	steam++.cpp
	cmclient.cpp
	handlers.cpp
	${PROTO_SRCS}
)

target_link_libraries(steam++
	${PROTOBUF_LIBRARIES}
	${CRYPTOPP_LIBRARIES}
	${LibArchive_LIBRARIES}
)


# sample project that uses libuv as the event loop
# to be removed when there is a complete example project somewhere

find_package(Libuv)
if (LIBUV_FOUND)
	add_executable(steamuv
		steamuv.cpp
	)
	
	include_directories(${LIBUV_INCLUDE_DIR})
	
	target_link_libraries(steamuv
		steam++
		${LIBUV_LIBRARIES}
	)
endif()

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
	set_target_properties(steam++ PROPERTIES COMPILE_FLAGS "-fPIC")
endif (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")

# libpurple plugin

find_library(LIBPURPLE_LIBRARIES purple)
find_path(LIBPURPLE_INCLUDE_DIRS purple.h PATH_SUFFIXES libpurple)
find_package(GLIB)
if (LIBPURPLE_LIBRARIES AND LIBPURPLE_INCLUDE_DIRS AND GLIB_FOUND)
	add_library(steam MODULE
		steampurple.cpp
	)
	
	include_directories(
		${LIBPURPLE_INCLUDE_DIRS}
		${GLIB_INCLUDE_DIRS}
	)
	
	target_link_libraries(steam
		steam++
		${LIBPURPLE_LIBRARIES}
		${GLIB_LIBRARIES}
	)
endif()
