#!/bin/sh

# ####################################################################
#
# LIB_PATH - are relative path to libraries of a deployed distribution.
# PLUGIN_PATH - are relative path to qt plugins of a deployed distribution.
# BIN_PATH - are relative path to targets of a deployed distribution.

# SYSTEM_LIB_PATH - are relative path to system libraries of a deployed distribution.
# BASE_NAME - are base name of the executable that will be launched after run this script.
# CUSTOM_SCRIPT_BLOCK - This is code from the customScript option
# RUN_COMMAND - This is command for run application. Required BASE_DIR variable.
#
# ####################################################################

BASE_DIR=$(dirname "$(readlink -f "$0")")

# Set library and plugin paths
export LD_LIBRARY_PATH="$BASE_DIR/lib:$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$BASE_DIR/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$BASE_DIR/plugins/platforms"

# Run the executable
"$BASE_DIR/Jahshaka" "$@"

