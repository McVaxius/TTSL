cmake_minimum_required(VERSION 3.22)

project(TTSLNativeServer LANGUAGES C CXX)

add_library(miniz STATIC
    third_party/miniz/miniz.c
    third_party/miniz/miniz_tdef.c
    third_party/miniz/miniz_tinfl.c
    third_party/miniz/miniz_zip.c
)

target_include_directories(miniz PUBLIC
    third_party/miniz
)

add_executable(ttsl-native-server WIN32
    src/main.cpp
    src/app.rc
)

target_compile_features(ttsl-native-server PRIVATE cxx_std_20)
target_compile_definitions(ttsl-native-server PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if (MSVC)
    target_compile_options(ttsl-native-server PRIVATE /W4 /permissive-)
endif()

target_link_libraries(ttsl-native-server PRIVATE
    miniz
    ws2_32
    shell32
    comctl32
    ole32
    windowscodecs
    winhttp
    bcrypt
)
