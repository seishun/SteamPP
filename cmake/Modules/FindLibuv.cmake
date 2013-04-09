# based on FindProtobuf.cmake
# am I supposed to put licensing information here

find_path(LIBUV_SRC_ROOT_FOLDER gyp_uv)

find_library(LIBUV_LIBRARY
	NAMES uv libuv
	PATHS
		${LIBUV_SRC_ROOT_FOLDER}/Release
		${LIBUV_SRC_ROOT_FOLDER}/out/Release/lib.target
)
mark_as_advanced(LIBUV_LIBRARY)

find_library(LIBUV_LIBRARY_DEBUG
	NAMES libuv
	PATHS
		${LIBUV_SRC_ROOT_FOLDER}/Debug
		${LIBUV_SRC_ROOT_FOLDER}/out/Debug/lib.target
)
mark_as_advanced(LIBUV_LIBRARY_DEBUG)

if(NOT LIBUV_LIBRARY_DEBUG)
	# There is no debug library
	set(LIBUV_LIBRARY_DEBUG ${LIBUV_LIBRARY})
	set(LIBUV_LIBRARIES     ${LIBUV_LIBRARY})
else()
	# There IS a debug library
	set(LIBUV_LIBRARIES
		optimized ${LIBUV_LIBRARY}
		debug     ${LIBUV_LIBRARY_DEBUG}
	)
endif()

# Find the include directory
find_path(LIBUV_INCLUDE_DIR
	uv.h
	PATHS ${LIBUV_SRC_ROOT_FOLDER}/include
)
mark_as_advanced(LIBUV_INCLUDE_DIR)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Libuv DEFAULT_MSG
	LIBUV_LIBRARY LIBUV_INCLUDE_DIR)

if(LIBUV_FOUND)
	set(LIBUV_INCLUDE_DIRS ${LIBUV_INCLUDE_DIR})
endif()
