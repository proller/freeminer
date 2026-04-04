# Add PolyVox as a subdirectory
add_subdirectory(../src/external/PolyVox)

# Create a library target for PolyVox
add_library(PolyVox INTERFACE)

# Set include directories for PolyVox
target_include_directories(PolyVox INTERFACE
    ${PROJECT_SOURCE_DIR}/src/external/PolyVox/include
)

# Add PolyVox to the client libraries
list(APPEND FREEMINER_CLIENT_LIBRARIES PolyVox)

# Add PolyVox to the server libraries (if needed)
list(APPEND FREEMINER_SERVER_LIBRARIES PolyVox)

# Set global include directories for PolyVox
include_directories(${PROJECT_SOURCE_DIR}/src/external/PolyVox/include)