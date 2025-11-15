# CMakeLists.txt generated from ProjectConverter.vcxproj
# WARNING: This is an automatically generated file. Manual edits may be overwritten.
# VCXPROJ-to-CMAKE limitations:
# - MSBuild macros (e.g., $(SolutionDir)) may not be translated correctly.
# - Complex conditions (custom platforms, etc.) are likely ignored.
# - Linked libraries (e.g., system libs, other projects) may be incomplete.

cmake_minimum_required(VERSION 3.16)

project(ProjectConverter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(CMAKE_GENERATOR_PLATFORM STREQUAL "x64")
    # Settings for x64
endif()

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
# TODO: Add other required Qt components (e.g., Network, Xml)

set(PROJECT_SOURCES
    "../../Open/XShowVersion.cpp"
    "ProjectConverter.cpp"
    "main.cpp"
)

set(PROJECT_HEADERS
    "../../Open/NList.h"
)

set(PROJECT_FORMS
    "ProjectConverter.ui"
)

set(PROJECT_RESOURCES
    "ProjectConverter.qrc"
)

# Other resource files (icons, images)
set(PROJECT_OTHER_RESOURCES
    "ProjectConverter.rc"
)

qt_wrap_ui(PROJECT_SOURCES ${PROJECT_FORMS})
qt_add_resources(PROJECT_SOURCES ${PROJECT_RESOURCES})

# Define the target
# WARNING: Unknown ConfigurationType: . Defaulting to Application.
add_executable(${PROJECT_NAME} WIN32
    ${PROJECT_SOURCES}
    ${PROJECT_HEADERS}
)

# === Configuration-specific settings ===

# --- End of Debug|x64 ---

target_include_directories(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:
        "../../Open"
    >
)

target_link_directories(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:
        "../../Lib/Release"
    >
)

target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:/Od>
)
target_link_libraries(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Release>:
        "ServiceLib.lib"
    >
)

# --- End of Release|x64 ---


# Qt ã¢ã¸ã¥ã¼ã«ã¸ã®ãªã³ã¯
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)
