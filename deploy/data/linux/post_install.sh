#!/bin/bash

APP_NAME=Jahshaka
LOG_FOLDER=/var/log/$APP_NAME
LOG_FILE="$LOG_FOLDER/post-install.log"
APP_PATH=/opt/$APP_NAME

# Create log folder if missing
if [ ! -d "$LOG_FOLDER" ]; then
    sudo mkdir -p "$LOG_FOLDER"
    echo "Jahshaka log dir created at /var/log/"
fi

# Create log file if missing
if [ ! -f "$LOG_FILE" ]; then
    sudo touch "$LOG_FILE"
    echo "Jahshaka log file created at $LOG_FILE"
fi

# Start logging
date > "$LOG_FILE"
echo "Script started" >> "$LOG_FILE"

# Kill existing processes
sudo killall -9 "$APP_NAME" 2>> "$LOG_FILE"

# Disable SteamOS readonly if present
if command -v steamos-readonly &> /dev/null; then
    sudo steamos-readonly disable >> "$LOG_FILE"
    echo "steamos-readonly disabled" >> "$LOG_FILE"
fi

# Remove write permissions from app folder
sudo chmod -R a-w "$APP_PATH/"

# Ensure executable script has correct permissions
sudo chmod 555 "$APP_PATH/$APP_NAME.sh" >> "$LOG_FILE"

# Create symlinks
sudo ln -sf "$APP_PATH/$APP_NAME.sh" /usr/local/sbin/$APP_NAME
sudo ln -sf "$APP_PATH/$APP_NAME.sh" /usr/local/bin/$APP_NAME

# Copy desktop files/icons
echo "user desktop creation loop started" >> "$LOG_FILE"
sudo cp "$APP_PATH/$APP_NAME.desktop" /usr/share/applications/ >> "$LOG_FILE"
sudo cp "$APP_PATH/$APP_NAME.png" /usr/share/pixmaps/ >> "$LOG_FILE"
sudo chmod 555 /usr/share/applications/$APP_NAME.desktop >> "$LOG_FILE"
echo "user desktop creation loop ended" >> "$LOG_FILE"

# Launch Jahshaka in background and log output
echo "Launching $APP_NAME..." >> "$LOG_FILE"
"$APP_PATH/$APP_NAME.sh" >> "$LOG_FILE" 2>&1 &

# Re-enable SteamOS readonly
if command -v steamos-readonly &> /dev/null; then
    sudo steamos-readonly enable >> "$LOG_FILE"
    echo "steamos-readonly enabled" >> "$LOG_FILE"
fi


date >> "$LOG_FILE"
echo "Script finished" >> "$LOG_FILE"
exit 0

