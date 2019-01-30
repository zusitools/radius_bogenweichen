cmake_minimum_required(VERSION 3.13)

add_subdirectory(parser)
generate_zusi_parser(ZusiParser ${CMAKE_CURRENT_BINARY_DIR}/zusi_parser)

add_executable(radius_bogenweichen radius_bogenweichen.cpp)
target_link_libraries(radius_bogenweichen PRIVATE ZusiParser)
