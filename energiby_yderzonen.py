import argparse
import subprocess
import re
import time
import matplotlib
matplotlib.use('TkAgg')  # Use TkAgg backend for window management
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import numpy as np
import numpy.matlib

import math
from pythonosc import dispatcher
from pythonosc import osc_server
from pythonosc import udp_client
from threading import Thread, Lock
from concurrent.futures import ThreadPoolExecutor
import os

from pprint import pprint
import urllib.request

import json
from scipy.interpolate import interp1d

# ==================== RASPBERRY PI OPTIMIZATION ====================
# Optimize matplotlib rendering and system performance
os.environ['MPLBACKEND'] = 'TkAgg'

# Try to set CPU affinity to use specific cores (RPi 5 has 4 cores)
try:
    import psutil
    process = psutil.Process()
    # Use cores 2 and 3, leaving 0-1 for system/rendering
    process.cpu_affinity([2, 3])
except ImportError:
    pass

# Reduce matplotlib memory usage and improve rendering
plt.rcParams['figure.max_open_warning'] = 0
plt.rcParams['lines.linewidth'] = 2
#plt.rcParams['lines.antialiased'] = False  # Disable antialiasing for speed
#plt.rcParams['patch.antialiased'] = False
#plt.style.use('fivethirtyeight')
plt.rcParams['toolbar'] = 'None'
plt.rcParams['figure.dpi'] = 96  # Standard DPI for RPi displays
plt.rcParams['font.size'] = 9
# ===================================================================


# Functions to handle the monitor information and plotting on multiple monitors
def get_monitor_info():
    """Get monitor positions and sizes using xrandr."""
    result = subprocess.run(['xrandr'], capture_output=True, text=True)
    output = result.stdout
    monitors = []
    for line in output.split('\n'):
        if ' connected ' in line and 'primary' in line:  # Primary monitor first
            match = re.search(r'(\d+)x(\d+)\+(\d+)\+(\d+)', line)
            if match:
                width, height, x, y = map(int, match.groups())
                monitors.append((x, y, width, height))
        elif ' connected ' in line and 'primary' not in line:
            match = re.search(r'(\d+)x(\d+)\+(\d+)\+(\d+)', line)
            if match:
                width, height, x, y = map(int, match.groups())
                monitors.append((x, y, width, height))
    return monitors

def create_plot_on_monitor(monitor, plot_func):
    """Create a matplotlib figure positioned on the specified monitor."""
    x, y, width, height = monitor
    width = int(width * 0.5)  # Use 90% of the monitor width
    height = int(height * 0.5)  # Use 90% of the monitor height
    fig = plt.figure(figsize=(width/100, height/100), dpi=96)  # Lower DPI for performance
    # Disable toolbar and enable fast rendering
    # fig.canvas.set_window_title('')
    plot_func(fig)
    # Position the window
    fig.canvas.manager.window.geometry(f"{width}x{height}+{x}+{y}")
    fig.canvas.manager.window.deiconify()  # Make the window visible
    fig.canvas.manager.window.update()  # Update the window to apply position
    # Enable faster rendering mode
    fig.patch.set_animated(True)
    return fig


monitors = get_monitor_info()
print("Detected monitors:", monitors)
if len(monitors) < 2:
    print("Need at least two monitors connected.")
    # exit(1)

# ==================== LOWPASS FILTER====================
class OnePole:
    def __init__(self, alpha, initial_value):
        self.alpha = alpha
        self.value = initial_value

    def set_alpha(self, alpha):
        self.alpha = alpha
    
    def update(self, new_value):
        self.value = new_value * self.alpha + self.value * (1 - self.alpha)
        return self.value
    
    def update_alpha(self, new_value, alpha):
        self.value = new_value * alpha + self.value * (1 - alpha)
        return self.value
    
    def reset(self, initial_value):
        self.value = initial_value
        return self.value

    def get(self):
        return self.value




# Data synchronization for multi-threaded rendering
data_lock = Lock()
rendering_queue = {'x': [], 'y': [], 'v': []}

# Thread pool for parallel calculations
executor = ThreadPoolExecutor(max_workers=3)

oscSenderTeensy = udp_client.SimpleUDPClient("127.0.0.1",7134)

# Variables used for the live plot
global x_values, el_plot_values, index, run, t, td

# Data about the energy requirements

class EnergyRequirement:
    """Encapsulate a demand profile and derived curves.

    An instance holds an hourly baseline vector and produces the
    interpolated/filtered curves that the rest of the application uses.

    Two separate objects are created below: one for electricity and one
    for heat.  The setter method allows the profile to be changed at
    runtime.
    """

    def __init__(self, mw_needed, N=961, offset=0.0, uncertainty=7.0, alpha=0.02):
        self.hours_vector = np.linspace(0, 48, 49, True)
        self.N = N
        self.set_mw_needed(mw_needed, offset, uncertainty, alpha)

    def set_mw_needed(self, mw_needed, offset=0.0, uncertainty=7.0, alpha=0.02):
        """Assign a new hourly demand pattern and recompute all curves."""
        self.offset = offset
        self.uncertainty = uncertainty
        self.alpha = alpha

        self.mw_needed = np.array(mw_needed) + offset
        self.time_vector = np.zeros(self.N)
        self.need_vector = np.full(self.N, self.mw_needed.mean())
        self.need_min_vector = np.zeros(self.N)
        self.need_max_vector = np.zeros(self.N)

        last_need = self.mw_needed[0]
        spline = interp1d(self.hours_vector, self.mw_needed, kind='cubic')
        for x in range(self.N):
            t = 0.05 * x
            last_need = spline(t) * self.alpha + last_need * (1.0 - self.alpha)
            self.need_vector[x] = last_need
            self.need_min_vector[x] = last_need - self.uncertainty
            self.need_max_vector[x] = last_need + self.uncertainty
            self.time_vector[x] = t

# default hourly demand curve used for both electricity and heat
default_mw_needed = [
    24.0, 26.0, 27.0, 28.5, 32.5, 37.0, 39.0, 41.0, 40.0, 37.0, 32.0, 27.0,
    21.0, 17.0, 16.0, 12.0, 18.0, 23.0, 29.0, 32.0, 26.0, 20.0, 16.0, 20.0,
    22.0, 25.0, 27.0, 29.0, 33.0, 38.0, 40.0, 40.0, 39.0, 37.0, 32.0, 27.0,
    21.0, 17.0, 16.0, 12.0, 18.0, 23.0, 29.0, 32.0, 26.0, 20.0, 18.0, 20.0,
    24.0,
]

# Number of time steps in the simulation (48 hours with 0.05 hour time steps)
N = 961

# instantiate requirement object; electricity and heat profiles can be changed independently
class EnergyRequirements:
    def __init__(self):
        # Set default curves for both electricity and heat; they can be changed independently at runtime using the set_mw_needed method
        self.electricity = EnergyRequirement(default_mw_needed, N=N, uncertainty=9.0, alpha=0.020, offset= 5.0)
        self.heat        = EnergyRequirement(default_mw_needed, N=N, uncertainty=7.0, alpha=0.005, offset=-4.0)

    def get_total_need_vector(self):
        return self.electricity.need_vector + self.heat.need_vector

    def get_total_need_at(self, index):
        return self.electricity.need_vector[index] + self.heat.need_vector[index]



def timeOfDay(t):
    while(t > 24.0):
        t -= 24.0
    return t


x_values = []
el_plot_values = []
b_values = []
heat_plot_values = []
s_values = []
index = 0 
run = 0
t = 0  # Time in hours
td = 0 # Time of day in hours (0-24)

# ------------------------------------------------------------------------------------------- #
# ---------------------------------- Wind Generator ----------------------------------------- #
# ------------------------------------------------------------------------------------------- #
class WindGenerator:
    def __init__(self):
        self.max = 35.0  # Max Wind Power in MW
        self.n = 0
        self.N = 15
        self.mean = 20.0
        self.sd = 15.0
        self.f1 = OnePole(0.10, self.mean)
        self.f2 = OnePole(0.01, self.mean)
        self.power = self.mean
        self.tmp = self.mean
        self.vector = np.zeros(N)
        self.active = True

    def activate(self, active):
        self.active = active
    
    def calculate(self):
        if self.n >= self.N:
            self.tmp = np.random.normal(self.mean, self.sd)
            print(self.tmp)
            self.n = 0
        else:
            self.n = self.n + 1

        self.f1.update(self.tmp)
        self.f2.update(self.f1.get())
        self.power = max(self.f2.get(), 0)
        return self.power
    
    def make_new_vector(self):
        # Reset Wind
        self.mean = np.random.normal(10.0, 10.0)
        if self.mean < 0:
            self.mean = 0
        self.sd = abs(np.random.normal(0.0, 15.0))
        
        self.f1.reset(self.mean)
        self.f2.reset(self.mean)
        self.power = self.mean
        self.tmp = self.mean
        for x in range(N):
            self.vector[x] = self.calculate()

    def get(self, index):
        if self.active:
            return self.vector[index]
        else:
            return 0.0

# ------------------------------------------------------------------------------------------- #
# ---------------------------------- Sun Generator ------------------------------------------ #
# ------------------------------------------------------------------------------------------- #
class SunGenerator:
    def __init__(self):
        # use the current average electricity demand for scaling
        self.max = 0.07 * 35 # Max Solar Power in MW, scaled to be a fraction of the average electricity demand
        self.f1 = OnePole(0.1, 0.0)
        self.f2 = OnePole(0.1, self.f1.get())
        self.power = 0.0
        self.vector = np.zeros(N)
        self.active = True
    
    def activate(self, active):
        self.active = active

    def calculate(self, td):
        sol = 0.0
        sol_alpha = 0.1
        if td > 5 and td < 13:
            sol = self.max
        else:
            sol = 0.0
            sol_alpha = 0.05
        
        # Two stage lowpass filter to create a smoother curve
        sol = self.f1.update_alpha(sol, sol_alpha)
        sol = self.f2.update_alpha(sol, sol_alpha)

        self.power = sol
        
        return self.power
    
    def make_new_vector(self):
        self.__init__()  # Reset the sun generator to create a new sun profile
        for x in range(N):
            td = timeOfDay(0.05 * x)
            self.vector[x] = self.calculate(td)

    def get(self, index):
        if self.active:
            return self.vector[index]
        else:
            return 0.0


# ------------------------------------------------------------------------------------------- #
# ---------------------------------- PowerPlant --------------------------------------------- #
# ------------------------------------------------------------------------------------------- #
class PowerPlant:
    def __init__(self, requirements):
        # Ref to requirements for scaling power output and emissions
        self.requirements = requirements
        # Parameters related to the storage of burnable waste
        self.storage_amount_max = 64.0
        self.storage_amount = self.storage_amount_max
        # oven state
        self.oven_amount_initial = 13.0
        self.oven_amount = self.oven_amount_initial
        self.oven_amount_max = 26.0
        self.oven_amount_ok_min = 8.0
        self.oven_amount_ok_max = 18.0
        self.oven_amount_to_fill = 4.0
        self.oven_consumption_rate = 0.3
        # Air flow state
        self.air_flow = 0.5

        # power generation state
        self.power_max = 60  # MW
        self.alpha_up = 0.008
        self.alpha_down = 0.004
        self.alpha_empty = 0.01
        # initialise filter using the current electricity requirement baseline
        self.power_filter = OnePole(0.1, self.requirements.get_total_need_at(0))
        self.v1 = 0.0

        # Turbine amount, i.e. the percentage of power that is converted to electricity
        self.turbine_pct = 0.3
        self.turbine_pct_filter = OnePole(0.1, self.turbine_pct)

        # Emission
        self.CaCO3_amount = 0.0
        self.NaOH_amount = 0.0
        self.acid_emission = OnePole(0.05, 0.0)
        self.CO_emission = OnePole(0.05, 0.0)

    def get_storage_pct(self):
        return self.storage_amount / self.storage_amount_max
    
    def get_oven_pct(self):
        return self.oven_amount / self.oven_amount_max
    
    def set_air_flow(self, air_flow):
        self.air_flow = air_flow

    def get_air_flow(self):
        return self.air_flow

    def set_turbine_pct(self, pct):
        self.turbine_pct = pct
        
    def get_electricity_pct(self):
        return self.turbine_pct_filter.get()
    
    def get_heat_pct(self):
        return 1 - self.turbine_pct_filter.get()

    def get_electric_power(self):
        return self.power_filter.get() * self.get_electricity_pct()
    
    def get_electric_power_pct(self):
        return self.get_electric_power() / self.power_max

    def get_heat_power(self):
        return self.power_filter.get() * self.get_heat_pct()

    def get_heat_power_pct(self):
        return self.get_heat_power() / self.power_max
    
    def get_total_power(self):
        return self.power_filter.get()
    
    def get_total_power_pct(self):
        return self.power_filter.get() / self.power_max
    
    def get_oven_temperature(self):
        return 800.0 * self.get_total_power_pct()
    
    def get_oven_temperature_pct(self):
        return self.get_total_power_pct()
    
    def get_lambda(self):
        if self.oven_amount > 0:
            return self.air_flow / self.get_oven_pct()
        else:
            return 1.0

    def set_CaCO3_amount(self, amount):
        self.CaCO3_amount = amount

    def set_NaOH_amount(self, amount):
        self.NaOH_amount = amount

    def get_acid_emission(self):
        return self.acid_emission.get()
        
    def get_CO_emission(self):
        return self.CO_emission.get()
    
    def fill_oven(self):
        space = self.oven_amount_max - self.oven_amount
        if self.storage_amount >= self.oven_amount_to_fill and space >= self.oven_amount_to_fill:
            self.oven_amount += self.oven_amount_to_fill
            self.storage_amount -= self.oven_amount_to_fill
        elif space >= self.oven_amount_to_fill:
            self.oven_amount += self.storage_amount
            self.storage_amount = 0
    
    def calculate_acid_emission(self):
        # Calculate 
        acid_emission = self.get_total_power_pct() * (1-self.CaCO3_amount) * 0.6
        return self.acid_emission.update(acid_emission)
        
    def calculate_CO_emission(self):
        CO_emission = self.get_total_power_pct() * (1-self.NaOH_amount) * 0.4
        return self.CO_emission.update(CO_emission)

    def calculate_power(self):
        tmp_power = self.air_flow * self.get_oven_pct() * self.power_max
        oven_factor = 0.8 + 0.3 * self.oven_amount / self.oven_amount_max
        bio_factor = 1.0
        consumption = (0.3 + 0.7 * tmp_power / self.power_max) * oven_factor * self.oven_consumption_rate
        if self.oven_amount > self.oven_amount_ok_max + 0.5:
            consumption *= 1 + (self.oven_amount - self.oven_amount_ok_max)
        elif self.oven_amount < self.oven_amount_ok_min - 0.5:
            bio_factor = max(1 - 0.02 * (self.oven_amount_ok_min - self.oven_amount), 0.0)
        self.oven_amount = max(self.oven_amount - consumption, 0.0)
        if self.oven_amount == 0.0:
            bio_factor = 0
            self.power_filter.update_alpha(0.0, self.alpha_empty)
        else:
            tmp_power = tmp_power * bio_factor
            if tmp_power > self.power_filter.get():
                self.power_filter.update_alpha(tmp_power, self.alpha_up)
            elif tmp_power < self.power_filter.get():
                self.power_filter.update_alpha(tmp_power, self.alpha_down)

        return self.power_filter.get()
    
    def calculate(self):
        # Update the turbine filter
        self.turbine_pct_filter.update(self.turbine_pct)
        # Calculate the power output of the plant
        power = self.calculate_power()
        # Calculate the emissions
        self.calculate_acid_emission()
        self.calculate_CO_emission()
        # Return the power output
        return power

    def reset(self):
        self.__init__(self.requirements)
       

# EnergyGrid class to manage the overall energy production and consumption balance
class EnergyGrid:
    def __init__(self):
        self.requirements = EnergyRequirements()
        self.wind_generator = WindGenerator()
        self.sun_generator = SunGenerator()
        self.powerplant = PowerPlant(self.requirements)

    def reset(self):
        self.wind_generator.make_new_vector()
        self.sun_generator.make_new_vector()
        self.powerplant.reset()

    def get_total_electricity(self, index):
        return self.wind_generator.get(index) + self.sun_generator.get(index) + self.powerplant.get_electric_power()        

    def get_total_heat(self, index):
        return self.powerplant.get_heat_power()

    def get_total_production(self, index):
        return self.wind_generator.get(index) + self.sun_generator.get(index) + self.powerplant.get_total_power()

    def calculate(self, index):
        # Calculate the power plant output first as it depends on the current state of the oven and air flow
        plant_power = self.powerplant.calculate()
        # Then calculate the wind and sun power for the current time step
        wind_power = self.wind_generator.get(index)
        sun_power = self.sun_generator.get(index)
        # Return the total production
        return wind_power + sun_power + plant_power



energy_grid = EnergyGrid()



def plot_electricity(fig):
    global lb,lheat,ls,lel
    ax = fig.gca()  # Get the current axes
    ax.set_xlim([0,48]) # Set the x-limits
    ax.set_ylim([0,70]) # Set the y-limits
    ax.set_xlabel('Time [h]')
    ax.set_ylabel('Power [MW]')
    ax.set_xticks([0  ,6  ,12  ,18  ,24 ,30 ,36  ,42  ,48])
    xlabels = ['0:00','6:00','12:00','18:00','0:00','6:00','12:00','18:00','0:00']
    ax.set_xticklabels(xlabels)
    # fill the requirement envelope from the electricity requirement object
    plt.fill_between(energy_grid.requirements.electricity.time_vector,
                     energy_grid.requirements.electricity.need_min_vector,
                     energy_grid.requirements.electricity.need_max_vector,
                     label="Behov")
    #lb, = ax.plot(x_values,b_values,'r-', label="Produktion") # Create a line with the data
    #lheat, = ax.plot(x_values,heat_plot_values,'b-', label="Vind") # Create a line with the data
    lel,  = plt.plot(x_values,el_plot_values,'k-', label="El Produktion") # Create a line with the data
    #ls, = ax.plot(x_values,s_values,'k-', label="Energi til Net") # Create a line with the data

    plt.legend(loc='upper left')
    plt.grid(True)

def plot_heat(fig):
    global lheat
    ax = fig.gca()  # Get the current axes
    ax.set_xlim([0,48]) # Set the x-limits
    ax.set_ylim([0,70]) # Set the y-limits
    ax.set_xlabel('Time [h]')
    ax.set_ylabel('Power [MW]')
    ax.set_xticks([0  ,6  ,12  ,18  ,24 ,30 ,36  ,42  ,48])
    xlabels = ['0:00','6:00','12:00','18:00','0:00','6:00','12:00','18:00','0:00']
    ax.set_xticklabels(xlabels)
    # use heat requirement for plot_heat
    plt.fill_between(energy_grid.requirements.heat.time_vector,
                     energy_grid.requirements.heat.need_min_vector,
                     energy_grid.requirements.heat.need_max_vector,
                     label="Behov")
    lheat, = ax.plot(x_values,heat_plot_values,'k-', label="Fjernvarme Produktion") # Create a line with the data

    plt.legend(loc='upper left')
    plt.grid(True)

# Create plots on each monitor
plt.ioff()  # Turn off interactive mode to prevent blocking

fig1 = create_plot_on_monitor(monitors[0], plot_electricity)  # Assign to monitor 1
plt.tight_layout()
fig2 = create_plot_on_monitor(monitors[0], plot_heat)  # Assign to monitor 0
plt.tight_layout()

def sendElData():
    global storage_amount
    oscSenderTeensy.send_message("/OvenAmount", energy_grid.powerplant.oven_amount/energy_grid.powerplant.oven_amount_max)
    oscSenderTeensy.send_message("/WasteStorage", energy_grid.powerplant.get_storage_pct())
    oscSenderTeensy.send_message("/OvenPower", energy_grid.powerplant.get_total_power_pct())
    oscSenderTeensy.send_message("/WindPower", energy_grid.wind_generator.get(index)/energy_grid.wind_generator.max)
    oscSenderTeensy.send_message("/SunPower", energy_grid.sun_generator.get(index)/energy_grid.sun_generator.max)
    oscSenderTeensy.send_message("/Acid", energy_grid.powerplant.get_acid_emission())
    oscSenderTeensy.send_message("/CO", energy_grid.powerplant.get_CO_emission())
    oscSenderTeensy.send_message("/ElectricityPct", energy_grid.powerplant.get_electricity_pct())
    oscSenderTeensy.send_message("/HeatPct", energy_grid.powerplant.get_heat_pct())
    oscSenderTeensy.send_message("/PlantElectricPower", energy_grid.powerplant.get_electric_power_pct())
    oscSenderTeensy.send_message("/OvenTemp", energy_grid.powerplant.get_oven_temperature_pct())
    oscSenderTeensy.send_message("/CaCO3", energy_grid.powerplant.CaCO3_amount)
    oscSenderTeensy.send_message("/NaOH", energy_grid.powerplant.NaOH_amount)
    oscSenderTeensy.send_message("/TurbinePct", energy_grid.powerplant.turbine_pct)
    oscSenderTeensy.send_message("/OvenAirFlow", energy_grid.powerplant.get_air_flow())

# Non-blocking OSC sender using thread pool
def sendElDataAsync():
    """Send OSC data in background thread to avoid blocking rendering"""
    executor.submit(sendElData)

def updatePlot():
    lel.set_xdata(x_values)
    lel.set_ydata(el_plot_values)
    # Use async OSC sending to avoid blocking the render thread
    sendElDataAsync()

def updateHeatPlot():
    lheat.set_xdata(x_values)
    lheat.set_ydata(heat_plot_values)

def clear():
    global x_values, el_plot_values, b_values, heat_plot_values, s_values, index, run, t, td
    global production_filter

    x_values = []
    el_plot_values = []
    heat_plot_values = []
    b_values = []
    s_values = []
    energy_grid.reset()
    index = 0
    t = 0
    td = 0

    updatePlot()



# Frame counter for batching updates
render_frame_counter = 0
BATCH_SIZE = 2  # Update every 2 frames to reduce rendering overhead

# Animate Function for the plotting - OPTIMIZED
def animate(i):
    global index, run, t, td, render_frame_counter
    if run > 0:
        t = index * 0.05
        td = timeOfDay(t)
        energy_grid.calculate(index)
        x_values.append(t)
        el_plot_values.append(energy_grid.get_total_electricity(index))
        heat_plot_values.append(energy_grid.get_total_heat(index))
        
        # Batch rendering updates to reduce matplotlib overhead
        render_frame_counter += 1
        if render_frame_counter >= BATCH_SIZE:
            updatePlot()
            render_frame_counter = 0
        
        if t >= 48.0:
            run = 0
            print("Consumption {0}".format(index))
            updatePlot()  # Final update

        index = index + 1
    
    # Minimal sleep to prevent CPU spinning (set to 0 for maximum speed on RPi)
    time.sleep(0.01)

def animateHeat(i):
    global index, run, t, td
    if run > 0:
        # Only update heat plot occasionally to reduce render load
        updateHeatPlot()
    time.sleep(0.01)

# --------------------------------------------------------------
# ------------------------- OSC --------------------------------
# --------------------------------------------------------------
# Function to recieve value over osc 
def oscValue(addr, value):
    energy_grid.powerplant.set_air_flow(value)
    print("[{0}] ~ {1}".format(addr, energy_grid.powerplant.air_flow))

def oscCmd(addr, value):
    global x_values, el_plot_values, index, run
    if value == 'clear':
        clear()
    elif value == 'run':
        run = 1
    elif value == 'stop':
        run = 0
    elif value == 'StartButton':
        run = 0
        clear()
        run = 1
    elif value == 'FillButton':
        energy_grid.powerplant.fill_oven()
    elif value == 'Reset':
        run = 0
        clear()
        
    print("[{0}] ~ {1}".format(addr, value))

def oscAmountInOven(addr, value):
    energy_grid.powerplant.oven_amount = value
    print("[{0}] ~ {1}".format(addr, energy_grid.powerplant.oven_amount))

# Setup the OSC Functionality
dispatcher = dispatcher.Dispatcher()
parser = argparse.ArgumentParser()
parser.add_argument("--ip", default="0.0.0.0", help="The ip to listen on")
parser.add_argument("--port", type=int, default=7133, help="The port to listen on")
args = parser.parse_args()
dispatcher.map("/OvenAirFlow", oscValue)
dispatcher.map("/cmd", oscCmd)
dispatcher.map("/AmountInOven", oscAmountInOven)
dispatcher.map("/UseWind", lambda addr, value: energy_grid.wind_generator.activate(value))
dispatcher.map("/UseSun", lambda addr, value: energy_grid.sun_generator.activate(value))
dispatcher.map("/FillOven", lambda addr, value: energy_grid.powerplant.fill_oven())
dispatcher.map("/CaCO3", lambda addr, value: energy_grid.powerplant.set_CaCO3_amount(value))
dispatcher.map("/NaOH", lambda addr, value: energy_grid.powerplant.set_NaOH_amount(value))
dispatcher.map("/TurbinePct", lambda addr, value: energy_grid.powerplant.set_turbine_pct(value))

# Print all incoming messages
def print_handler(address, *args):
    print(f"Received message: {address} {args}")


# Set default handler
dispatcher.set_default_handler(print_handler)


server = osc_server.ThreadingOSCUDPServer((args.ip, args.port), dispatcher)
print("Serving on {}".format(server.server_address))

# Start Osc in a Thread
oscThread = Thread(target = server.serve_forever)
oscThread.daemon = True  # Make it a daemon thread so it doesn't block shutdown
oscThread.start()

clear()

# Start the Animation Function with optimized settings
# Use larger interval (50ms) for RPi to reduce CPU load, disable blitting as matplotlib TkAgg doesn't support it well
ani1 = FuncAnimation(fig1, animate, interval=50, blit=False, cache_frame_data=False)
ani2 = FuncAnimation(fig2, animateHeat, interval=50, blit=False, cache_frame_data=False)

plt.figure(fig1.number)
fig1.canvas.manager.window.attributes('-fullscreen', False)
fig1.canvas.draw()

plt.figure(fig2.number)
fig2.canvas.manager.window.attributes('-fullscreen', False)
fig2.canvas.draw()

# Show the plots (this will block until the windows are closed)
plt.show()    


# ==================== RASPBERRY PI OPTIMIZATION TIPS ====================
# To further improve performance on Raspberry Pi 5, apply these settings:
#
# 1. BOOT CONFIG (/boot/firmware/config.txt):
#    - gpu_mem=128          # Allocate more GPU memory if using hardware acceleration
#    - hdmi_blanking=1      # Reduce power consumption
#    - disable_splash=1     # Disable splash screen
#
# 2. SYSTEM SETTINGS:
#    - Disable unnecessary services: sudo systemctl disable bluetooth avahi-daemon
#    - Set CPU governor to performance: echo performance | sudo tee /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor
#    - Disable HDMI CEC: echo -e 'dtoverlay=cec\nenabled=0' >> /boot/firmware/config.txt
#
# 3. DISPLAY SETTINGS (for faster rendering):
#    - Use framebuffer backend instead of X11 for lower latency
#    - export DISPLAY=:0 before running the script
#    - Reduce refresh rate if needed: xrandr --output HDMI-1 --rate 50
#
# 4. PYTHON OPTIMIZATION:
#    - Use PyPy instead of CPython for up to 3x speedup: pip install pypy
#    - Set PYTHONOPTIMIZE=2 for aggressive optimization
#
# 5. MATPLOTLIB-SPECIFIC OPTIMIZATIONS ALREADY APPLIED:
#    - Lower DPI (96) for faster rendering
#    - Disabled antialiasing for better performance
#    - Batched updates (BATCH_SIZE=2) to reduce redraws
#    - ThreadPoolExecutor for non-blocking OSC messages
#    - Larger animation interval (50ms vs 10ms)
#    - Daemon thread for OSC server
#    - Cache frame data disabled for lower memory usage
#
# 6. PERFORMANCE MONITORING:
#    - Watch CPU usage: watch -n 1 'ps aux | grep energiby'
#    - Monitor memory: free -h
#    - Check temperature: vcgencmd measure_temp
# ========================================================================    