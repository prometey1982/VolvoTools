cmake_minimum_required(VERSION 3.16)
project(Common VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR} SUB_SOURCES)

set(SOURCE
  ${SOURCE}
  ${SUB_SOURCES}
)

add_library(${PROJECT_NAME} ${SOURCE})

if (WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE   
       # Prevents Windows.h from adding unnecessary includes    
       WIN32_LEAN_AND_MEAN  
       # Prevents Windows.h from defining min/max as macros 
       NOMINMAX 
    )   
endif()

target_include_directories(${PROJECT_NAME} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
