cmake_minimum_required(VERSION 3.22)

project(TTSLNativeServer LANGUAGES C CXX)

set(ttsl_version_project "${CMAKE_CURRENT_SOURCE_DIR}/../TTSL/TTSL.csproj")
file(READ "${ttsl_version_project}" ttsl_project_metadata)
string(REGEX MATCH "<Version>([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)</Version>"
    ttsl_project_version "${ttsl_project_metadata}")
if (NOT ttsl_project_version)
    message(FATAL_ERROR "The TTSL project must supply its four-part release version.")
endif()
set(ttsl_app_version "${CMAKE_MATCH_1}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${ttsl_version_project}")

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
    TTSL_APP_VERSION="${ttsl_app_version}"
)

if (MSVC)
    target_compile_options(ttsl-native-server PRIVATE /W4 /permissive- /utf-8)
endif()

target_link_libraries(ttsl-native-server PRIVATE
    miniz
    ws2_32
    shell32
    comdlg32
    comctl32
    ole32
    windowscodecs
    winhttp
    bcrypt
)
