cmake_minimum_required(VERSION 3.16)
project(FlasherTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)

find_package(Boost REQUIRED COMPONENTS unit_test_framework)
find_package(Easyloggingpp REQUIRED)

add_executable(FlasherTests D2FlasherTest.cpp)
target_link_libraries(FlasherTests Flasher Common Boost::unit_test_framework easyloggingpp::easyloggingpp)
target_include_directories(FlasherTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if (WIN32)
    target_compile_definitions(FlasherTests PRIVATE
       WIN32_LEAN_AND_MEAN
       NOMINMAX
    )
endif()

add_test(NAME D2Flasher COMMAND FlasherTests --log_level=all)
