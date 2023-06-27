cmake_minimum_required(VERSION 3.22)
project(OSPRayAsAService)


find_package(ospray REQUIRED)


add_executable(osprayAsAService
    src/main.cpp
    external/stb/stb_image_write.h
)
target_link_libraries(osprayAsAService
    PUBLIC
        ospray::ospray
)
target_include_directories(osprayAsAService
    PUBLIC
        external/stb
)
