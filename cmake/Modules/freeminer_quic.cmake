
if(ENABLE_QUIC)
	#if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/mvfst/_build/deps/include)
	#	include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/mvfst/_build/deps/include)
	#endif()

	#if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/mvfst/_build/deps/lib/libfolly.a)
	#endif()

	if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/mvfst/quic/QuicConstants.h)
		message(warning "CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}")
		set(CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/external/mvfst/_build/deps")

	#set(mvfst_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/mvfst/_build/deps")

	#include(CMakeFindDependencyMacro)
	#find_package(mvfst REQUIRED)
	#find_dependency(mvfst)

		add_subdirectory(external/mvfst)
		set(QUIC_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/mvfst/quic)
		set(QUIC_LIBRARY mvfst_client mvfst_fizz_client mvfst_server)
		include_directories(
			${CMAKE_CURRENT_SOURCE_DIR}/external/mvfst/_build/deps/include
			${CMAKE_CURRENT_SOURCE_DIR}/external/mvfst/
			# ${CMAKE_CURRENT_SOURCE_DIR}/external/mvfst/_build/deps/folly ${CMAKE_CURRENT_SOURCE_DIR}/external/mvfst/_build/deps/folly/build ${CMAKE_CURRENT_SOURCE_DIR}/external/mvfst/_build/deps/fizz
		)
		
	endif()
	#if(NOT QUIC_LIBRARY)
	#	find_library(QUIC_LIBRARY NAMES mvfst_server)
	#	find_path(QUIC_INCLUDE_DIR enet/enet.h)
	#endif()
	if(QUIC_LIBRARY AND QUIC_INCLUDE_DIR)
		#include_directories(${QUIC_INCLUDE_DIR})
		message(STATUS "Using QUIC: ${QUIC_INCLUDE_DIR} ${QUIC_LIBRARY}")
		set(USE_QUIC 1)
	endif()
endif()
