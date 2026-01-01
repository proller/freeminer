
if(USE_OSMIUM)
    file(GLOB_RECURSE mapgen_earth_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/arnis-cpp/src/*.h" "${CMAKE_CURRENT_SOURCE_DIR}/arnis-cpp/src/*.cpp")
    set(mapgen_earth_SRCS
        ${mapgen_earth_SRCS}
        ${CMAKE_CURRENT_SOURCE_DIR}/arnis_adapter.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/blocks.cpp
    )
    message(STATUS "Using osmium + arnis")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/luanti-earth/native/src/voxelizer.h")
    option(USE_VOXEL_EARTH "Earth: Use voxel earth" 1)
endif()

set(mapgen_earth_SRCS ${mapgen_earth_SRCS}
    ${CMAKE_CURRENT_SOURCE_DIR}/hgt.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/http.cpp
)

if(USE_VOXEL_EARTH OR USE_OSMIUM_TOOL)
    add_subdirectory(json)
    set(NLOHMANN_INCLUDE_DIR json/include)
    include_directories(BEFORE SYSTEM ${NLOHMANN_INCLUDE_DIR})
endif()

if(USE_VOXEL_EARTH)
    set(TINYGLTF_BUILD_LOADER_EXAMPLE 0 CACHE INTERNAL "")
    set(TINYGLTF_INSTALL 0 CACHE INTERNAL "")
    set(TINYGLTF_INSTALL_VENDOR 0 CACHE INTERNAL "")

    include_directories(tinygltf)

    set(HAVE_NLOHMANN_JSON 1 CACHE INTERNAL "")
    add_subdirectory(luanti-earth/native)
    set(FREEMINER_CLIENT_LIBRARIES ${FREEMINER_CLIENT_LIBRARIES} earth_native PARENT_SCOPE)
    set(FREEMINER_SERVER_LIBRARIES ${FREEMINER_SERVER_LIBRARIES} earth_native PARENT_SCOPE)
endif()

set(freeminer_mapgen_SRCS ${freeminer_mapgen_SRCS}
    ${mapgen_earth_SRCS}
    PARENT_SCOPE
)
