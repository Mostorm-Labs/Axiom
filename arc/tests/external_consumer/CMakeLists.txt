cmake_minimum_required(VERSION 3.30)
project(arc_external_consumer LANGUAGES CXX)

find_package(Arc 0.1 CONFIG REQUIRED)

add_executable(arc_external_consumer main.cpp)
target_compile_features(arc_external_consumer PRIVATE cxx_std_20)
target_link_libraries(arc_external_consumer PRIVATE Arc::Core Arc::Headless)
