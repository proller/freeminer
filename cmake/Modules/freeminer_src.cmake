
# == freeminer:

find_package(MsgPack REQUIRED)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    option(ENABLE_SCTP "Enable SCTP networking (EXPERIMENTAL)" 0)
    option(USE_MULTI "Enable MT+ENET+WSS networking" 1)
endif()
if(USE_MULTI)
    #set(ENABLE_SCTP 1 CACHE BOOL "") # Maybe bugs
    set(ENABLE_ENET 1 CACHE BOOL "")
    #set(ENABLE_WEBSOCKET_SCTP 1 CACHE BOOL "") # NOT FINISHED
    if(NOT ANDROID)
        set(ENABLE_WEBSOCKET 1 CACHE BOOL "")
    endif()
endif()

option(FETCH_DEPS "Compile deps (boost,...) in place" 0)

if(FETCH_DEPS)
    include(FetchContent)
    set(FETCHCONTENT_QUIET FALSE) # Needed to print downloading progress
    set(ENABLE_LIB_ONLY ON CACHE BOOL "")
    set(ENABLE_TESTS OFF CACHE BOOL "")
    FetchContent_Declare(
        BZip2
        GIT_REPOSITORY "https://gitlab.com/bzip2/bzip2.git"
        GIT_TAG "master"
        # GIT_TAG "bzip2-1.0.8" # CMake support not available
        GIT_SHALLOW TRUE

        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/bzip2
        OVERRIDE_FIND_PACKAGE TRUE
        USES_TERMINAL_DOWNLOAD TRUE
        GIT_PROGRESS TRUE
        DOWNLOAD_EXTRACT_TIMESTAMP ON
    )
    FetchContent_MakeAvailable(BZip2)
    set(BZIP2_FOUND 1 CACHE BOOL "")
    add_library(BZip2::BZip2 ALIAS bz2)
    set(BZIP2_INCLUDE_DIR "${bzip2_SOURCE_DIR}" CACHE INTERNAL "")
    target_include_directories(bz2 PUBLIC ${BZIP2_INCLUDE_DIR})
endif()

if(FETCH_DEPS)
    set(BOOST_ENABLE_CMAKE ON)
    set(BOOST_INCLUDE_LIBRARIES program_options)

    include(FetchContent)
    set(FETCHCONTENT_QUIET FALSE) # Needed to print downloading progress
    FetchContent_Declare(
        Boost
        GIT_REPOSITORY https://github.com/boostorg/boost.git
        GIT_TAG boost-1.90.0
        GIT_SHALLOW TRUE

        SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/boost
        OVERRIDE_FIND_PACKAGE TRUE # needed to find correct Boost
        USES_TERMINAL_DOWNLOAD TRUE
        GIT_PROGRESS TRUE
        DOWNLOAD_EXTRACT_TIMESTAMP ON
        EXCLUDE_FROM_ALL
    )
    FetchContent_MakeAvailable(Boost)
endif()

if(ENABLE_WEBSOCKET OR ENABLE_WEBSOCKET_SCTP)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/websocketpp/CMakeLists.txt)
        find_package(Boost)
        if(Boost_FOUND)

            if(boost_SOURCE_DIR)
                include_directories(BEFORE SYSTEM
                    ${boost_SOURCE_DIR}/libs/align/include
                    ${boost_SOURCE_DIR}/libs/asio/include
                    ${boost_SOURCE_DIR}/libs/assert/include
                    ${boost_SOURCE_DIR}/libs/bind/include
                    ${boost_SOURCE_DIR}/libs/config/include
                    ${boost_SOURCE_DIR}/libs/container_hash/include
                    ${boost_SOURCE_DIR}/libs/container/include
                    ${boost_SOURCE_DIR}/libs/core/include
                    ${boost_SOURCE_DIR}/libs/date_time/include
                    ${boost_SOURCE_DIR}/libs/describe/include
                    ${boost_SOURCE_DIR}/libs/detail/include
                    ${boost_SOURCE_DIR}/libs/function/include
                    ${boost_SOURCE_DIR}/libs/lexical_cast/include
                    ${boost_SOURCE_DIR}/libs/move/include
                    ${boost_SOURCE_DIR}/libs/mp11/include
                    ${boost_SOURCE_DIR}/libs/mpl/include
                    ${boost_SOURCE_DIR}/libs/numeric/conversion/include
                    ${boost_SOURCE_DIR}/libs/smart_ptr/include
                    ${boost_SOURCE_DIR}/libs/static_assert/include
                    ${boost_SOURCE_DIR}/libs/system/include
                    ${boost_SOURCE_DIR}/libs/throw_exception/include
                    ${boost_SOURCE_DIR}/libs/type_index/include
                    ${boost_SOURCE_DIR}/libs/type_traits/include
                )
            endif()

            include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/websocketpp)
            #add_subdirectory(external/websocketpp)
            #set(WEBSOCKETPP_LIBRARY websocketpp::websocketpp)
            find_package(OpenSSL)

            if(NOT TARGET OpenSSL::SSL AND FETCH_DEPS)
                # https://stackoverflow.com/questions/66829315/how-to-use-cmake-fetchcontent-to-link-openssl

                include(ExternalProject)
                set(OPENSSL_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/openssl)
                set(OPENSSL_INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/openssl)
                set(OPENSSL_INCLUDE_DIR ${OPENSSL_INSTALL_DIR}/include)
                set(OPENSSL_CONFIGURE_COMMAND ${OPENSSL_SOURCE_DIR}/config)
                ExternalProject_Add(
                    OpenSSL
                    SOURCE_DIR ${OPENSSL_SOURCE_DIR}
                    GIT_REPOSITORY https://github.com/openssl/openssl.git
                    GIT_TAG openssl-3.6.0 # OpenSSL_1_1_1n
                    USES_TERMINAL_DOWNLOAD TRUE
                    CONFIGURE_COMMAND
                    ${OPENSSL_CONFIGURE_COMMAND}
                    --prefix=${OPENSSL_INSTALL_DIR}
                    --openssldir=${OPENSSL_INSTALL_DIR}
                    BUILD_COMMAND make
                    TEST_COMMAND ""
                    INSTALL_COMMAND make install
                    INSTALL_DIR ${OPENSSL_INSTALL_DIR}
                )
                # We cannot use find_library because ExternalProject_Add() is performed at build time.
                # And to please the property INTERFACE_INCLUDE_DIRECTORIES,
                # we make the include directory in advance.
                file(MAKE_DIRECTORY ${OPENSSL_INCLUDE_DIR})

                add_library(OpenSSL::SSL STATIC IMPORTED GLOBAL)
                set_property(TARGET OpenSSL::SSL PROPERTY IMPORTED_LOCATION ${OPENSSL_INSTALL_DIR}/lib/libssl.${OPENSSL_LIBRARY_SUFFIX})
                set_property(TARGET OpenSSL::SSL PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${OPENSSL_INCLUDE_DIR})
                add_dependencies(OpenSSL::SSL OpenSSL)

                add_library(OpenSSL::Crypto STATIC IMPORTED GLOBAL)
                set_property(TARGET OpenSSL::Crypto PROPERTY IMPORTED_LOCATION ${OPENSSL_INSTALL_DIR}/lib/libcrypto.${OPENSSL_LIBRARY_SUFFIX})
                set_property(TARGET OpenSSL::Crypto PROPERTY INTERFACE_INCLUDE_DIRECTORIES ${OPENSSL_INCLUDE_DIR})
                add_dependencies(OpenSSL::Crypto OpenSSL)
            endif()

            if(OPENSSL_FOUND)
                set(WEBSOCKETPP_LIBRARY ${WEBSOCKETPP_LIBRARY} OpenSSL::SSL)
            endif()

            set(WEBSOCKETPP_LIBRARY ${WEBSOCKETPP_LIBRARY} Boost::headers)
            set(USE_WEBSOCKET 1 CACHE BOOL "")
            message(STATUS "Using websocket ${USE_WEBSOCKET}: ${CMAKE_CURRENT_SOURCE_DIR}/external/websocketpp : ${WEBSOCKETPP_LIBRARY}")
            #TODO:
            # set(USE_WEBSOCKET_SCTP 1 CACHE BOOL "")
            set(FREEMINER_COMMON_LIBRARIES ${FREEMINER_COMMON_LIBRARIES} ${WEBSOCKETPP_LIBRARY})
        endif()
    else()
        #set(USE_WEBSOCKET 0)
        #set(USE_WEBSOCKET_SCTP 0)
    endif()
endif()

if(ENABLE_SCTP AND NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/usrsctp/usrsctplib)
    message(WARNING "Please Clone usrsctp:  git clone --depth 1 https://github.com/sctplab/usrsctp ${CMAKE_CURRENT_SOURCE_DIR}/external/usrsctp")
    set(ENABLE_SCTP 0)
endif()

if(ENABLE_SCTP)
    # from external/usrsctp/usrsctplib/CMakeLists.txt :
    if(SCTP_DEBUG)
        set(sctp_debug 1 CACHE INTERNAL "")
        add_definitions(-DSCTP_DEBUG=1)
    endif()
    set(sctp_build_programs 0 CACHE INTERNAL "")
    set(sctp_werror 0 CACHE INTERNAL "")
    set(WERROR 0 CACHE INTERNAL "") #old

    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/external/usrsctp)

    #include_directories(${CMAKE_CURRENT_SOURCE_DIR}/external/usrsctp/usrsctplib)
    set(SCTP_LIBRARY usrsctp)

    set(USE_SCTP 1)

    message(STATUS "Using sctp: ${CMAKE_CURRENT_SOURCE_DIR}/external/usrsctp ${SCTP_LIBRARY} SCTP_DEBUG=${SCTP_DEBUG}")
    set(FREEMINER_COMMON_LIBRARIES ${FREEMINER_COMMON_LIBRARIES} ${SCTP_LIBRARY})
endif()

if(ENABLE_ENET)
    if(NOT ENABLE_SYSTEM_ENET AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/enet/include/enet/enet.h)
        add_subdirectory(external/enet)
        set(ENET_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/enet/include)
        set(ENET_LIBRARY enet)
    endif()
    if(NOT ENET_LIBRARY)
        find_library(ENET_LIBRARY NAMES enet)
        find_path(ENET_INCLUDE_DIR enet/enet.h)
    endif()
    if(ENET_LIBRARY AND ENET_INCLUDE_DIR)
        include_directories(${ENET_INCLUDE_DIR})
        message(STATUS "Using enet: ${ENET_INCLUDE_DIR} ${ENET_LIBRARY}")
        set(USE_ENET 1)
        set(FREEMINER_COMMON_LIBRARIES ${FREEMINER_COMMON_LIBRARIES} ${ENET_LIBRARY})
    endif()
endif()

#set(TinyTIFF_BUILD_TESTS 0 CACHE INTERNAL "")
#add_subdirectory(external/TinyTIFF/src)
#set(TINYTIFF_LIRARY TinyTIFF)

option(ENABLE_TIFF "Enable tiff (geotiff for mapgen earth)" 1)
if(ENABLE_TIFF AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/external/libtiff/CMakeLists.txt)
    set(tiff-tools 0 CACHE INTERNAL "")
    set(tiff-tests 0 CACHE INTERNAL "")
    set(tiff-docs 0 CACHE INTERNAL "")
    add_subdirectory(external/libtiff)
    set(TIFF_LIRARY TIFF::tiff)
    set(TIFF_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/external/libtiff/libtiff ${CMAKE_CURRENT_SOURCE_DIR}/external/libtiff/libtiff)
    include_directories(BEFORE SYSTEM ${TIFF_INCLUDE_DIR})
    message(STATUS "Using tiff: ${TIFF_INCLUDE_DIR} ${TIFF_LIRARY}")
    set(USE_TIFF 1)
    set(FREEMINER_COMMON_LIBRARIES ${FREEMINER_COMMON_LIBRARIES} ${TIFF_LIRARY})
endif()

option(ENABLE_OSMIUM "Enable Osmium" 1)

# if(ENABLE_OSMIUM)
#     find_path(OSMIUM_INCLUDE_DIR osmium/osm.hpp)
# endif()

if(ENABLE_OSMIUM AND (OSMIUM_INCLUDE_DIR OR EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/mapgen/earth/libosmium/CMakeLists.txt))

    if(FETCH_DEPS)
        include(FetchContent)
        set(FETCHCONTENT_QUIET FALSE) # Needed to print downloading progress

        FetchContent_Declare(lz4
            URL https://github.com/lz4/lz4/archive/refs/tags/v1.10.0.tar.gz
            URL_HASH SHA256=537512904744b35e232912055ccf8ec66d768639ff3abe5788d90d792ec5f48b
            SOURCE_SUBDIR build/cmake
            SYSTEM TRUE

            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/lz4
            GIT_SHALLOW TRUE
            OVERRIDE_FIND_PACKAGE TRUE
            USES_TERMINAL_DOWNLOAD TRUE
            GIT_PROGRESS TRUE
            DOWNLOAD_EXTRACT_TIMESTAMP ON
            EXCLUDE_FROM_ALL

        )
        FetchContent_MakeAvailable(lz4)

        FetchContent_Declare(protozero
            GIT_REPOSITORY https://github.com/mapbox/protozero
            GIT_TAG v1.8.1
            SOURCE_SUBDIR cmake
            GIT_SUBMODULES_RECURSE OFF

            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/protozero
            GIT_SHALLOW TRUE
            OVERRIDE_FIND_PACKAGE TRUE
            USES_TERMINAL_DOWNLOAD TRUE
            GIT_PROGRESS TRUE
            DOWNLOAD_EXTRACT_TIMESTAMP ON
            EXCLUDE_FROM_ALL
        )
        FetchContent_MakeAvailable(protozero)

        set(PROTOZERO_INCLUDE_DIR "${protozero_SOURCE_DIR}")

        FetchContent_Declare(
            expat
            GIT_REPOSITORY https://github.com/libexpat/libexpat/
            GIT_TAG R_2_7_3
            SOURCE_SUBDIR expat/

            SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/expat
            GIT_SHALLOW TRUE
            OVERRIDE_FIND_PACKAGE TRUE
            USES_TERMINAL_DOWNLOAD TRUE
            GIT_PROGRESS TRUE
            DOWNLOAD_EXTRACT_TIMESTAMP ON
            EXCLUDE_FROM_ALL

        )
        FetchContent_MakeAvailable(expat)

        add_library(EXPAT::EXPAT ALIAS expat)
    endif()
    set(Boost_USE_STATIC_LIBS ${BUILD_STATIC_LIBS})
    find_package(Boost COMPONENTS program_options)
    if(Boost_FOUND)
        set(BUILD_TESTING 0 CACHE INTERNAL "")
        set(BUILD_DATA_TESTS 0 CACHE INTERNAL "")
        set(BUILD_EXAMPLES 0 CACHE INTERNAL "")
        set(BUILD_BENCHMARKS 0 CACHE INTERNAL "")
        set(Osmium_USE_GEOS 0 CACHE INTERNAL "")
        set(Osmium_USE_GDAL 0 CACHE INTERNAL "")
        set(CPPCHECK 0 CACHE INTERNAL "")

        if(NOT OSMIUM_INCLUDE_DIR)
            if(boost_SOURCE_DIR)
                include_directories(BEFORE SYSTEM
                    ${boost_SOURCE_DIR}/libs/any/include
                    ${boost_SOURCE_DIR}/libs/assert/include
                    ${boost_SOURCE_DIR}/libs/config/include
                    ${boost_SOURCE_DIR}/libs/container_hash/include
                    ${boost_SOURCE_DIR}/libs/container/include
                    ${boost_SOURCE_DIR}/libs/core/include
                    ${boost_SOURCE_DIR}/libs/integer/include
                    ${boost_SOURCE_DIR}/libs/iterator/include
                    ${boost_SOURCE_DIR}/libs/lexical_cast/include
                    ${boost_SOURCE_DIR}/libs/move/include
                    ${boost_SOURCE_DIR}/libs/preprocessor/include
                    ${boost_SOURCE_DIR}/libs/program_options/include
                    ${boost_SOURCE_DIR}/libs/range/include
                    ${boost_SOURCE_DIR}/libs/static_assert/include
                    ${boost_SOURCE_DIR}/libs/throw_exception/include
                    ${boost_SOURCE_DIR}/libs/type_index/include
                    ${boost_SOURCE_DIR}/libs/type_traits/include
                    ${boost_SOURCE_DIR}/libs/utility/include
                    ${boost_SOURCE_DIR}/libs/variant/include

                    ${boost_SOURCE_DIR}/libs/algorithm/include
                    ${boost_SOURCE_DIR}/libs/array/include
                    ${boost_SOURCE_DIR}/libs/bind/include
                    ${boost_SOURCE_DIR}/libs/conversion/include
                    ${boost_SOURCE_DIR}/libs/detail/include
                    ${boost_SOURCE_DIR}/libs/function/include
                    ${boost_SOURCE_DIR}/libs/geometry/include
                    ${boost_SOURCE_DIR}/libs/graph/include
                    ${boost_SOURCE_DIR}/libs/math/include
                    ${boost_SOURCE_DIR}/libs/mpl/include
                    ${boost_SOURCE_DIR}/libs/multi_index/include
                    ${boost_SOURCE_DIR}/libs/multiprecision/include
                    ${boost_SOURCE_DIR}/libs/numeric/conversion/include
                    ${boost_SOURCE_DIR}/libs/parameter/include
                    ${boost_SOURCE_DIR}/libs/property_map/include
                    ${boost_SOURCE_DIR}/libs/qvm/include
                    ${boost_SOURCE_DIR}/libs/rational/include
                    ${boost_SOURCE_DIR}/libs/smart_ptr/include
                    ${boost_SOURCE_DIR}/libs/tokenizer/include
                    ${boost_SOURCE_DIR}/libs/tti/include
                    ${boost_SOURCE_DIR}/libs/unordered/include
                )
            endif()

            # TODO: support system installed libosmium
            if(NOT FETCH_DEPS)
                add_subdirectory(mapgen/earth/libosmium)
                set(OSMIUM_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mapgen/earth/libosmium/include)
            else()
                FetchContent_Declare(libosmium
                    GIT_REPOSITORY https://github.com/osmcode/libosmium
                    GIT_TAG v2.22.0
                    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/libosmium
                    SOURCE_SUBDIR cmake
                    GIT_SUBMODULES_RECURSE OFF

                    GIT_SHALLOW TRUE
                    OVERRIDE_FIND_PACKAGE TRUE
                    USES_TERMINAL_DOWNLOAD TRUE
                    GIT_PROGRESS TRUE
                    DOWNLOAD_EXTRACT_TIMESTAMP ON
                    EXCLUDE_FROM_ALL

                )
                FetchContent_MakeAvailable(libosmium)
                set(OSMIUM_INCLUDE_DIR ${libosmium_SOURCE_DIR}/include ${PROTOZERO_INCLUDE_DIR}/include)
            endif()

            include_directories(BEFORE SYSTEM ${OSMIUM_INCLUDE_DIR})

        endif()
        find_package(BZip2)
        if(BZIP2_FOUND)
            set(OSMIUM_LIRARY ${OSMIUM_LIRARY} BZip2::BZip2)
        endif()
        find_package(EXPAT)
        if(EXPAT_FOUND)
            set(OSMIUM_LIRARY ${OSMIUM_LIRARY} EXPAT::EXPAT)
        endif()
        set(OSMIUM_LIRARY ${OSMIUM_LIRARY} Boost::headers)
        set(USE_OSMIUM 1)
        message(STATUS "Using osmium: ${OSMIUM_INCLUDE_DIR} : ${OSMIUM_LIRARY}")
        set(FREEMINER_COMMON_LIBRARIES ${FREEMINER_COMMON_LIBRARIES} ${OSMIUM_LIRARY})

        option(ENABLE_OSMIUM_TOOL "Enable Osmium tool" 1)
        if(ENABLE_OSMIUM_TOOL)
            set(USE_OSMIUM_TOOL 1)
        endif()

        if(USE_OSMIUM_TOOL)
            add_subdirectory(mapgen/earth/json)
            set(NLOHMANN_INCLUDE_DIR mapgen/earth/json/include)
            include_directories(BEFORE SYSTEM ${NLOHMANN_INCLUDE_DIR})
            set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/mapgen/earth/osmium-tool/cmake/Modules/")
            # add_subdirectory(mapgen/earth/osmium-tool)
            set(OSMIUM_TOOL_SRC mapgen/earth/osmium-tool/src/)

            configure_file(${OSMIUM_TOOL_SRC}/version.cpp.in ${PROJECT_BINARY_DIR}/${OSMIUM_TOOL_SRC}/version.cpp)

            add_library(osmium-tool-lib
                ${PROJECT_BINARY_DIR}/${OSMIUM_TOOL_SRC}/version.cpp
                ${OSMIUM_TOOL_SRC}command_extract.cpp
                ${OSMIUM_TOOL_SRC}cmd.cpp
                ${OSMIUM_TOOL_SRC}cmd_factory.cpp
                ${OSMIUM_TOOL_SRC}id_file.cpp
                ${OSMIUM_TOOL_SRC}io.cpp
                ${OSMIUM_TOOL_SRC}util.cpp
                ${OSMIUM_TOOL_SRC}command_help.cpp
                ${OSMIUM_TOOL_SRC}option_clean.cpp
                ${OSMIUM_TOOL_SRC}export/export_format_json.cpp
                ${OSMIUM_TOOL_SRC}export/export_format_pg.cpp
                ${OSMIUM_TOOL_SRC}export/export_format_text.cpp
                ${OSMIUM_TOOL_SRC}export/export_handler.cpp
                ${OSMIUM_TOOL_SRC}extract/extract_bbox.cpp
                ${OSMIUM_TOOL_SRC}extract/extract.cpp
                ${OSMIUM_TOOL_SRC}extract/extract_polygon.cpp
                ${OSMIUM_TOOL_SRC}extract/geojson_file_parser.cpp
                ${OSMIUM_TOOL_SRC}extract/geometry_util.cpp
                ${OSMIUM_TOOL_SRC}extract/osm_file_parser.cpp
                ${OSMIUM_TOOL_SRC}extract/poly_file_parser.cpp
                ${OSMIUM_TOOL_SRC}extract/strategy_complete_ways.cpp
                ${OSMIUM_TOOL_SRC}extract/strategy_complete_ways_with_history.cpp
                ${OSMIUM_TOOL_SRC}extract/strategy_simple.cpp
                ${OSMIUM_TOOL_SRC}extract/strategy_smart.cpp
            )
            target_link_libraries(osmium-tool-lib
                PRIVATE ${OSMIUM_LIRARY}
                PUBLIC Boost::program_options)
            target_include_directories(osmium-tool-lib PRIVATE ${OSMIUM_INCLUDE_DIR})

            set(OSMIUM_TOOL_LIBRARY osmium-tool-lib)
            set(FREEMINER_COMMON_LIBRARIES ${FREEMINER_COMMON_LIBRARIES} ${OSMIUM_TOOL_LIBRARY})

        endif()
        message(STATUS "Using osmiumtool ${USE_OSMIUM_TOOL} : ${OSMIUM_TOOL_LIBRARY}")
    endif()
endif()

option(ENABLE_ICONV "Enable utf8 convert via iconv " FALSE)

if(ENABLE_ICONV)
    find_package(Iconv)
    if(ICONV_INCLUDE_DIR)
        set(USE_ICONV 1)
        message(STATUS "iconv.h found: ${ICONV_INCLUDE_DIR}")
    else()
        message(STATUS "iconv.h NOT found")
    endif()
endif()

if(NOT USE_ICONV)
    set(USE_ICONV 0)
endif()

#option(ENABLE_MANDELBULBER "Use Mandelbulber source to generate more fractals in math mapgen" OFF)
set(USE_MANDELBULBER 1)
#find_package(Mandelbulber)

option(ENABLE_IPV4_DEFAULT "Do not use ipv6 dual socket " FALSE)
if(ENABLE_IPV4_DEFAULT)
    set(USE_IPV4_DEFAULT 1)
else()
    set(USE_IPV4_DEFAULT 0)
endif()


if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND ${CMAKE_VERSION} VERSION_GREATER "3.11.0")
    set(USE_DEBUG_DUMP ON CACHE BOOL "")
endif()

if(USE_DEBUG_DUMP)
    #get_target_property(MAGIC_ENUM_INCLUDE_DIR ch_contrib::magic_enum INTERFACE_INCLUDE_DIRECTORIES)
    # CMake generator expression will do insane quoting when it encounters special character like quotes, spaces, etc.
    # Prefixing "SHELL:" will force it to use the original text.
    #set (INCLUDE_DEBUG_HELPERS "SHELL:-I\"${MAGIC_ENUM_INCLUDE_DIR}\" -include \"${ClickHouse_SOURCE_DIR}/base/base/dump.h\"")
    set(INCLUDE_DEBUG_HELPERS "SHELL:-I\"${CMAKE_CURRENT_SOURCE_DIR}/debug/\" -include \"${CMAKE_CURRENT_SOURCE_DIR}/debug/dump.h\"")
    #set (INCLUDE_DEBUG_HELPERS "SHELL:-include \"${CMAKE_CURRENT_SOURCE_DIR}/util/dump.h\"")
    # Use generator expression as we don't want to pollute CMAKE_CXX_FLAGS, which will interfere with CMake check system.
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:${INCLUDE_DEBUG_HELPERS}>)
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_definitions(-DDUMP_STREAM=actionstream)
    else()
        #add_definitions(-DDUMP_STREAM=verbosestream)
        add_definitions(-DDUMP_STREAM=actionstream)
    endif()
endif()

set(FMcommon_SRCS ${FMcommon_SRCS}
    circuit_element_virtual.cpp
    circuit_element.cpp
    circuit.cpp
    fm_abm_world.cpp
    fm_bitset.cpp
    fm_liquid.cpp
    fm_map.cpp
    fm_server.cpp
    fm_world_merge.cpp
    fm_far_calc.cpp
    key_value_storage.cpp
    log_types.cpp
    stat.cpp
    content_abm_grow_tree.cpp
    content_abm.cpp
    fm_abm.cpp
    fm_clientiface.cpp
    fm_serverenvironment.cpp
)

set(FREEMINER_COMMON_LIBRARIES ${FREEMINER_COMMON_LIBRARIES}
    ${MSGPACK_LIBRARY}
)

set(FREEMINER_CLIENT_LIBRARIES
    ${FREEMINER_COMMON_LIBRARIES}
)

find_package(PNG REQUIRED)

set(FREEMINER_SERVER_LIBRARIES
    ${FREEMINER_COMMON_LIBRARIES}
    ${PNG_LIBRARY}
)

# == end freeminer:
