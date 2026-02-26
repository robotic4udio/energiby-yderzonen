#!/bin/bash
# Quick Raspberry Pi Optimization Script
# Run with: sudo bash rpi_optimize.sh

set -e

echo "================================"
echo "RPi 5 Performance Optimization"
echo "================================"

# 1. Update system
echo "[1/5] Updating system..."
sudo apt update
sudo apt upgrade -y

# 2. Install required packages
echo "[2/5] Installing optimization packages..."
sudo apt install -y cpufreq-utils psutil python3-psutil xset

# 3. Disable unnecessary services
echo "[3/5] Disabling unnecessary services..."
sudo systemctl disable bluetooth avahi-daemon 2>/dev/null || true
sudo systemctl stop bluetooth avahi-daemon 2>/dev/null || true

# 4. Set CPU governor to performance
echo "[4/5] Setting CPU governor to performance..."
echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor > /dev/null

# 5. Create persistence script
echo "[5/5] Creating persistence script..."
cat > /tmp/rpi_performance.sh << 'EOF'
#!/bin/bash
# Set performance governor
echo performance | tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor > /dev/null 2>&1

# Disable screen blanking
xset s off
xset -dpms
xset m 0 0
EOF

chmod +x /tmp/rpi_performance.sh

# Add to crontab
(crontab -l 2>/dev/null | grep -v "rpi_performance.sh" || true; echo "@reboot /tmp/rpi_performance.sh") | crontab -

echo ""
echo "✓ Optimization complete!"
echo ""
echo "IMPORTANT: Edit /boot/firmware/config.txt for boot-time settings:"
echo "  gpu_mem=256"
echo "  v3d=1"
echo "  disable_bt=1"
echo "  disable_wifi=1"
echo ""
echo "Then reboot with: sudo reboot"
