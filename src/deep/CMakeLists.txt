# SPDX-License-Identifier: Apache-2.0

# May also be configured on its own, without Cycles or shader dependencies.
cmake_minimum_required(VERSION 3.10)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(CyclesDeepReference LANGUAGES CXX)
  include(CTest)
endif()

add_library(cycles_deep_reference STATIC reconstruction.cpp volume.cpp)
target_compile_features(cycles_deep_reference PUBLIC cxx_std_17)

add_library(cycles_deep_capture STATIC capture.cpp capture.h)
target_include_directories(cycles_deep_capture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(cycles_deep_capture PUBLIC cxx_std_17)
target_link_libraries(cycles_deep_capture PUBLIC cycles_deep_reference)
target_include_directories(cycles_deep_reference PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

if(BUILD_TESTING)
  find_package(Threads REQUIRED)
  add_executable(cycles_deep_capture_test capture_test.cpp)
  target_link_libraries(cycles_deep_capture_test PRIVATE cycles_deep_capture Threads::Threads)
  add_test(NAME cycles_deep_capture COMMAND cycles_deep_capture_test)
  add_executable(cycles_deep_reference_test reconstruction_test.cpp)
  target_link_libraries(cycles_deep_reference_test PRIVATE cycles_deep_reference)
  add_test(NAME cycles_deep_reference COMMAND cycles_deep_reference_test)
  add_executable(cycles_deep_density_cell_test density_cell_test.cpp)
  target_link_libraries(cycles_deep_density_cell_test PRIVATE cycles_deep_reference)
  add_test(NAME cycles_deep_density_cell COMMAND cycles_deep_density_cell_test)
endif()

option(WITH_CYCLES_DEEP_EXR_TESTS "Build synthetic deep OpenEXR writer and tests" OFF)
if(WITH_CYCLES_DEEP_EXR_TESTS OR WITH_CYCLES_DEEP_OPAQUE)
  if(NOT TARGET OpenEXR::OpenEXR)
    find_package(OpenEXR CONFIG REQUIRED)
  endif()
  add_library(cycles_deep_exr STATIC exr_writer.cpp publication.cpp)
  target_link_libraries(cycles_deep_exr PUBLIC cycles_deep_reference OpenEXR::OpenEXR)
  if(BUILD_TESTING)
    add_executable(cycles_deep_production_test production_test.cpp)
    target_link_libraries(cycles_deep_production_test PRIVATE cycles_deep_exr cycles_deep_capture Threads::Threads)
    add_test(NAME cycles_deep_production COMMAND cycles_deep_production_test ${CMAKE_CURRENT_BINARY_DIR}/production-fixtures)
    add_executable(cycles_deep_exr_test exr_writer_test.cpp)
    target_link_libraries(cycles_deep_exr_test PRIVATE cycles_deep_exr)
    add_executable(cycles_deep_volume_test volume_test.cpp)
    target_link_libraries(cycles_deep_volume_test PRIVATE cycles_deep_exr)
    add_test(NAME cycles_deep_volume
      COMMAND cycles_deep_volume_test ${CMAKE_CURRENT_BINARY_DIR}/volume-fixtures)
    add_executable(cycles_deep_density_test density_test.cpp)
    target_link_libraries(cycles_deep_density_test PRIVATE cycles_deep_exr)
    add_test(NAME cycles_deep_density
      COMMAND cycles_deep_density_test ${CMAKE_CURRENT_BINARY_DIR}/density-fixtures)
    if(WIN32)
      if(CMAKE_VERSION VERSION_LESS 3.21)
        message(FATAL_ERROR "Deep EXR Windows test DLL staging requires CMake 3.21+")
      endif()
      foreach(deep_test cycles_deep_exr_test cycles_deep_production_test cycles_deep_volume_test cycles_deep_density_test)
        add_custom_command(TARGET ${deep_test} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:${deep_test}> $<TARGET_FILE_DIR:${deep_test}>
          COMMAND_EXPAND_LISTS)
      endforeach()
    endif()
    add_test(NAME cycles_deep_exr
      COMMAND cycles_deep_exr_test ${CMAKE_CURRENT_BINARY_DIR}/fixtures)
  endif()
endif()
