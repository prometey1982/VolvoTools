cmake_minimum_required(VERSION 3.16)
project(VolvoTools VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory (j2534)
add_subdirectory (Registry)
add_subdirectory (Common)
add_subdirectory (Logger)
add_subdirectory (Flasher)
add_subdirectory (argparse)
add_subdirectory (libintelhex)
add_subdirectory (VolvoFlasher)
add_subdirectory (VolvoLogger)
