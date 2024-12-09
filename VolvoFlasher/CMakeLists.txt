cmake_minimum_required(VERSION 3.16)
project(VolvoFlasher VERSION 1.0.0 LANGUAGES CXX)

find_package(Boost REQUIRED)
 
aux_source_directory(${CMAKE_CURRENT_SOURCE_DIR}/src SUB_SOURCES)

set(SOURCE
  ${SOURCE}
  ${SUB_SOURCES}
)

add_executable(${PROJECT_NAME} ${SOURCE})

if (WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE   
       # Prevents Windows.h from adding unnecessary includes    
       WIN32_LEAN_AND_MEAN  
       # Prevents Windows.h from defining min/max as macros 
       NOMINMAX 
    )   
endif()
target_link_libraries(${PROJECT_NAME} Common j2534 boost::boost)
