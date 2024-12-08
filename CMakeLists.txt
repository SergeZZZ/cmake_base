cmake_minimum_required(VERSION 3.10)
project(templ)

set(CMAKE_CXX_STANDARD 23)

set(NAME example)
#find_package(Boost)

add_executable(${NAME}
  main.cpp
  )

target_compile_options(${NAME} PRIVATE  "-Wall")
