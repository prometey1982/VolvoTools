cmake_minimum_required(VERSION 3.16)
project(VolvoFlasher VERSION 1.0.0 LANGUAGES CXX)

#find_package(Boost REQUIRED)
find_package(Easyloggingpp REQUIRED)
 
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src SUB_SOURCES)

set(SOURCE
  ${SOURCE}
  ${SUB_SOURCES}
)

FILE(GLOB HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h*)
FILE(GLOB PRIVATE_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h*)

add_executable(${PROJECT_NAME} ${SOURCE} ${HEADERS} ${PRIVATE_HEADERS})

if (WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE   
       # Prevents Windows.h from adding unnecessary includes    
       WIN32_LEAN_AND_MEAN  
       # Prevents Windows.h from defining min/max as macros 
       NOMINMAX 
    )   
endif()
target_link_libraries(${PROJECT_NAME} Common j2534 Flasher argparse easyloggingpp::easyloggingpp)
