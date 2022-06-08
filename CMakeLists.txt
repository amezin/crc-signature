cmake_minimum_required(VERSION 3.16)
project(signature)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Boost 1.71 REQUIRED COMPONENTS program_options)
find_package(Threads REQUIRED)

add_executable(${PROJECT_NAME} src/cli.cpp src/signature.cpp)
target_link_libraries(${PROJECT_NAME} PUBLIC Boost::program_options Threads::Threads)
target_compile_definitions(${PROJECT_NAME} PUBLIC _FILE_OFFSET_BITS=64)

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/.gitignore *)
