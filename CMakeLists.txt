# Author: Younguk Kim (bluekyu)

cmake_minimum_required(VERSION 3.12)
project(rpcpp_plugins
    DESCRIPTION "Plugins for Render Pipeline C++"
    HOMEPAGE_URL "https://github.com/bluekyu/rpcpp_plugins"
    LANGUAGES CXX
)

# === configure ====================================================================================
option(${PROJECT_NAME}_ENABLE_RTTI "Enable Run-Time Type Information" OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)    # Project Grouping

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# check package
if(NOT TARGET render_pipeline)
    find_package(render_pipeline CONFIG REQUIRED)
endif()

# check installing directory
render_pipeline_check_install_directory()

# common variable
set(RPPLUGINS_CONFIG_IN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/rpplugins-config.cmake.in")

# plugins
set(RENDER_PIPELINE_PLUGINS
    "ar_render"
    "background2d"
    "openvr"
    "recording"
    "rpflex"
    "vivesr"
)

foreach(plugin_id ${RENDER_PIPELINE_PLUGINS})
    option(rpcpp_plugins_BUILD_${plugin_id} "Enable to build '${plugin_id}'" OFF)
    if(rpcpp_plugins_BUILD_${plugin_id})
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/${plugin_id}")
    endif()
endforeach()
# ==================================================================================================
