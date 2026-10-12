# SPDX-License-Identifier: Apache-2.0
set(LIB cycles_device cycles_kernel cycles_scene cycles_session cycles_bvh
        cycles_subd cycles_graph cycles_util)
if(WITH_CYCLES_OSL)
  list(APPEND LIB cycles_kernel_osl)
endif()
if(CYCLES_STANDALONE_REPOSITORY)
  list(APPEND LIB extern_sky)
else()
  list(APPEND LIB bf_intern_sky)
endif()
cycles_external_libraries_append(LIB)

# NODE_DEFINE registration must survive archive-member dead stripping.
function(cycles_c_keep_registrations target)
  foreach(library cycles_scene cycles_session)
    if(MSVC)
      target_link_options(${target} PRIVATE "/WHOLEARCHIVE:$<TARGET_FILE:${library}>")
    elseif(APPLE)
      target_link_options(${target} PRIVATE "LINKER:-force_load,$<TARGET_FILE:${library}>")
    else()
      target_link_options(${target} PRIVATE "LINKER:--whole-archive,$<TARGET_FILE:${library}>,--no-whole-archive")
    endif()
  endforeach()
endfunction()

if(WIN32)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate_dispatch.py
    ${CMAKE_CURRENT_SOURCE_DIR}/cycles_c.h ${CMAKE_CURRENT_BINARY_DIR}/dispatch
    COMMAND_ERROR_IS_FATAL ANY)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS cycles_c.h generate_dispatch.py)
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_capi_dependencies ${PLATFORM_BUNDLED_LIBRARIES_DEBUG})
  else()
    set(_capi_dependencies ${PLATFORM_BUNDLED_LIBRARIES_RELEASE})
  endif()
  list(JOIN _capi_dependencies "\n" _capi_dependency_paths)
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/dispatch/dependency-paths.txt "${_capi_dependency_paths}\n")
  execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/generate_versions.py
    ${CMAKE_CURRENT_BINARY_DIR}/dispatch/dependency-paths.txt
    ${CMAKE_CURRENT_BINARY_DIR}/dispatch/dependency_versions.inc COMMAND_ERROR_IS_FATAL ANY)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS generate_versions.py ${_capi_dependencies})
  add_library(cycles_c_backend_private SHARED cycles_c.cpp backend.cpp)
  target_include_directories(cycles_c_backend_private PRIVATE .. ${CMAKE_CURRENT_BINARY_DIR}/dispatch ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(cycles_c_backend_private PRIVATE ${LIB})
  cycles_c_keep_registrations(cycles_c_backend_private)
  set_target_properties(cycles_c_backend_private PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS OFF)
  add_library(cycles_c SHARED front.cpp)
  target_include_directories(cycles_c PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/dispatch ${CMAKE_CURRENT_SOURCE_DIR})
  # The front owns no native objects; its static C++ runtime imports only Windows APIs.
  target_compile_options(cycles_c PRIVATE "$<IF:$<CONFIG:Debug>,/MTd,/MT>")
  target_link_libraries(cycles_c PRIVATE version)
  add_dependencies(cycles_c cycles_c_backend_private)
  install(TARGETS cycles_c_backend_private RUNTIME DESTINATION .)
else()
  add_library(cycles_c SHARED cycles_c.cpp)
  target_include_directories(cycles_c PRIVATE ..)
  target_link_libraries(cycles_c PRIVATE ${LIB})
  cycles_c_keep_registrations(cycles_c)
endif()
target_compile_definitions(cycles_c PRIVATE CCL_C_API_BUILD)
set_target_properties(cycles_c PROPERTIES
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES
  WINDOWS_EXPORT_ALL_SYMBOLS OFF VERSION 1.7.0 SOVERSION 1)
# Localize linked static-library symbols as well as the wrapper's own symbols.
if(APPLE)
  set_target_properties(cycles_c PROPERTIES INSTALL_RPATH "@loader_path")
  set_property(TARGET cycles_c APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/exports.apple)
  target_link_options(cycles_c PRIVATE "LINKER:-exported_symbols_list,${CMAKE_CURRENT_SOURCE_DIR}/exports.apple")
elseif(UNIX)
  set_target_properties(cycles_c PROPERTIES INSTALL_RPATH "$ORIGIN")
  set_property(TARGET cycles_c APPEND PROPERTY LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/exports.map)
  target_link_options(cycles_c PRIVATE "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map")
endif()
# Windows dependency DLLs use the existing install-root convention.
install(TARGETS cycles_c RUNTIME DESTINATION . LIBRARY DESTINATION lib ARCHIVE DESTINATION lib)
install(FILES cycles_c.h DESTINATION include)
cycles_install_libraries(cycles_c)

if(BUILD_TESTING)
  add_executable(cycles_c_host test/host.c)
  set_target_properties(cycles_c_host PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
  target_link_libraries(cycles_c_host PRIVATE ${CMAKE_DL_LIBS})
  add_test(NAME cycles_c_host COMMAND cycles_c_host $<TARGET_FILE:cycles_c>
    ${CMAKE_INSTALL_PREFIX} ${CMAKE_CURRENT_BINARY_DIR})

  add_executable(cycles_c_contract test/contract.cpp)
  target_include_directories(cycles_c_contract PRIVATE ..)
  target_link_libraries(cycles_c_contract PRIVATE ${LIB})
  add_test(NAME cycles_c_contract COMMAND cycles_c_contract)

  add_executable(cycles_c_native_dump test/native_dump.cpp)
  target_include_directories(cycles_c_native_dump PRIVATE ..)
  target_link_libraries(cycles_c_native_dump PRIVATE ${LIB})
  cycles_c_keep_registrations(cycles_c_native_dump)

  add_executable(cycles_c_scene_host test/scene_host.c)
  set_target_properties(cycles_c_scene_host PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
  target_link_libraries(cycles_c_scene_host PRIVATE cycles_c)
  add_test(NAME cycles_c_scene_host COMMAND cycles_c_scene_host ${CMAKE_INSTALL_PREFIX})

  add_executable(cycles_c_editing test/editing.cpp)
  target_include_directories(cycles_c_editing PRIVATE ..)
  target_link_libraries(cycles_c_editing PRIVATE ${LIB})
  cycles_c_keep_registrations(cycles_c_editing)
  add_test(NAME cycles_c_editing COMMAND cycles_c_editing ${CMAKE_INSTALL_PREFIX})

  if(WITH_CYCLES_OSL AND WITH_CYCLES_PUGIXML)
    add_executable(cycles_c_shaders test/shaders.cpp)
    target_include_directories(cycles_c_shaders PRIVATE ..)
    target_link_libraries(cycles_c_shaders PRIVATE ${LIB})
    cycles_c_keep_registrations(cycles_c_shaders)
    add_test(NAME cycles_c_shaders COMMAND cycles_c_shaders ${CMAKE_INSTALL_PREFIX}
      ${CMAKE_SOURCE_DIR}/examples ${CMAKE_CURRENT_BINARY_DIR}/shader-tests)
    set_tests_properties(cycles_c_shaders PROPERTIES TIMEOUT 120)
  endif()

  add_executable(cycles_c_geometry test/geometry.cpp)
  target_include_directories(cycles_c_geometry PRIVATE ..)
  target_link_libraries(cycles_c_geometry PRIVATE ${LIB})
  cycles_c_keep_registrations(cycles_c_geometry)
  add_test(NAME cycles_c_geometry COMMAND cycles_c_geometry ${CMAKE_INSTALL_PREFIX}
    ${CMAKE_CURRENT_BINARY_DIR}/geometry-tests)
  set_tests_properties(cycles_c_geometry PROPERTIES TIMEOUT 120)

  if(WITH_CYCLES_OSL AND WITH_CYCLES_PUGIXML)
    add_executable(cycles_c_rendering test/rendering.cpp)
    target_include_directories(cycles_c_rendering PRIVATE ..)
    target_link_libraries(cycles_c_rendering PRIVATE cycles_c ${LIB})
    cycles_c_keep_registrations(cycles_c_rendering)
    add_test(NAME cycles_c_rendering COMMAND cycles_c_rendering ${CMAKE_INSTALL_PREFIX}
      ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/rendering-tests CPU)
    set_tests_properties(cycles_c_rendering PROPERTIES TIMEOUT 300)
    if(WITH_CYCLES_DEEP_OPAQUE)
      add_executable(cycles_c_deep test/deep.cpp)
      target_include_directories(cycles_c_deep PRIVATE ..)
      target_link_libraries(cycles_c_deep PRIVATE cycles_c ${LIB} cycles_deep_exr)
      cycles_c_keep_registrations(cycles_c_deep)
      add_test(NAME cycles_c_deep COMMAND cycles_c_deep ${CMAKE_INSTALL_PREFIX}
        ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/deep-tests)
      set_tests_properties(cycles_c_deep PROPERTIES TIMEOUT 180)
    endif()
  endif()

  add_executable(cycles_c_controls test/controls.cpp)
  target_include_directories(cycles_c_controls PRIVATE ..)
  target_link_libraries(cycles_c_controls PRIVATE ${LIB})
  cycles_c_keep_registrations(cycles_c_controls)
  add_test(NAME cycles_c_controls COMMAND cycles_c_controls ${CMAKE_INSTALL_PREFIX}
    ${CMAKE_CURRENT_BINARY_DIR}/control-tests)
  set_tests_properties(cycles_c_controls PROPERTIES TIMEOUT 120)

  add_executable(cycles_c_benchmark test/benchmark.cpp)
  target_include_directories(cycles_c_benchmark PRIVATE ..)
  target_link_libraries(cycles_c_benchmark PRIVATE ${LIB} ${CMAKE_DL_LIBS})
  add_test(NAME cycles_c_benchmark COMMAND cycles_c_benchmark $<TARGET_FILE:cycles_c>
    ${CMAKE_INSTALL_PREFIX} ${CMAKE_CURRENT_BINARY_DIR}/benchmark.json)
  set_tests_properties(cycles_c_scene_host cycles_c_editing cycles_c_benchmark PROPERTIES TIMEOUT 120)

  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME cycles_c_source_inventory COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/test/source_inventory.py ${CMAKE_CURRENT_SOURCE_DIR}/..
    $<TARGET_FILE:cycles_c> ${CMAKE_CURRENT_BINARY_DIR}/source-inventory.json)
  add_test(NAME cycles_c_reflection COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/test/compare_reflection.py
    $<TARGET_FILE:cycles_c_host> $<TARGET_FILE:cycles_c_native_dump> $<TARGET_FILE:cycles_c>
    ${CMAKE_INSTALL_PREFIX} ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_BINARY_DIR}/reflection.json)
  if(WIN32)
    find_program(CCL_EXPORT_TOOL NAMES dumpbin REQUIRED)
  else()
    find_program(CCL_EXPORT_TOOL NAMES nm REQUIRED)
  endif()
  add_test(NAME cycles_c_exports COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/test/check_exports.py ${CCL_EXPORT_TOOL} $<TARGET_FILE:cycles_c>)
  set_tests_properties(cycles_c_host cycles_c_contract cycles_c_exports cycles_c_source_inventory
    cycles_c_reflection PROPERTIES TIMEOUT 60)
endif()
