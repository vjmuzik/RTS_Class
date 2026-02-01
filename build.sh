#!/bin/bash
touch ~/.bash_profile
export IDF_TOOLS_PATH="$HOME/.espressif"
source "$HOME/.espressif/python_env/idf5.5_py3.9_env/bin/activate"

# 1. Source the IDF environment (update this path to your actual export.sh)
set -a
. $HOME/esp/esp-idf/export.sh
set +a

export ESP_PYTHON="$HOME/.espressif/python_env/idf5.5_py3.9_env/bin/python"

# 2. Navigate to the project directory (Xcode sets this as $SRCROOT)
cd "$SRCROOT"

# 3. Handle the command based on what you want to do
# You can pass "build" or "flash" as an argument from Xcode
XCODE_ERROR_FORMAT='s#^(.+):([0-9]+):([0-9]+): (error|warning):#\1:\2:\3: \4:#'

case "$1" in
    flash)
        echo "--- Flashing ESP32 ---"
        idf.py build flash | sed -E "$XCODE_ERROR_FORMAT"
        ;;
    clean)
        echo "--- Cleaning Project ---"
        idf.py fullclean
        ;;
    install)
        echo "--- Running IDF Install ---"
        cd "$HOME/esp/esp-idf"
        ./install.sh esp32
        ;;
    configure)
        echo "--- Opening Menuconfig in Terminal ---"
        # We define the env path here to keep the AppleScript clean
        PYTHON_ENV="$HOME/.espressif/python_env/idf5.5_py3.9_env/bin/activate"
        
        osascript <<EOF
        tell application "Terminal"
            activate
            do script "cd '$SRCROOT' && source '$PYTHON_ENV' && . \$HOME/esp/esp-idf/export.sh && idf.py menuconfig && exit"
        end tell
EOF
        ;;
    *)
        echo "--- Building Project ---"
        idf.py build | sed -E "$XCODE_ERROR_FORMAT"
        ;;
esac
