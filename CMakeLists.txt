cmake_minimum_required (VERSION 3.5)

project(audio_transport)

set(CMAKE_CXX_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
  set (CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

#Adding cmake modules
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/modules/)

#######################
## Internal Libraries
#######################

include_directories(include)
file(GLOB PROJECT_SOURCES src/*.cpp)

#######################
## External Libraries
#######################

# FFTW3
find_package(FFTW REQUIRED)
include_directories(${FFTW_INCLUDES})
set(LIBS ${LIBS} ${FFTW_LIBRARIES})

####################
## Library Creation
####################

add_library(${PROJECT_NAME} SHARED ${PROJECT_SOURCES})
target_link_libraries(${PROJECT_NAME} ${LIBS})
set(LIBS ${LIBS} ${PROJECT_NAME})

################
## Installation
################

install(TARGETS ${PROJECT_NAME}
        DESTINATION lib)
install(DIRECTORY include/${PROJECT_NAME}
        DESTINATION include)

#####################################
## Unit Tests
#####################################

option(BUILD_EXAMPLES "BUILD_EXAMPLES" OFF)
if (BUILD_EXAMPLES)
  # Find audiorw
  find_package(AUDIORW REQUIRED)
  include_directories(${AUDIORW_INCLUDE})
  set(LIBS ${LIBS} ${AUDIORW_LIBRARY})

  # from list of files we'll create examples example_name.cpp -> example_name
  file(GLOB EXAMPLE_SOURCES example/*.cpp)
  foreach(_example_file ${EXAMPLE_SOURCES})
      get_filename_component(_example_name ${_example_file} NAME_WE)
      add_executable(${_example_name} ${_example_file})
      target_link_libraries(${_example_name} ${LIBS})
  endforeach()
endif()

#####################################
## Unit Tests (no external deps)
#####################################

option(BUILD_TESTS "BUILD_TESTS" OFF)
if (BUILD_TESTS)
  # Build tests that don't require audiorw
  add_executable(test_realtime_transport test/test_realtime_transport.cpp)
  target_link_libraries(test_realtime_transport ${PROJECT_NAME} ${FFTW_LIBRARIES})

  # Add other standalone tests
  file(GLOB TEST_SOURCES test/test_*.cpp)
  foreach(_test_file ${TEST_SOURCES})
      get_filename_component(_test_name ${_test_file} NAME_WE)
      if(NOT _test_name STREQUAL "test_realtime_transport")
          add_executable(${_test_name} ${_test_file})
          target_link_libraries(${_test_name} ${PROJECT_NAME} ${FFTW_LIBRARIES})
      endif()
  endforeach()
endif()
