cmake_minimum_required(VERSION 3.22)
project(Tapestry)


find_package(ospray REQUIRED)
find_package(Python COMPONENTS Interpreter REQUIRED)


add_executable(engine
    src/engine/main.cpp
    external/stb/stb_image_write.h
)
target_link_libraries(engine
    PUBLIC
        ospray::ospray
)
target_include_directories(engine
    PRIVATE
        external/stb
)

add_custom_command(
    OUTPUT
        "${CMAKE_CURRENT_BINARY_DIR}/server.pyz"
    BYPRODUCTS
        "${CMAKE_CURRENT_BINARY_DIR}/server"
    DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/requirements.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/server/main.py"
    COMMAND
        "${Python_EXECUTABLE}" -m pip install
            -r "${CMAKE_CURRENT_SOURCE_DIR}/requirements.txt"
            --target "${CMAKE_CURRENT_BINARY_DIR}/server"
            --upgrade
    COMMAND
        "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/src"
            "${CMAKE_CURRENT_BINARY_DIR}/server/tapestry"
    COMMAND
        "${Python_EXECUTABLE}" -m zipapp
            --output="${CMAKE_CURRENT_BINARY_DIR}/server.pyz"
            "--python=/usr/bin/env python3"
            --main=tapestry.server.main:cli
            "${CMAKE_CURRENT_BINARY_DIR}/server"
)

add_custom_target(server
    ALL
    DEPENDS
        "${CMAKE_CURRENT_BINARY_DIR}/server.pyz"
)

install(
    PROGRAMS
        "${CMAKE_CURRENT_BINARY_DIR}/engine"
    TYPE BIN
    RENAME
        "tapestryEngine"
)

install(
    PROGRAMS
        "${CMAKE_CURRENT_BINARY_DIR}/server.pyz"
    TYPE BIN
    RENAME
        "tapestryServer"
)