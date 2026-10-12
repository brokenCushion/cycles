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

add_library(cycles_c SHARED cycles_c.cpp cycles_c.h)
target_include_directories(cycles_c PRIVATE ..)
target_compile_definitions(cycles_c PRIVATE CCL_C_API_BUILD)
target_link_libraries(cycles_c PRIVATE ${LIB})
cycles_c_keep_registrations(cycles_c)
set_target_properties(cycles_c PROPERTIES
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES
  WINDOWS_EXPORT_ALL_SYMBOLS OFF VERSION 1.1.0 SOVERSION 1)
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
