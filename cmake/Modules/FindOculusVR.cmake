
FIND_PATH(OCULUSVR_INCLUDE_DIR OVR.h)

FIND_LIBRARY(OCULUSVR_LIBRARY NAMES OculusVR)
FIND_LIBRARY(XRANDR_LIBRARY NAMES Xrandr)



IF(OCULUSVR_LIBRARY AND OCULUSVR_INCLUDE_DIR)
	SET(OCULUSVR_FOUND 1)
ENDIF()

IF(OCULUSVR_FOUND)
	MESSAGE(STATUS "Found OculusVR header file in ${OCULUSVR_INCLUDE_DIR}")
	MESSAGE(STATUS "Found OculusVR library ${OCULUSVR_LIBRARY}")
ENDIF()
