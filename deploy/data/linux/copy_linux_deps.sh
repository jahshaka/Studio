#!/bin/bash
# =====================================================
# Script to copy an executable and all its dependent libraries
# including only the necessary Qt6 plugins
# Usage: ./copy_linux_deps.sh /path/to/executable /destination/dir /path/to/Qt6
# =====================================================

EXECUTABLE="$1"
DEST_DIR="$2"
QT_BASE="$3"

if [ -z "$EXECUTABLE" ] || [ -z "$DEST_DIR" ] || [ -z "$QT_BASE" ]; then
    echo "Usage: $0 /path/to/executable /destination/dir /path/to/Qt6"
    exit 1
fi

LIB_DIR="$DEST_DIR/lib"
PLUGIN_DIR="$DEST_DIR/plugins"

mkdir -p "$LIB_DIR" "$PLUGIN_DIR"

export LD_LIBRARY_PATH="$LIB_DIR:$LD_LIBRARY_PATH"
export QT_PLUGIN_PATH="$PLUGIN_DIR"

# Array to track copied libraries to avoid duplicates
declare -A COPIED_LIBS

# -----------------------------------------------------
# Function to copy a library and its dependencies recursively
# Arguments: library_path
# -----------------------------------------------------
copy_lib_recursive() {
    local lib="$1"
    if [ -z "$lib" ] || [ ! -f "$lib" ]; then
        return
    fi

    # Skip the main executable
    if [ "$lib" -ef "$EXECUTABLE" ]; then
        return
    fi

    # Skip system libraries
    case "$lib" in
        /lib/*|/usr/lib/*|/lib64/*|/usr/lib64/*)
            return
            ;;
    esac

    if [ "${COPIED_LIBS[$lib]}" ]; then
        return
    fi
    COPIED_LIBS[$lib]=1

    cp -u "$lib" "$LIB_DIR/"

    # Recursively copy dependencies
    local deps
    deps=$(ldd "$lib" 2>/dev/null | awk '/=>/ && $3 ~ /^\// {print $3}')
    for dep in $deps; do
        copy_lib_recursive "$dep"
    done
}

# -----------------------------------------------------
# 1. Copy main executable (already在DEST_DIR下)
# -----------------------------------------------------
cp "$EXECUTABLE" "$DEST_DIR/"

# 2. Copy dependencies
copy_lib_recursive "$EXECUTABLE"

# -----------------------------------------------------
# 3. Copy only the necessary Qt6 plugins
# -----------------------------------------------------
QT_PLUGIN_DIR="$QT_BASE/plugins"
NEEDED_PLUGINS=("platforms" "imageformats" "sqldrivers" "mediaservice")

for sub in "${NEEDED_PLUGINS[@]}"; do
    if [ -d "$QT_PLUGIN_DIR/$sub" ]; then
        mkdir -p "$PLUGIN_DIR/$sub"
        find "$QT_PLUGIN_DIR/$sub" -type f -name "*.so" | while IFS= read -r plugin; do
            cp -u "$plugin" "$PLUGIN_DIR/$sub/"
            copy_lib_recursive "$plugin"
        done
    fi
done

echo "All dependencies copied to $LIB_DIR, executable in $DEST_DIR, and plugins to $PLUGIN_DIR"

