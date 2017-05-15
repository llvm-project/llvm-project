IF (NOT CUDA_HEADER_PREFIX)
  SET(CUDA_HEADER_PREFIX "/usr/local/cuda/include")
ENDIF (NOT CUDA_HEADER_PREFIX)

FIND_PATH(CUDALIB_INCLUDE_DIR
  NAMES cuda.h
  PATHS ${CUDA_HEADER_PREFIX})

FIND_LIBRARY(CUDALIB_LIBRARY NAMES cuda)

IF (CUDALIB_INCLUDE_DIR)
  SET(CUDALIB_FOUND TRUE)
ENDIF (CUDALIB_INCLUDE_DIR)

IF (CUDALIB_FOUND)
  IF (NOT CUDA_FIND_QUIETLY)
    MESSAGE(STATUS "Found CUDA: ${CUDALIB_INCLUDE_DIR}")
  ENDIF (NOT CUDA_FIND_QUIETLY)
ELSE (CUDALIB_FOUND)
  IF (CUDA_FIND_REQUIRED)
    MESSAGE(FATAL_ERROR "Could not find CUDA")
  ENDIF (CUDA_FIND_REQUIRED)
ENDIF (CUDALIB_FOUND)
