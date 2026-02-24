#!/bin/bash
sudo rsync -av "./oven_video_display.service" "/lib/systemd/system/"
sudo systemctl daemon-reload
sudo systemctl enable oven_video_display.service