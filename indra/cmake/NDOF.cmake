# -*- cmake -*-
include(Prebuilt)

if (STANDALONE)
  include(FindNDOF)
  if(NOT NDOF_FOUND)
    message(STATUS "Building without N-DoF joystick support")
  endif(NOT NDOF_FOUND)
else (STANDALONE)
  if (WINDOWS OR DARWIN)
    use_prebuilt_binary(libndofdev)
  elseif (LINUX)
    use_prebuilt_binary(libndofdev-open)
  endif (WINDOWS OR DARWIN)

  if (WINDOWS)
    set(NDOF_LIBRARY libndofdev)
  elseif (DARWIN OR LINUX)
    set(NDOF_LIBRARY ndofdev)
  endif (WINDOWS)

	if (WINDOWS)
      set(NDOF_INCLUDE_DIR ${LIBS_PREBUILT_DIR}/include)
	else (WINDOWS)
      set(NDOF_INCLUDE_DIR ${LIBS_PREBUILT_DIR}/include/ndofdev)
	endif (WINDOWS)
  set(NDOF_FOUND 1)
endif (STANDALONE)

if (NDOF_FOUND)
  add_definitions(-DLIB_NDOF=1)
  include_directories(${NDOF_INCLUDE_DIR})
else (NDOF_FOUND)
  message(STATUS "Building without N-DoF joystick support")
  set(NDOF_INCLUDE_DIR "")
  set(NDOF_LIBRARY "")
endif (NDOF_FOUND)
