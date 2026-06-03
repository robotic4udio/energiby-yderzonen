#!/bin/bash
#sudo apt-get update
#sudo apt-get install -y unclutter

sudo rsync -av "./energiby_yderzonen.service" "/lib/systemd/system/"
sudo systemctl daemon-reload
sudo systemctl enable energiby_yderzonen.service