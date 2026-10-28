cmake_minimum_required(VERSION 3.26)
project(IntelNPUTest VERSION 2.0.0 LANGUAGES CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "This deployment target is Windows x64 only")
endif()

find_package(OpenVINO REQUIRED)

add_library(npu_test_engine STATIC src/test_engine.cpp src/test_engine.h)
target_compile_features(npu_test_engine PUBLIC cxx_std_17)
target_include_directories(npu_test_engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(npu_test_engine PUBLIC openvino::runtime)

target_compile_definitions(npu_test_engine PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

add_executable(IntelNPUTestCLI src/cli_main.cpp)
target_link_libraries(IntelNPUTestCLI PRIVATE npu_test_engine)
target_compile_definitions(IntelNPUTestCLI PRIVATE UNICODE _UNICODE)

add_executable(IntelNPUTest WIN32 src/gui_main.cpp)
target_link_libraries(IntelNPUTest PRIVATE npu_test_engine comdlg32 user32 gdi32 shell32)
target_compile_definitions(IntelNPUTest PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

if(MSVC)
    foreach(t npu_test_engine IntelNPUTestCLI IntelNPUTest)
        target_compile_options(${t} PRIVATE /W4 /utf-8 /EHsc)
    endforeach()
    set_property(TARGET IntelNPUTestCLI PROPERTY LINK_FLAGS "/SUBSYSTEM:CONSOLE")
endif()
