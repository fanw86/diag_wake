cmake_minimum_required(VERSION 3.16)
project(diag_wake VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(diag_wake
    src/main.cpp
    src/monitor.cpp
    src/eventlog.cpp
    src/report.cpp
)

target_link_libraries(diag_wake PRIVATE powrprof wevtapi ole32 oleaut32)

if(MSVC)
    target_compile_options(diag_wake PRIVATE /W3)
else()
    target_compile_options(diag_wake PRIVATE -Wall -Wextra)
endif()
