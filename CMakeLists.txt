cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED OBCX_CURRENT_PACKAGE)
  project(obcx_actor_bridge VERSION 0.2.0 LANGUAGES CXX)
  find_package(obcx-sdk CONFIG REQUIRED GLOBAL)
  include(OBCXPackages)
  obcx_load_configured_workspace()
  return()
endif()
set(OBCX_BRIDGE_BUILD_TESTS OFF)
if(OBCX_PACKAGE_PROFILE STREQUAL "tests")
  set(OBCX_BRIDGE_BUILD_TESTS ON)
endif()
option(OBCX_BRIDGE_CONFORMANCE_TESTS
       "Build installed Message Store/Bridge pipeline and reload smoke tests"
       OFF)
set(OBCX_MESSAGE_STORE_ACTOR_PATH
    ""
    CACHE FILEPATH
          "Installed Message Store actor used by Bridge conformance smoke tests"
)
set(OBCX_BRIDGE_CONFORMANCE_INSTALL_PREFIX
    ""
    CACHE PATH
          "Shared installed-SDK prefix used by Bridge conformance smoke tests")
set(OBCX_CORE_SOURCE_DIR
    ""
    CACHE PATH
          "OBCX source tree providing non-installed runtime conformance headers"
)

if(OBCX_BRIDGE_CONFORMANCE_TESTS AND NOT OBCX_BRIDGE_BUILD_TESTS)
  message(
    FATAL_ERROR "OBCX_BRIDGE_CONFORMANCE_TESTS requires OBCX_BRIDGE_BUILD_TESTS"
  )
endif()

include_directories(include)

add_subdirectory(src)

obcx_add_actor(SOURCES actor/bridge_actor.cpp)
target_link_libraries(bridge_actor PRIVATE bridge_core)
install(TARGETS bridge_actor LIBRARY DESTINATION lib/obcx/actors)
install(FILES package.toml DESTINATION share/obcx/packages/vollate.bridge)
target_include_directories(bridge_actor
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(OBCX_BRIDGE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
