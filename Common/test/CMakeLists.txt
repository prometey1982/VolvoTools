cmake_minimum_required(VERSION 3.16)
project(CommonTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

find_package(Boost REQUIRED COMPONENTS unit_test_framework)
find_package(Easyloggingpp REQUIRED)

add_executable(CommonTests
    D2MessageTest.cpp
    D2RequestTest.cpp
)
target_link_libraries(CommonTests Common Boost::unit_test_framework easyloggingpp::easyloggingpp)
target_include_directories(CommonTests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/Flasher/test
    ${CMAKE_SOURCE_DIR}/Common
)

if (WIN32)
    target_compile_definitions(CommonTests PRIVATE
       WIN32_LEAN_AND_MEAN
       NOMINMAX
    )
endif()

add_test(NAME D2Common COMMAND CommonTests --log_level=all)
