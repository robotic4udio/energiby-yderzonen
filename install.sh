#!/bin/bash
sudo apt-get update
sudo apt-get install -y unclutter

sudo rsync -av "./two_monitors_plot.service" "/lib/systemd/system/"
sudo systemctl daemon-reload
sudo systemctl enable two_monitors_plot.service