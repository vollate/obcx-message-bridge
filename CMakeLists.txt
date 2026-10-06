cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED OBCX_CURRENT_PACKAGE)
  project(obcx_actor_bridge VERSION 0.2.0 LANGUAGES CXX)
  find_package(obcx-sdk CONFIG REQUIRED GLOBAL)
  include(OBCXPackages)
  obcx_load_configured_workspace()
  return()
endif()
include_directories(include)

add_subdirectory(src)

obcx_add_actor(SOURCES actor/bridge_actor.cpp)
target_link_libraries(bridge_actor PRIVATE bridge_core)
install(TARGETS bridge_actor LIBRARY DESTINATION lib/obcx/actors)
install(FILES package.toml DESTINATION share/obcx/packages/vollate.bridge)
target_include_directories(bridge_actor
                           PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
