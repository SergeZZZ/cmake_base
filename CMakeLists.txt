cmake_minimum_required(VERSION 3.10)
project(lock_free_queue)

set(CMAKE_CXX_STANDARD 23)

set(NAME lock_free_queue)
#find_package(Boost)

add_executable(${NAME}
  main.cpp
  )

target_compile_options(${NAME} PRIVATE  "-Wall")
