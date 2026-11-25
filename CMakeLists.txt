cmake_minimum_required(VERSION 3.10)
project(Evolution CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(SFML 2 COMPONENTS graphics window system REQUIRED)
find_package(box2d REQUIRED)
find_package(Threads REQUIRED)

add_executable(Evolution
    main.cpp
    src/Game.cpp
    src/Creature.cpp
)

target_include_directories(Evolution PRIVATE include)
target_link_libraries(Evolution PRIVATE sfml-graphics sfml-window sfml-system box2d::box2d Threads::Threads)
