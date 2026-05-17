cmake_minimum_required(VERSION 3.24)

project(MediaStudio VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(MEDIASTUDIO_ENABLE_MYSQL "Enable MySQL media library support" OFF)
option(MEDIASTUDIO_DEPLOY_RUNTIME "Copy runtime DLLs after build" ON)

set(MEDIASTUDIO_QT_ROOT "" CACHE PATH "Qt installation root")
set(MEDIASTUDIO_FFMPEG_ROOT "" CACHE PATH "FFmpeg installation root")
set(MEDIASTUDIO_GLEW_ROOT "" CACHE PATH "GLEW installation root")
set(MEDIASTUDIO_MYSQL_CONNECTOR_ROOT "" CACHE PATH "MySQL Connector/C root")
set(MEDIASTUDIO_MYSQL_SERVER_ROOT "" CACHE PATH "MySQL Server root")

if(EXISTS "${MEDIASTUDIO_QT_ROOT}")
    list(APPEND CMAKE_PREFIX_PATH "${MEDIASTUDIO_QT_ROOT}")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
find_package(OpenGL REQUIRED)

set(MEDIASTUDIO_SOURCES
    src/main.cpp
    src/MediaDecoder.cpp
    src/AudioOutput.cpp
    src/VideoRenderer.cpp
    src/AudioVisualizer.cpp
    src/MediaTranscoder.cpp
    src/PacketQueue.cpp
    src/PlayerController.cpp
    src/VideoWidget.cpp
    src/MainWindow.cpp
)

set(MEDIASTUDIO_HEADERS
    src/MediaInfo.h
    src/MediaDecoder.h
    src/AudioOutput.h
    src/VideoRenderer.h
    src/AudioVisualizer.h
    src/MediaTranscoder.h
    src/PacketQueue.h
    src/MediaDatabase.h
    src/PlayerController.h
    src/VideoWidget.h
    src/MainWindow.h
)

if(MEDIASTUDIO_ENABLE_MYSQL)
    list(APPEND MEDIASTUDIO_SOURCES src/MediaDatabase.cpp)
endif()

qt_standard_project_setup()
qt_add_executable(MediaStudio WIN32
    ${MEDIASTUDIO_SOURCES}
    ${MEDIASTUDIO_HEADERS}
)

target_compile_definitions(MediaStudio PRIVATE
    QT_OPENGLWIDGETS_LIB
    WIN32_LEAN_AND_MEAN
    MEDIASTUDIO_ENABLE_MYSQL=$<BOOL:${MEDIASTUDIO_ENABLE_MYSQL}>
)

target_include_directories(MediaStudio PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${MEDIASTUDIO_FFMPEG_ROOT}/include"
    "${MEDIASTUDIO_GLEW_ROOT}/include"
)

function(mediastudio_import_unknown_library target_name library_path)
    if(NOT EXISTS "${library_path}")
        message(FATAL_ERROR "Required library not found: ${library_path}")
    endif()
    add_library(${target_name} UNKNOWN IMPORTED)
    set_target_properties(${target_name} PROPERTIES IMPORTED_LOCATION "${library_path}")
endfunction()

set(MEDIASTUDIO_FFMPEG_COMPONENTS
    avcodec
    avformat
    avdevice
    avutil
    avfilter
    swresample
    swscale
)

set(MEDIASTUDIO_FFMPEG_TARGETS)
foreach(component IN LISTS MEDIASTUDIO_FFMPEG_COMPONENTS)
    if(MINGW AND EXISTS "${MEDIASTUDIO_FFMPEG_ROOT}/lib/lib${component}.dll.a")
        set(component_library "${MEDIASTUDIO_FFMPEG_ROOT}/lib/lib${component}.dll.a")
    elseif(EXISTS "${MEDIASTUDIO_FFMPEG_ROOT}/lib/${component}.lib")
        set(component_library "${MEDIASTUDIO_FFMPEG_ROOT}/lib/${component}.lib")
    elseif(EXISTS "${MEDIASTUDIO_FFMPEG_ROOT}/lib/lib${component}.dll.a")
        set(component_library "${MEDIASTUDIO_FFMPEG_ROOT}/lib/lib${component}.dll.a")
    else()
        message(FATAL_ERROR "FFmpeg library not found: ${component}")
    endif()

    set(component_target "FFmpeg::${component}")
    mediastudio_import_unknown_library(${component_target} "${component_library}")
    list(APPEND MEDIASTUDIO_FFMPEG_TARGETS ${component_target})
endforeach()

if(MINGW AND EXISTS "${MEDIASTUDIO_GLEW_ROOT}/lib/Release/x64/libglew32.dll.a")
    set(MEDIASTUDIO_GLEW_LIBRARY "${MEDIASTUDIO_GLEW_ROOT}/lib/Release/x64/libglew32.dll.a")
elseif(EXISTS "${MEDIASTUDIO_GLEW_ROOT}/lib/Release/x64/glew32.lib")
    set(MEDIASTUDIO_GLEW_LIBRARY "${MEDIASTUDIO_GLEW_ROOT}/lib/Release/x64/glew32.lib")
elseif(EXISTS "${MEDIASTUDIO_GLEW_ROOT}/bin/Release/x64/glew32.dll")
    set(MEDIASTUDIO_GLEW_LIBRARY "${MEDIASTUDIO_GLEW_ROOT}/bin/Release/x64/glew32.dll")
else()
    message(FATAL_ERROR "GLEW x64 library not found under ${MEDIASTUDIO_GLEW_ROOT}")
endif()

mediastudio_import_unknown_library(GLEW::GLEW "${MEDIASTUDIO_GLEW_LIBRARY}")

target_link_libraries(MediaStudio PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    OpenGL::GL
    GLEW::GLEW
    ${MEDIASTUDIO_FFMPEG_TARGETS}
)

if(WIN32)
    target_link_libraries(MediaStudio PRIVATE ole32 uuid winmm)
endif()

if(MEDIASTUDIO_ENABLE_MYSQL)
    set(MEDIASTUDIO_MYSQL_INCLUDE_DIR "${MEDIASTUDIO_MYSQL_CONNECTOR_ROOT}/include/mysql")
    set(MEDIASTUDIO_MYSQL_LIBRARY "${MEDIASTUDIO_MYSQL_CONNECTOR_ROOT}/lib/libmariadb.dll.a")

    if(NOT EXISTS "${MEDIASTUDIO_MYSQL_INCLUDE_DIR}/mysql.h")
        message(FATAL_ERROR "MariaDB/MySQL C API header not found: ${MEDIASTUDIO_MYSQL_INCLUDE_DIR}/mysql.h")
    endif()

    target_include_directories(MediaStudio PRIVATE "${MEDIASTUDIO_MYSQL_INCLUDE_DIR}")
    mediastudio_import_unknown_library(MySQL::mariadb "${MEDIASTUDIO_MYSQL_LIBRARY}")
    target_link_libraries(MediaStudio PRIVATE MySQL::mariadb)
endif()

if(MEDIASTUDIO_DEPLOY_RUNTIME AND WIN32)
    file(GLOB MEDIASTUDIO_FFMPEG_RUNTIME_DLLS "${MEDIASTUDIO_FFMPEG_ROOT}/bin/*.dll")
    if(MEDIASTUDIO_FFMPEG_RUNTIME_DLLS)
        add_custom_command(TARGET MediaStudio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MEDIASTUDIO_FFMPEG_RUNTIME_DLLS} $<TARGET_FILE_DIR:MediaStudio>
        )
    endif()

    if(EXISTS "${MEDIASTUDIO_GLEW_ROOT}/bin/Release/x64/glew32.dll")
        add_custom_command(TARGET MediaStudio POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${MEDIASTUDIO_GLEW_ROOT}/bin/Release/x64/glew32.dll" $<TARGET_FILE_DIR:MediaStudio>
        )
    endif()

    if(MEDIASTUDIO_ENABLE_MYSQL)
        file(GLOB MEDIASTUDIO_MYSQL_RUNTIME_DLLS "${MEDIASTUDIO_MYSQL_CONNECTOR_ROOT}/bin/*.dll")
        list(FILTER MEDIASTUDIO_MYSQL_RUNTIME_DLLS EXCLUDE REGEX ".*(libgcc_s_seh-1|libstdc\\+\\+-6|libwinpthread-1)\\.dll$")
        if(MEDIASTUDIO_MYSQL_RUNTIME_DLLS)
            add_custom_command(TARGET MediaStudio POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MEDIASTUDIO_MYSQL_RUNTIME_DLLS} $<TARGET_FILE_DIR:MediaStudio>
            )
        endif()
    endif()

    if(EXISTS "${MEDIASTUDIO_QT_ROOT}/bin/windeployqt.exe")
        add_custom_command(TARGET MediaStudio POST_BUILD
            COMMAND "${MEDIASTUDIO_QT_ROOT}/bin/windeployqt.exe" --no-translations $<TARGET_FILE:MediaStudio>
        )
    endif()
endif()
