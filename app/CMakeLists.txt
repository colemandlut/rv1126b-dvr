cmake_minimum_required(VERSION 3.16)
project(rv1126b_dvr CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

# nlohmann/json 是纯头文件库；交叉编译时从宿主机目录取
find_path(NLOHMANN_INCLUDE nlohmann/json.hpp PATHS /usr/include NO_CMAKE_FIND_ROOT_PATH REQUIRED)

add_library(dvrcore STATIC
  src/config.cpp
  src/log.cpp
  src/storage.cpp
  src/pipeline.cpp
  src/gpio.cpp
  src/watchdog.cpp
  src/recorder.cpp)
target_include_directories(dvrcore PUBLIC src ${NLOHMANN_INCLUDE})

add_executable(dvr-record src/main.cpp)
target_link_libraries(dvr-record dvrcore)

option(DVR_STATIC "静态链接，避免依赖板上 libstdc++ 版本" ON)
if(DVR_STATIC AND CMAKE_CROSSCOMPILING)
  target_link_options(dvr-record PRIVATE -static)
endif()

option(DVR_TESTS "编译单元测试（宿主机运行）" OFF)
if(DVR_TESTS)
  enable_testing()
  add_executable(test_storage tests/test_storage.cpp)
  target_link_libraries(test_storage dvrcore)
  add_test(NAME storage COMMAND test_storage)
  add_executable(test_recorder tests/test_recorder.cpp)
  target_link_libraries(test_recorder dvrcore)
  target_compile_definitions(test_recorder PRIVATE DVR_SAMPLE_CONFIG="${CMAKE_SOURCE_DIR}/config/dvr.json")
  add_test(NAME recorder COMMAND test_recorder)
endif()
