cmake_minimum_required(VERSION 2.8.1)
project(Steam++)

find_package(Protobuf REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(LibArchive REQUIRED)

set(PROTOBUF_IMPORT_DIRS $ENV{STEAMRE}/Resources/Protobufs)
PROTOBUF_GENERATE_CPP(PROTO_SRCS PROTO_HDRS
	${PROTOBUF_IMPORT_DIRS}/steamclient/steammessages_clientserver.proto
	${PROTOBUF_IMPORT_DIRS}/steamclient/steammessages_base.proto
	${PROTOBUF_IMPORT_DIRS}/steamclient/encrypted_app_ticket.proto
)

list(APPEND CMAKE_CXX_FLAGS -std=c++11)

include_directories(
	${PROTOBUF_INCLUDE_DIRS}
	${OPENSSL_INCLUDE_DIR}
	${ZLIB_INCLUDE_DIRS}
	${LibArchive_INCLUDE_DIRS}
)

add_library(steam++
	steam_client.cpp
	handlers.cpp
	${PROTO_SRCS}
)

target_link_libraries(steam++
	${PROTOBUF_LIBRARIES}
	${OPENSSL_LIBRARIES}
	${ZLIB_LIBRARIES}
	${LibArchive_LIBRARIES}
)


# sample project that uses libuv as the event loop
# to be removed when there is a complete example project somewhere

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")
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
