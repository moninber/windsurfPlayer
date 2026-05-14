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

set(MEDIASTUDIO_QT_ROOT "E:/Qt/6.11.0/mingw_64" CACHE PATH "Qt installation root")
set(MEDIASTUDIO_FFMPEG_ROOT "E:/ffmpeg" CACHE PATH "FFmpeg installation root")
set(MEDIASTUDIO_GLEW_ROOT "E:/OpenGl/glew-2.3.1-win32/glew-2.3.1" CACHE PATH "GLEW installation root")
set(MEDIASTUDIO_MYSQL_CONNECTOR_ROOT "E:/mysql-connector-c++" CACHE PATH "MySQL Connector/C++ root")
set(MEDIASTUDIO_MYSQL_SERVER_ROOT "E:/MySQL/MySQL Server 8.0" CACHE PATH "MySQL Server root")

if(EXISTS "${MEDIASTUDIO_QT_ROOT}")
    list(APPEND CMAKE_PREFIX_PATH "${MEDIASTUDIO_QT_ROOT}")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
find_package(OpenGL REQUIRED)

set(MEDIASTUDIO_SOURCES
    main.cpp
    MediaDecoder.cpp
    AudioOutput.cpp
    VideoRenderer.cpp
    AudioVisualizer.cpp
    MediaTranscoder.cpp
    PlayerController.cpp
    VideoWidget.cpp
    MainWindow.cpp
)

set(MEDIASTUDIO_HEADERS
    MediaInfo.h
    MediaDecoder.h
    AudioOutput.h
    VideoRenderer.h
    AudioVisualizer.h
    MediaTranscoder.h
    MediaDatabase.h
    PlayerController.h
    VideoWidget.h
    MainWindow.h
)

if(MEDIASTUDIO_ENABLE_MYSQL)
    list(APPEND MEDIASTUDIO_SOURCES MediaDatabase.cpp)
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
    "${CMAKE_CURRENT_SOURCE_DIR}"
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
    set(MEDIASTUDIO_MYSQL_INCLUDE_CANDIDATES
        "${MEDIASTUDIO_MYSQL_CONNECTOR_ROOT}/include"
        "${MEDIASTUDIO_MYSQL_CONNECTOR_ROOT}/include/jdbc"
        "${MEDIASTUDIO_MYSQL_CONNECTOR_ROOT}/include/jdbc/cppconn"
        "${MEDIASTUDIO_MYSQL_SERVER_ROOT}/include"
        "C:/Program Files/MySQL/mysql-connector-c++-9.6.0-winx64/mysql-connector-c++-9.6.0-winx64/include"
        "C:/Program Files/MySQL/mysql-connector-c++-9.6.0-winx64/mysql-connector-c++-9.6.0-winx64/include/jdbc"
        "C:/Program Files/MySQL/mysql-connector-c++-9.6.0-winx64/mysql-connector-c++-9.6.0-winx64/include/jdbc/cppconn"
        "C:/Program Files/MySQL/MySQL Server 8.0/include"
    )

    foreach(mysql_include IN LISTS MEDIASTUDIO_MYSQL_INCLUDE_CANDIDATES)
        if(EXISTS "${mysql_include}")
            target_include_directories(MediaStudio PRIVATE "${mysql_include}")
        endif()
    endforeach()

    set(MEDIASTUDIO_MYSQL_LIBRARY_CANDIDATE_DIRS
        "${MEDIASTUDIO_MYSQL_CONNECTOR_ROOT}/lib64"
        "${MEDIASTUDIO_MYSQL_CONNECTOR_ROOT}/lib64/vs14"
        "${MEDIASTUDIO_MYSQL_CONNECTOR_ROOT}/lib"
        "${MEDIASTUDIO_MYSQL_SERVER_ROOT}/lib"
        "C:/Program Files/MySQL/mysql-connector-c++-9.6.0-winx64/mysql-connector-c++-9.6.0-winx64/lib64/vs14"
        "C:/Program Files/MySQL/MySQL Server 8.0/lib"
    )

    foreach(mysql_lib_name mysqlcppconn mysqlcppconnx libmysql)
        unset(mysql_lib_path)
        foreach(mysql_lib_dir IN LISTS MEDIASTUDIO_MYSQL_LIBRARY_CANDIDATE_DIRS)
            if(MINGW AND EXISTS "${mysql_lib_dir}/lib${mysql_lib_name}.dll.a")
                set(mysql_lib_path "${mysql_lib_dir}/lib${mysql_lib_name}.dll.a")
                break()
            elseif(EXISTS "${mysql_lib_dir}/${mysql_lib_name}.lib")
                set(mysql_lib_path "${mysql_lib_dir}/${mysql_lib_name}.lib")
                break()
            elseif(EXISTS "${mysql_lib_dir}/${mysql_lib_name}.dll")
                set(mysql_lib_path "${mysql_lib_dir}/${mysql_lib_name}.dll")
                break()
            endif()
        endforeach()

        if(NOT mysql_lib_path)
            message(FATAL_ERROR "MySQL library not found: ${mysql_lib_name}. Set MEDIASTUDIO_MYSQL_CONNECTOR_ROOT and MEDIASTUDIO_MYSQL_SERVER_ROOT, or configure with MEDIASTUDIO_ENABLE_MYSQL=OFF.")
        endif()

        set(mysql_target "MySQL::${mysql_lib_name}")
        mediastudio_import_unknown_library(${mysql_target} "${mysql_lib_path}")
        target_link_libraries(MediaStudio PRIVATE ${mysql_target})
    endforeach()
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
        file(GLOB MEDIASTUDIO_MYSQL_RUNTIME_DLLS
            "${MEDIASTUDIO_MYSQL_CONNECTOR_ROOT}/lib64/*.dll"
            "${MEDIASTUDIO_MYSQL_CONNECTOR_ROOT}/lib64/vs14/*.dll"
            "${MEDIASTUDIO_MYSQL_CONNECTOR_ROOT}/lib/*.dll"
            "${MEDIASTUDIO_MYSQL_SERVER_ROOT}/lib/*.dll"
        )
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
