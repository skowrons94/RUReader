cmake_minimum_required(VERSION 3.0)
project(RUReader VERSION 1.0 LANGUAGES CXX)

set(CMAKE_INSTALL_DIR /usr/bin/RUReader)

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(HEADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

find_package(ROOT REQUIRED COMPONENTS RIO Net)

include(${ROOT_USE_FILE})

include_directories(
  ${HEADER_DIR}
  ${ROOT_INCLUDE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
)

add_executable(RUReader
  ${SRC_DIR}/main.cc
  ${SRC_DIR}/RUReader.cc
  ${SRC_DIR}/DataFrame.cc
  ${SRC_DIR}/DataFrameBuilderPHA.cc
  ${SRC_DIR}/DataFrameBuilderPSD.cc
  ${SRC_DIR}/Utils.cc
)

target_compile_features(RUReader PUBLIC cxx_std_14)

target_link_libraries(RUReader PRIVATE ${ROOT_LIBRARIES})

install(TARGETS RUReader
  DESTINATION ${CMAKE_INSTALL_DIR})
