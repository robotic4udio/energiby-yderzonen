#!/bin/bash
sudo rsync -av "./two_monitors_plot.service" "/lib/systemd/system/"
sudo systemctl daemon-reload
sudo systemctl enable two_monitors_plot.service