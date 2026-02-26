#!/bin/bash
# Run Energiby YderZonen with Performance Optimizations
# Usage: bash run_optimized.sh

set -e

echo "Starting Energiby YderZonen with optimizations..."
echo ""

# 1. Disable screen blanking
xset s off
xset -dpms
xset m 0 0

# 2. Optimize display refresh rate (set to 60Hz)
echo "Setting display to 60Hz for optimal performance..."
xrandr --rate 60 2>/dev/null || echo "Display rate setting skipped"

# 3. Set CPU governor to performance
echo "Setting CPU governor to performance mode..."
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor > /dev/null 2>&1

# 4. Kill other CPU-intensive processes (optional)
killall firefox chromium-browser thunderbird 2>/dev/null || true

# 5. Run with Python optimization level
export PYTHONOPTIMIZE=2

echo "Starting application..."
python3 ./energiby_yderzonen.py "$@"

# 6. Reset CPU governor to powersave when done
echo "Resetting CPU governor to powersave..."
echo powersave | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor > /dev/null 2>&1

echo "Done!"
