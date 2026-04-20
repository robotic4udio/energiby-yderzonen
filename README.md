# Energiby YderZonen
Energiby YderZonen is an interactive installation with a digital interface that simulates an energy distribution system. Users act as power plant operators, making decisions on running the power plant, managing energy distribution, and responding to community needs. The installation includes a physical interface with buttons and displays, as well as a digital dashboard that provides real-time feedback on energy production, consumption, and sustainability metrics.

Designed as a game, the project challenges students to balance energy supply, community needs, and sustainability. The goal is to promote critical thinking about energy management and sustainable practices through hands-on experience.

## The wall installation
![Energiby YderZonen Wall](yz_energy_wall.jpg)

## The control panel
![Energiby YderZonen control panel](yz_energy_controlpanel.jpg)

### Teensy 4.1 with Ethernet

![Energiby YderZonen control panel](yz_circuit_board.png)

| Pin     | Component         | Purpose                                                                   |     |
| ------- | ----------------- | ------------------------------------------------------------------------- | --- |
| 26      | PushButton 1      | Start Game Button                                                         |     |
| 27      | PushButton 2      | Fill Oven Button                                                          |     |
| 28      | PushButton 3      | Enable/Disable Wind Turbine Button                                        |     |
| 29      | PushButton 4      | Enable/Disable Solar Panel Button                                         |     |
| 30      | PushButton 5      | Enable/Disable Waste Energy Button                                        |     |
| 31      | PushButton 6      | Buy Electricity Button                                                    |     |
| 32      | PushButton 7      | Sell Electricity Button                                                   |     |
| 33      | GPIO              | SPARE                                                                     |     |
| 14 (A0) | Potentiometer 1   | Air Blower Speed Control [0-100%]                                         |     |
| 15 (A1) | Potentiometer 2   | Energy Distribution Control [0-100%] (Steam to Turbine vs Heat Exchanger) |     |
| 16 (A2) | Potentiometer 3   | CaCO3 Dosing Amount [0-100%]                                              |     |
| 17 (A3) | Potentiometer 4   | NaOH Dosing Amount [0-100%]                                               |     |
| 18 (A4) | GPIO              | SPARE                                                                     |     |
| 19 (A5) | GPIO              | SPARE                                                                     |     |
| 20 (A6) | GPIO              | SPARE                                                                     |     |
| 21 (A7) | GPIO              | SPARE                                                                     |     |
| 0       | LED 1             | Start Game Button LED (Game Status: Lights up when the game is active)    |     |
| 1       | LED 2             | Fill Oven Button LED (Lights up when pressed)                             |     |
| 2       | LED 3             | Wind Turbine Status (Lights up when enabled)                              |     |
| 3       | LED 4             | Solar Panel Status (Lights up when enabled)                               |     |
| 4       | LED 5             | Waste Energy Status (Lights up when enabled)                              |     |
| 5       | LED 6             | Buy Status (Lights up when pressed)                                       |     |
| 6       | LED 7             | Sell Status (Lights up when pressed)                                      |     |
| 7       | LED 8             | Oven Temperature High Alarm                                               |     |
| 8       | LED 9             | Acid Emissions High Alarm                                                 |     |
| 9       | LED 10            | CO Emissions High Alarm                                                   |     |
| 10      | LED 11            | SPARE LED                                                                 |     |
| 11      | LED 12            | SPARE LED                                                                 |     |
| 34      | LED Strip 1       | Burnables in Oven [0-100%]                                                |     |
| 35      | LED Strip 2       | Air Blower Speed [0-100%]                                                 |     |
| 36      | LED Strip 3       | Oven Effect [0-60MW]                                                      |     |
| 37      | LED Strip 4       | Steam Percentage to Turbine [0-100%]                                      |     |
| -       | LED Strip 5       | Steam Percentage to Heat Exchanger [0-100%]                               |     |
| 38      | LED Strip 6       | Wind Energy Production [0-100%]                                           |     |
| 39      | LED Strip 7       | Solar Energy Production [0-100%]                                          |     |
| -       | LED Strip 8       | Waste Energy Production [0-100%]                                          |     |
| -       | LED Strip 9       | Buy/Sell Energy Status [-100-100%]                                        |     |
| 40      | LED Strip 10      | CaCO3 Dosing Amount (Eliminates Acid Emissions)                           |     |
| 41      | LED Strip 11      | NaOH Dosing Amount (Eliminates CO Emissions)                              |     |
| 22      | VU Meter (MOSFET) | Oven Temperature (Visualized as a VU Meter)                               |     |
| 23      | VU Meter (MOSFET) | Acid Emissions (Visualized as a VU Meter)                                 |     |
| 24      | VU Meter (MOSFET) | CO Emissions (Visualized as a VU Meter)                                   |     |
| 25      | SPARE MOSFET      | SPARE                                                                     |     |


# What is needed?
1. Raspberry Pi 5
2. Teensy 4.1

# Notes - Install PI
Download Raspberry Pi Imager https://www.raspberrypi.com/software/
  - Write Raspberry PI OS (32-BIT) to a 16GB SD Card


Install Needed Packages!
```console
sudo apt update
sudo apt upgrade -y
sudo apt install -y code
sudo apt install -y python3 python3-pip python3-numpy python3-matplotlib python3-opencv
pip3 install python-oscv --break-system-packages
```

#




# Install Arduino
Download Arduino IDE: https://downloads.arduino.cc/arduino-1.8.19-linuxarm.tar.xz

Install Arduino IDE
Open a terminal window and Navigate to the Downloads folder:
```console
cd ~/Downloads
```

List the files in the Downloads folder using:
```console
ls
```

You should see the Arduino IDE archive:
```console
arduino-####-linuxarm.tar.xz
```
Note the version number.

Extract the contents of the downloaded file:
```console
tar -xf arduino-####-linuxarm.tar.xz
```
This should create a folder named “arduino-####” full of files.

Move the folder to /opt using:
```console
sudo mv arduino-#### /opt
```
Finally complete the installation by running:
```console
sudo /opt/arduino-####/install.sh
```

# Install Teensyduino
Download Teensyduino: https://www.pjrc.com/teensy/td_156/TeensyduinoInstall.linuxarm

Linux Installation
Download the Linux udev rules https://www.pjrc.com/teensy/00-teensy.rules and copy the file to /etc/udev/rules.d.
```console
sudo cp 00-teensy.rules /etc/udev/rules.d/
```
Run the installer by adding execute permission and then execute it.
```console
chmod 755 TeensyduinoInstall.linux64
./TeensyduinoInstall.linux64
```
Install missing package
```console
sudo apt-get install libusb-0.1-4
```

# Hide Mouse Pointer on Boot
```bash
sudo sed -i -- "s/#xserver-command=X/xserver-command=X -nocursor/" /etc/lightdm/lightdm.conf
```

# Access
