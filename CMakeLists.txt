cmake_minimum_required(VERSION 3.10)
project(radius_bogenweichen)

add_subdirectory(parser)
generate_zusi_parser(ZusiParser ${CMAKE_CURRENT_BINARY_DIR}/zusi_parser)

add_executable(radius_bogenweichen radius_bogenweichen.cpp)
set_property(TARGET radius_bogenweichen PROPERTY CXX_STANDARD 17)
set_property(TARGET radius_bogenweichen PROPERTY CXX_STANDARD_REQUIRED TRUE)
target_link_libraries(radius_bogenweichen PRIVATE ZusiParser)
target_include_directories(radius_bogenweichen PRIVATE rapidxml)
target_compile_definitions(radius_bogenweichen PRIVATE -D_USE_MATH_DEFINES)
install(TARGETS radius_bogenweichen RUNTIME DESTINATION bin)
install(FILES weichen.txt DESTINATION bin)
