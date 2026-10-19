cmake_minimum_required(VERSION 3.16)
project(trucker_chat VERSION 0.1.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Platform-independent part (also built for the unit tests and the preview tool on Linux)
set(TC_CORE src/TextUtil.cpp src/Filter.cpp src/TwitchProtocol.cpp src/YouTubeProtocol.cpp src/Canvas.cpp src/Cards.cpp)

if(NOT WIN32)
    # Development on Linux/macOS: unit tests and the preview tool (renders the real card code with FreeType).
    add_executable(tc-tests tests/tests.cpp ${TC_CORE})
    target_include_directories(tc-tests PRIVATE src third_party)
    find_package(Freetype)
    find_package(ZLIB)
    if(FREETYPE_FOUND AND ZLIB_FOUND)
        add_executable(tc-preview tools/preview/preview.cpp ${TC_CORE})
        target_include_directories(tc-preview PRIVATE src third_party)
        target_link_libraries(tc-preview PRIVATE Freetype::Freetype ZLIB::ZLIB)
    endif()
    return()
endif()

enable_language(RC)
add_library(trucker-chat SHARED
    ${TC_CORE}
    src/Plugin.cpp src/Common.cpp src/Config.cpp src/EmbeddedData.cpp src/Http.cpp src/ChatSource.cpp
    src/TwitchSource.cpp src/YouTubeSource.cpp src/GlyphGdi.cpp src/D3D11Overlay.cpp src/ChatDisplay.cpp src/InputManager.cpp src/MenuWindow.cpp src/RadioLink.cpp
    ${CMAKE_BINARY_DIR}/resources.rc
    src/exports.def)
target_include_directories(trucker-chat PRIVATE src third_party sdk/include)
target_compile_definitions(trucker-chat PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS _WIN32_WINNT=0x0A00)
target_link_libraries(trucker-chat PRIVATE winhttp winmm user32 gdi32 comctl32 d3d11 dxgi dxguid)
if(MSVC)
    target_compile_options(trucker-chat PRIVATE /W3 /utf-8 /EHsc)
    set_property(TARGET trucker-chat PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")  # no VC++ redist needed
else()
    target_compile_options(trucker-chat PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
    # static C++ runtime; reproducible: no timestamp in the PE header, no absolute paths in the binary
    target_link_options(trucker-chat PRIVATE -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp)
    target_compile_options(trucker-chat PRIVATE -ffile-prefix-map=${CMAKE_SOURCE_DIR}=.)
endif()
set(TC_VERSION_LABEL "${PROJECT_VERSION} Alpha")
target_compile_definitions(trucker-chat PRIVATE "TC_VERSION=\"${TC_VERSION_LABEL}\"")
set_target_properties(trucker-chat PROPERTIES PREFIX "" OUTPUT_NAME "trucker-chat")

# Fonts and the default configuration are embedded into the DLL (src/resources.rc.in) and unpacked by the plugin itself.
set(TC_DATA ${CMAKE_SOURCE_DIR}/data)
set(TC_VERSION_COMMAS "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
configure_file(${CMAKE_SOURCE_DIR}/src/resources.rc.in ${CMAKE_BINARY_DIR}/resources.rc @ONLY)
set_source_files_properties(${CMAKE_BINARY_DIR}/resources.rc PROPERTIES OBJECT_DEPENDS
    "${TC_DATA}/fonts/Inter-Regular.otf;${TC_DATA}/fonts/Inter-Bold.otf;${TC_DATA}/fonts/OFL.txt;${TC_DATA}/trucker-chat.json")
