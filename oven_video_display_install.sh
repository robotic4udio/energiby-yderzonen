#!/bin/bash
sudo apt-get update
sudo apt-get install -y unclutter

sudo rsync -av "./oven_video_display.service" "/lib/systemd/system/"
sudo systemctl daemon-reload
sudo systemctl enable oven_video_display.service