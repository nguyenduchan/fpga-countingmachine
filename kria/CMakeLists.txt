cmake_minimum_required(VERSION 3.16)
project(kria_eth_camera VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)

add_executable(kria_eth_camera
    src/main.cpp
    src/config.cpp
    src/usb_camera.cpp
    src/mjpeg_decode.cpp
    src/eth_camera_server.cpp
    src/tile_balance.cpp
    src/fpga_tile.cpp
)

target_include_directories(kria_eth_camera PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${OpenCV_INCLUDE_DIRS}
)

# Board identity baked into the binary. Runtime file config/board.conf overrides
# these defaults when it is present, but the project setting remains here.
target_compile_definitions(kria_eth_camera PRIVATE
    KRIA_BOARD_NAME="Kria KV260"
    KRIA_UBUNTU_VERSION="24.04"
    KRIA_CAMERA_MODEL="OV9281"
)

target_link_libraries(kria_eth_camera PRIVATE
    ${OpenCV_LIBS}
    turbojpeg
    jpeg
    Threads::Threads
)

target_compile_options(kria_eth_camera PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS kria_eth_camera RUNTIME DESTINATION bin)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../config/board.conf
        DESTINATION etc/kria-ethernet-camera)
