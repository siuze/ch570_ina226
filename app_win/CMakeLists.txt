cmake_minimum_required(VERSION 3.20)
project(CH570_Monitor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files
set(SOURCES
    src/main.cpp
    src/ble_scanner.cpp
    src/serial_manager.cpp
    src/ui/style.cpp
    src/ui/dashboard.cpp
    src/resource.rc

    # Dear ImGui
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_win32.cpp
    third_party/imgui/backends/imgui_impl_dx11.cpp

    # ImPlot
    third_party/implot/implot.cpp
    third_party/implot/implot_items.cpp
)

# Application executable (Windows GUI subsystem)
add_executable(CH570_Monitor WIN32 ${SOURCES})

# Include paths
target_include_directories(CH570_Monitor PRIVATE
    src
    src/ui
    third_party/imgui
    third_party/imgui/backends
    third_party/implot
)

# Compiler options for MSVC
if(MSVC)
    target_compile_options(CH570_Monitor PRIVATE
        /utf-8
        /permissive-
        /W3
        /O2
        /GL
    )
    target_link_options(CH570_Monitor PRIVATE
        /LTCG
        /OPT:REF
        /OPT:ICF
    )
endif()

# System libraries
target_link_libraries(CH570_Monitor PRIVATE
    d3d11
    dxgi
    setupapi
    windowsapp
)
