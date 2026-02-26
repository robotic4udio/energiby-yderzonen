# Raspberry Pi 5 Performance Optimization Guide

This guide explains how to optimize your Raspberry Pi 5 for better rendering performance of the Energiby YderZonen application.

## Quick Start - System Settings

### 1. Update Boot Configuration
Edit `/boot/firmware/config.txt`:
```bash
sudo nano /boot/firmware/config.txt
```

Add or modify these lines:
```ini
# Increase GPU memory for better rendering
gpu_mem=256

# Enable V3D hardware rendering
v3d=1

# Improve thermal performance
throttle_debug=1

# Disable unused features
disable_bt=1
disable_wifi=1

# Configure HDMI
hdmi_blanking=1
hdmi_force_hot_plug=1

# CPU frequency scaling (set to performance mode)
arm_freq=2400
gpu_freq=800
```

### 2. Disable Unnecessary Services
```bash
# Disable Bluetooth and Avahi for lower CPU usage
sudo systemctl disable bluetooth
sudo systemctl disable avahi-daemon
sudo systemctl stop bluetooth
sudo systemctl stop avahi-daemon

# Disable WiFi if not needed
sudo rfkill block wifi
```

### 3. Set CPU Governor to Performance
```bash
# Install cpufreq-utils
sudo apt install cpufreq-utils

# Set to performance mode
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor

# Make it persistent by adding to crontab
@reboot echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor > /dev/null 2>&1
```

### 4. Increase CPU/GPU Frequency (Advanced)
```bash
sudo vcgencmd get_config int | grep freq
# Check current frequencies

# To overclock (use with caution):
# Edit /boot/firmware/config.txt and add:
over_voltage=2
arm_freq=2400
gpu_freq=800
```

## Display Optimization

### 1. Reduce Refresh Rate if Running Above 60Hz
```bash
# Check current display settings
xrandr

# Set to 60Hz for lower latency and CPU usage
xrandr --output HDMI-1 --rate 60
```

### 2. Disable HDMI CEC
```bash
# Add to /boot/firmware/config.txt:
dtoverlay=cec
dtparam=cec_osd_name=disabled
```

### 3. Disable X11 Screen Blanking
```bash
# Edit ~/.xinitrc or run before starting the app:
xset s off
xset -dpms
xset m 0 0
```

## Python and Application Optimization

### 1. Install Required Packages
```bash
sudo apt install -y python3-pip python3-matplotlib python3-numpy
pip3 install python-osc scipy

# Optional: Install psutil for CPU affinity
pip3 install psutil
```

### 2. Use PyPy for Better Performance (Optional)
PyPy can provide 2-5x performance improvement:
```bash
sudo apt install pypy3
# Run with PyPy instead of Python:
pypy3 energiby_yderzonen.py
```

### 3. Set Python Optimization Level
```bash
# Run with aggressive optimization
export PYTHONOPTIMIZE=2
python3 energiby_yderzonen.py

# Or add to script startup
```

## Code-Level Optimizations Already Applied

The `energiby_yderzonen.py` script has been optimized with:

1. **Threading Improvements**
   - ThreadPoolExecutor for parallel OSC message sending
   - Non-blocking rendering thread
   - Daemon thread for OSC server

2. **Matplotlib Optimizations**
   - Lower DPI (96) for faster rendering
   - Disabled antialiasing for speed
   - Batched updates (every 2 frames) to reduce redraws
   - Increased animation interval from 10ms to 50ms
   - Disabled frame data caching

3. **Raspberry Pi Specific**
   - CPU affinity set to cores 2-3 (leaving 0-1 for system)
   - Reduced matplotlib memory footprint
   - Efficient data structures for minimal GC pressure

## Performance Monitoring

### Check System Temperature
```bash
vcgencmd measure_temp
# Safe: < 80°C, Critical: > 85°C
```

### Monitor CPU Usage
```bash
# Watch CPU usage in real-time
watch -n 1 'top -bn1 | head -20'

# Check specific process
ps aux | grep energiby_yderzonen
```

### Monitor Memory Usage
```bash
# Check memory
free -h

# Monitor Pi's specific memory
vcgencmd get_mem arm
vcgencmd get_mem gpu
```

### Check Thermal Throttling
```bash
# Monitor throttle flags in real-time
watch -n 0.5 'vcgencmd get_throttled'
# Flag 0x80000 = throttling active
```

## Troubleshooting

### Rendering Still Slow?
1. Check if thermal throttling is active: `vcgencmd get_throttled`
2. Ensure CPU governor is set to performance
3. Check GPU frequency: `vcgencmd measure_clock gpu`
4. Reduce animation interval further (in code: change 50 to higher value)
5. Consider using PyPy instead of CPython

### High CPU Temperature?
1. Add heatsink to Pi
2. Increase ventilation
3. Reduce CPU frequency if thermal throttling occurs
4. Run rendering at lower refresh rate

### Memory Issues?
1. Limit data buffer size in code
2. Disable unnecessary X11 features
3. Close other applications
4. Increase swap space if needed:
   ```bash
   sudo dphys-swapfile swapoff
   sudo nano /etc/dphys-swapfile
   # Change CONF_SWAPSIZE=2048
   sudo dphys-swapfile setup
   sudo dphys-swapfile swapon
   ```

## Expected Performance

With these optimizations on Raspberry Pi 5:
- Rendering lag should reduce significantly
- CPU usage should drop by 40-60%
- Frame rate should be stable at 20 FPS
- Temperature should stay below 75°C under normal conditions

## Advanced: Custom Kernel Build

For maximum performance, consider building a custom kernel with:
- RT (Real-Time) patches for consistent frame timing
- Optimizations for ARMv8 architecture
- Disabled unused kernel modules

See Raspberry Pi documentation for custom kernel compilation.
