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
from dataclasses import dataclass
import sys
import signal


# Run is FullScreen on two displays?
FullScreen = False


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

# ==================== LOOKUP TABLE ====================
class LookupTable:
    def __init__(self, x_values, y_values):
        self.x_values = np.array(x_values)
        self.y_values = np.array(y_values)
        self.interpolator = interp1d(self.x_values, self.y_values, kind='linear', fill_value="extrapolate")
    def get(self, x):
        x = float(x)
        y = float(self.interpolator(x))
        return y

# ==================== LOWPASS FILTER====================
class OnePole:
    def __init__(self, tau, initial_value):
        self.tau = max(float(tau), 1e-12)
        self.value = initial_value

    @staticmethod
    def alpha_from_tau(tau, dt):
        tau = max(float(tau), 1e-12)
        dt = max(float(dt), 0.0)
        return 1.0 - math.exp(-dt / tau)

    @staticmethod
    def tau_from_alpha(alpha, dt):
        alpha = min(max(float(alpha), 1e-9), 1.0 - 1e-9)
        dt = max(float(dt), 1e-12)
        return -dt / math.log(1.0 - alpha)

    def set_tau(self, tau):
        self.tau = max(float(tau), 1e-12)
    
    def set_alpha(self, alpha, dt):
        self.tau = OnePole.tau_from_alpha(alpha, dt)
    
    def update(self, new_value, dt):
        alpha = OnePole.alpha_from_tau(self.tau, dt)
        self.value = new_value * alpha + self.value * (1 - alpha)
        return self.value
    
    def update_tau(self, new_value, tau, dt):
        alpha = OnePole.alpha_from_tau(tau, dt)
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
executor = ThreadPoolExecutor(max_workers=4)

oscSenderOvenDisplay = udp_client.SimpleUDPClient("192.168.0.101",7134)
oscSenderStorageDisplay = udp_client.SimpleUDPClient("192.168.0.104",7134)
oscSenderControlPanel = udp_client.SimpleUDPClient("192.168.0.105",7134)
oscSenderWall = udp_client.SimpleUDPClient("192.168.0.106",7134)

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

    def __init__(self, mw_needed, N=961, mul=1.0, offset=0.0, uncertainty=7.0, tau=1):
        self.hours_vector = np.linspace(0, 48, 49, True)
        self.N = N
        self.set_mw_needed(mw_needed, mul, offset, uncertainty, tau)

    def set_mw_needed(self, mw_needed, mul=1.0, offset=0.0, uncertainty=7.0, tau=1):
        """Assign a new hourly demand pattern and recompute all curves."""
        self.offset = offset
        self.uncertainty = uncertainty * mul
        self.tau = tau  # Time constant for demand smoothing
        alpha = OnePole.alpha_from_tau(self.tau, dt)

        self.mw_needed = np.array(mw_needed) * mul + offset
        self.time_vector = np.zeros(self.N)
        self.need_vector = np.full(self.N, self.mw_needed.mean())
        self.need_min_vector = np.zeros(self.N)
        self.need_max_vector = np.zeros(self.N)

        last_need = self.mw_needed[0]
        spline = interp1d(self.hours_vector, self.mw_needed, kind='cubic')
        for x in range(self.N):
            # Clamp to interpolation domain to avoid float overshoot at 48h.
            t = min(dt * x, self.hours_vector[-1])
            last_need = spline(t) * alpha + last_need * (1.0 - alpha)
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

# ==================== SAMPLING TIME ====================
Ts = 0.04  # Wall-clock sampling time in seconds (40 ms between OSC updates)
dt_base = 5/60  # Base simulation time step in hours (1 minute per step for high fidelity)
playback_speed = 0.2  # Playback speed factor: <1 = slower, >1 = faster. 0.1 = 10x slower than base
dt = dt_base * playback_speed  # Actual simulation time step in hours
N = int(np.ceil(48.0 / dt)) + 1  # Total steps to cover 48 hours

# Game
point_score = 0

# Equivalent taus for legacy OnePole alphas using the fixed legacy step.
tau_wind_f1 = 15
tau_wind_f2 = 1.0
tau_sun_day = 2
tau_sun_night = 1
tau_power_filter = 1
tau_turbine_filter = 0.5
tau_emission_filter = 1
tau_power_up = 5
tau_power_down = 5
tau_power_empty = 1

# Power Ranges 
power_plant_max = 600.0 # Max power output of the plant in MW
wind_power_max = 300.0  
sun_power_max = 200.0 

# Lookup Tables
oven_temp_lookup = LookupTable(
    [0.00, 0.20, 0.50, 0.75, 1.00],
    [0.00, 0.20, 0.40, 0.60, 1.00])

oven_disp_lookup = LookupTable(
    [0.0, 0.1, 0.20, 0.3, 0.4, 0.5, 0.6 , 0.7 , 0.8 , 0.9, 1.0],
    [0.0, 0.2, 0.35, 0.5, 0.6, 0.7, 0.85, 0.9 , 0.95, 1.0, 1.0])

oven_pct_to_power_lookup = LookupTable(
    [0.0, 0.05, 0.2, 0.4, 0.8, 1.0],
    [0.0, 0.1 , 0.3, 0.5, 1.0, 1.0])

air_to_power_lookup = LookupTable(
    [0.0 , 0.2, 0.5, 0.8, 1.0],
    [0.01, 0.1, 0.4, 0.9, 1.0])

print(f"Wall-clock Ts = {Ts*1000:.1f} ms, Playback speed = {playback_speed}x")
print(f"Simulation dt = {dt*3600:.1f} seconds/step, {dt*60:.2f} min/step")
print(f"N = {N} steps for 48 hours (runtime ~{N*Ts:.1f} seconds)")
# =====================================================

# instantiate requirement object; electricity and heat profiles can be changed independently
class EnergyRequirements:
    def __init__(self):
        # Set default curves for both electricity and heat; they can be changed independently at runtime using the set_mw_needed method
        self.electricity = EnergyRequirement(default_mw_needed, N=N, uncertainty=9.0, tau=4, offset= 100.0, mul=8.0)
        self.heat        = EnergyRequirement(default_mw_needed, N=N, uncertainty=9.0, tau=12, offset=-80.0, mul=9.0)

    def get_total_need_vector(self):
        return self.electricity.need_vector + self.heat.need_vector

    def get_total_need_at(self, index):
        return self.electricity.need_vector[index] + self.heat.need_vector[index]

    def get_electricity_quotient_at(self, index):
        return self.electricity.need_vector[index] / self.get_total_need_at(index)
    
    def get_heat_quotient_at(self, index):
        return self.heat.need_vector[index] / self.get_total_need_at(index)


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
reset_on_start = False

# ------------------------------------------------------------------------------------------- #
# ---------------------------------- Wind Generator ----------------------------------------- #
# ------------------------------------------------------------------------------------------- #
class WindGenerator:
    def __init__(self):
        self.max = wind_power_max  # Max Wind Power in MW
        self.n = 0
        self.N = 15
        self.mean = 150.0
        self.sd = 100.0
        self.f1 = OnePole(tau_wind_f1, self.mean)
        self.f2 = OnePole(tau_wind_f2, self.mean)
        self.power = self.mean
        self.tmp = self.mean
        self.vector = np.zeros(N)
        self.active = True

    def activate(self, active):
        self.active = active

    def isActive(self):
        return self.active
    
    def calculate(self):
        if self.n >= self.N:
            self.tmp = np.random.normal(self.mean, self.sd)
            self.n = 0
        else:
            self.n = self.n + 1

        self.f1.update(self.tmp, dt)
        self.f2.update(self.f1.get(), dt)
        self.power = min(max(self.f2.get(), 0), self.max)
        return self.power
    
    def make_new_vector(self):
        # Reset Wind
        self.mean = np.random.normal(self.max/4, self.max/4)
        if self.mean < 0:
            self.mean = 0
        self.sd = abs(np.random.normal(self.max/4, self.max))
        
        self.f1.reset(self.mean)
        self.f2.reset(self.mean)
        self.power = self.mean
        self.tmp = self.mean
        for x in range(N):
            self.vector[x] = self.calculate()
            # print(self.vector[x])

    def get_available_power(self, index):
        return self.vector[index]

    def get(self, index):
        if self.active:
            return self.get_available_power(index)
        else:
            return 0.0

# ------------------------------------------------------------------------------------------- #
# ---------------------------------- Sun Generator ------------------------------------------ #
# ------------------------------------------------------------------------------------------- #
class SunGenerator:
    def __init__(self):
        # use the current average electricity demand for scaling
        self.max = sun_power_max  # Max Solar Power in MW, scaled to be a fraction of the average electricity demand
        self.f1 = OnePole(tau_sun_day, 0.0)
        self.f2 = OnePole(tau_sun_day, self.f1.get())
        self.power = 0.0
        self.vector = np.zeros(N)
        self.active = True
    
    def activate(self, active):
        self.active = active

    def isActive(self):
        return self.active

    def calculate(self, td):
        sol = 0.0
        sol_tau = tau_sun_day
        if td > 5 and td < 13:
            sol = self.max
        else:
            sol = 0.0
            sol_tau = tau_sun_night
        
        # Two stage lowpass filter to create a smoother curve
        sol = self.f1.update_tau(sol, sol_tau, dt)
        sol = self.f2.update_tau(sol, sol_tau, dt)

        self.power = sol
        
        return self.power
    
    def make_new_vector(self):
        self.__init__()  # Reset the sun generator to create a new sun profile
        for x in range(N):
            td = timeOfDay(dt * x)  # Use dt for time calculation
            self.vector[x] = self.calculate(td)

    def get_available_power(self, index):
        return self.vector[index]

    def get(self, index):
        if self.active:
            return self.get_available_power(index)
        else:
            return 0.0




# ------------------------------------------------------------------------------------------- #
# ---------------------------------- PowerPlant --------------------------------------------- #
# ------------------------------------------------------------------------------------------- #
@dataclass
class BurnableWaste:
    amount: float = 0.0
    energy_density: float = 1.0
    acid_amount: float = 0.5
    co_amount: float = 0.5
    moisture_content: float = 0.15

    def copy_with_amount(self, amount):
        return BurnableWaste(
            amount=max(amount, 0.0),
            energy_density=self.energy_density,
            acid_amount=self.acid_amount,
            co_amount=self.co_amount,
            moisture_content=self.moisture_content,
        )

    def effective_energy_factor(self):
        # Moisture reduces the effective energy released from the fuel.
        return max(0.2, self.energy_density * (1.0 - 0.6 * self.moisture_content))


class BurnableWasteStorage:
    # Preset profiles for common waste streams delivered to the plant.
    # Keys are both the integer index and the lowercase name (resolved in _waste_from_type).
    WASTE_TYPES = {
        0: dict(name="household",  energy_density=0.85, acid_amount=0.90, co_amount=0.88, moisture_content=0.22),
        1: dict(name="paper",      energy_density=0.90, acid_amount=0.60, co_amount=0.75, moisture_content=0.08),
        2: dict(name="plastic",    energy_density=1.40, acid_amount=1.10, co_amount=2.10, moisture_content=0.02),
        3: dict(name="wood",       energy_density=1.05, acid_amount=0.70, co_amount=0.90, moisture_content=0.25),
        4: dict(name="industrial", energy_density=1.20, acid_amount=1.80, co_amount=1.60, moisture_content=0.05),
    }
    _WASTE_TYPE_BY_NAME = {v["name"]: k for k, v in WASTE_TYPES.items()}

    def __init__(self, capacity):
        self.capacity = max(capacity, 0.0)
        self.waste_batches = []
        self.clear()  # Fill with random waste batches on initialization

    @staticmethod
    def _waste_from_type(waste_type, amount):
        """Return a BurnableWaste with preset properties for the given type name or index."""
        if isinstance(waste_type, str):
            if waste_type.lower() == "random":
                idx = np.random.randint(0, len(BurnableWasteStorage.WASTE_TYPES))
            else:
                idx = BurnableWasteStorage._WASTE_TYPE_BY_NAME.get(waste_type.lower())
                if idx is None:
                    valid = list(BurnableWasteStorage._WASTE_TYPE_BY_NAME)
                    raise ValueError(f"Unknown waste type '{waste_type}'. Valid names: {valid}")
        else:
            idx = int(waste_type)
            if idx == -1:
                idx = np.random.randint(0, len(BurnableWasteStorage.WASTE_TYPES))
        props = BurnableWasteStorage.WASTE_TYPES.get(idx)
        if props is None:
            valid = list(BurnableWasteStorage.WASTE_TYPES)
            raise ValueError(f"Unknown waste type index {waste_type}. Valid indices: {valid}")
        return BurnableWaste(
            amount=max(amount, 0.0),
            energy_density=props["energy_density"],
            acid_amount=props["acid_amount"],
            co_amount=props["co_amount"],
            moisture_content=props["moisture_content"],
        )

    def total_amount(self):
        return sum(batch.amount for batch in self.waste_batches)

    def available_capacity(self):
        return max(self.capacity - self.total_amount(), 0.0)

    def fill_ratio(self):
        if self.capacity <= 0:
            return 0.0
        return self.total_amount() / self.capacity

    def add_waste(self, waste, amount=10.0):
        """Add waste to storage.

        waste can be:
          - a BurnableWaste object  (existing behaviour; amount parameter ignored)
          - a str name, e.g. 'household', 'paper', 'plastic', 'organic', 'wood', 'industrial'
          - an int index 0-5 matching the same order
        When a type name/index is given, `amount` sets the quantity added (default 10).
        """
        if isinstance(waste, (str, int)):
            waste = BurnableWasteStorage._waste_from_type(waste, amount)
        if waste is None or waste.amount <= 0:
            return 0.0
        added_amount = min(waste.amount, self.available_capacity())
        if added_amount <= 0:
            return 0.0
        self.waste_batches.append(waste.copy_with_amount(added_amount))
        return added_amount

    def clear(self):
        """Empty the storage and refill it with random waste batches of size 9."""
        self.waste_batches = []
        while self.available_capacity() >= 9.0:
            self.add_waste(-1, 9.0)

    def remove_waste(self, amount):
        amount_to_remove = min(max(amount, 0.0), self.total_amount())
        if amount_to_remove <= 0:
            return BurnableWaste()

        removed = []
        remaining = amount_to_remove
        while remaining > 0 and self.waste_batches:
            batch = self.waste_batches[0]
            take = min(batch.amount, remaining)
            removed.append(batch.copy_with_amount(take))
            batch.amount -= take
            remaining -= take
            if batch.amount <= 0:
                self.waste_batches.pop(0)

        return self._blend(removed)

    def _blend(self, waste_parts):
        if not waste_parts:
            return BurnableWaste()
        total = sum(w.amount for w in waste_parts)
        if total <= 0:
            return BurnableWaste()

        def weighted(attr):
            return sum(getattr(w, attr) * w.amount for w in waste_parts) / total

        return BurnableWaste(
            amount=total,
            energy_density=weighted('energy_density'),
            acid_amount=weighted('acid_amount'),
            co_amount=weighted('co_amount'),
            moisture_content=weighted('moisture_content'),
        )


class PowerPlant:
    def __init__(self, requirements):
        # Ref to requirements for scaling power output and emissions
        self.requirements = requirements
        # Parameters related to the storage of burnable waste
        self.storage_amount_max = 140.0
        self.storage = BurnableWasteStorage(self.storage_amount_max)
       
        # oven state
        self.oven_amount_initial = 15.0
        self.oven_amount = self.oven_amount_initial
        self.oven_waste = BurnableWaste(
            amount=self.oven_amount_initial,
            energy_density=1.0,
            acid_amount=0.8,
            co_amount=0.7,
            moisture_content=0.16,
        )
        self.oven_amount_max = 30.0
        self.oven_amount_ok_min = 16.0
        self.oven_amount_ok_max = 25.0
        self.oven_amount_to_fill = 3.0
        self.oven_consumption_rate = 1.0
        # Air flow state
        self.air_flow = 0.5

        # power generation state
        self.power_max = power_plant_max  # MW
        self.calorific_scaling = 1.7
        self.tau_up = tau_power_up
        self.tau_down = tau_power_down
        self.tau_empty = tau_power_empty
        # initialise filter using the current electricity requirement baseline
        self.power_filter = OnePole(tau_power_filter, self.requirements.get_total_need_at(0))
        self.lambda_val = 1.0
        self.v1 = 0.0

        # Turbine amount, i.e. the percentage of power that is converted to electricity
        self.turbine_pct = requirements.get_electricity_quotient_at(0)
        self.turbine_pct_filter = OnePole(tau_turbine_filter, self.turbine_pct)

        # Emission
        self.CaCO3_amount = 0.0
        self.NaOH_amount = 0.0
        self.acid_emission = OnePole(tau_emission_filter, 0.0)
        self.CO_emission = OnePole(tau_emission_filter, 0.0)

        # Active state
        self.active = True

    def activate(self, active):
        self.active = active

    def isActive(self):
        return self.active

    def get_storage_pct(self):
        return self.storage.fill_ratio()
    
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
        if self.active:
            return self.power_filter.get() * self.get_electricity_pct()
        else:
            return 0.0
    
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
        temp = oven_temp_lookup.get(self.get_total_power_pct())
        return temp
    
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
        if space <= 0:
            return

        amount_to_add = min(self.oven_amount_to_fill, space)
        incoming_waste = self.storage.remove_waste(amount_to_add)
        if incoming_waste.amount <= 0:
            return

        current_oven_amount = max(self.oven_waste.amount, 0.0)
        total = current_oven_amount + incoming_waste.amount
        if total <= 0:
            return

        def blend(current_value, incoming_value):
            return (current_value * current_oven_amount + incoming_value * incoming_waste.amount) / total

        self.oven_waste = BurnableWaste(
            amount=total,
            energy_density=blend(self.oven_waste.energy_density, incoming_waste.energy_density),
            acid_amount=blend(self.oven_waste.acid_amount, incoming_waste.acid_amount),
            co_amount=blend(self.oven_waste.co_amount, incoming_waste.co_amount),
            moisture_content=blend(self.oven_waste.moisture_content, incoming_waste.moisture_content),
        )
        self.oven_amount = self.oven_amount + incoming_waste.amount
    
    def calculate_acid_emission(self):
        # Waste chemistry drives baseline emissions; additives reduce them.
        acid_emission = self.get_total_power_pct() * (self.oven_waste.acid_amount - self.CaCO3_amount)
        if acid_emission < 0: 
            acid_emission = 0
        elif acid_emission > 1:
            acid_emission = 1            
        return self.acid_emission.update(acid_emission, dt)
        
    def calculate_CO_emission(self):
        lambda_scaling = 1.0
        if self.get_lambda() < 1.0:
            lambda_scaling = 1.0 + 2.0 * (1.0 - self.get_lambda())  # Sharp increase as lambda drops below 1
        CO_emission = self.get_total_power_pct() * (self.oven_waste.co_amount - self.NaOH_amount) * lambda_scaling
        if CO_emission < 0: 
            CO_emission = 0
        elif CO_emission > 1:
            CO_emission = 1
        return self.CO_emission.update(CO_emission, dt)

    def calculate_power(self):
        oven_pct       = self.get_oven_pct()
        oven_pct_squared = oven_pct * oven_pct
        airflow = self.air_flow
        airflow_squared = airflow * airflow
        airflow_cubed = airflow * airflow * airflow
        # moisture       = self.oven_waste.moisture_content
        moisture = 0.0
        energy_density = self.oven_waste.energy_density

        # --- Lambda: air-to-fuel ratio ---
        # Stoichiometric point: air_flow == oven_pct → λ = 1.
        # λ < 1 → oxygen-starved, incomplete combustion, CO rises.
        # λ > 1 → excess air → good burn but progressive dilutive cooling.
        self.lambda_val = airflow / (oven_pct + 1e-9)

        # if lambda_val < 1.0:
        #     combustion_eff = lambda_val                    # limited by O₂
        # elif lambda_val <= 1.3:
        #     combustion_eff = 1.0                           # optimal lean-burn window
        # else:
        #     combustion_eff = max(0.25, 1.3 / lambda_val)  # excess-air cooling

        if self.lambda_val < 1.0:
            combustion_eff = self.lambda_val               # limited by O₂
        else:
            combustion_eff = 1.0                           # optimal lean-burn window

        # --- Moisture factor ---
        # Wet waste must first evaporate its water before combustion can proceed,
        # reducing both the burn rate and the net calorific value released.
        moisture_factor = max(0.0, 1.0 - 1.2 * moisture)

        # --- Burn rate: waste consumed per time-step ---
        # Driven by oxygen supply (air_flow) and combustion surface area (oven_pct),
        # strongly dampened by moisture.
                
        burn_rate = (1+airflow_cubed*2) * (1+oven_pct) * moisture_factor * self.oven_consumption_rate
        burn_rate *= dt # Scale by time step to maintain consistent behavior across different dt values
        
        self.oven_amount = max(self.oven_amount - burn_rate, 0.0)
        self.oven_waste.amount = self.oven_amount

        # --- Target thermal power ---
        # Released heat ∝ burn_rate × net calorific value × combustion efficiency.
        # Net calorific value: moisture lowers energy yield.
        if self.oven_amount <= 0.0:
            self.power_filter.update_tau(0.0, self.tau_empty, dt)
        else:
            net_calorific = energy_density * moisture_factor * self.calorific_scaling
            target_power  = air_to_power_lookup.get(airflow) * oven_pct_to_power_lookup.get(oven_pct) * net_calorific * combustion_eff * self.power_max
            target_power  = max(0.0, min(self.power_max * 1.05, target_power))

            tau = self.tau_up if target_power > self.power_filter.get() else self.tau_down
            self.power_filter.update_tau(target_power, tau, dt)

        return self.power_filter.get()
    
    def calculate(self):
        # Update the turbine filter
        self.turbine_pct_filter.update(self.turbine_pct, dt)
        # Calculate the power output of the plant
        power = self.calculate_power()
        # Calculate the emissions
        self.calculate_acid_emission()
        self.calculate_CO_emission()
        # Return the power output
        return power

    def reset(self):
        self.__init__(self.requirements)
       
# Class to keep track of the amount of MW being bought from or sold to the market. 
# Negative values indicate selling to the market, positive values indicate buying from the market.
class ElectricMarket:
    def __init__(self):
        self.reset()
        self.max = 200.0  # Max amount of MW that can be bought or sold
        self.batch = 40.0  # Amount of MW to buy/sell in one transaction

    def set(self, amount):
        self.amount = amount

    def get(self):
        return self.amount
    
    def reset(self):
        self.amount = 0.0

    def sell(self):
        self.amount = max(self.amount - self.batch, -self.max)

    def buy(self):
        self.amount = min(self.amount + self.batch, self.max)

# EnergyGrid class to manage the overall energy production and consumption balance
class EnergyGrid:
    def __init__(self):
        self.requirements = EnergyRequirements()
        self.wind_generator = WindGenerator()
        self.sun_generator = SunGenerator()
        self.powerplant = PowerPlant(self.requirements)
        self.electric_market = ElectricMarket()

    def reset(self):
        self.wind_generator.make_new_vector()
        self.sun_generator.make_new_vector()
        self.powerplant.reset()
        self.electric_market.reset()

    def get_total_electricity(self, index):
        return self.wind_generator.get(index) + self.sun_generator.get(index) + self.powerplant.get_electric_power() + self.electric_market.get()       

    def get_total_heat(self, index):
        return self.powerplant.get_heat_power()

    def get_total_production(self, index):
        return self.wind_generator.get(index) + self.sun_generator.get(index) + self.powerplant.get_total_power() + self.electric_market.get()  

    def calculate(self, index):
        # Calculate the power plant output first as it depends on the current state of the oven and air flow
        plant_power = self.powerplant.calculate()
        # Then calculate the wind and sun power for the current time step
        wind_power = self.wind_generator.get(index)
        sun_power = self.sun_generator.get(index)
        market_power = self.electric_market.get()
        # Return the total production
        return wind_power + sun_power + plant_power + market_power



energy_grid = EnergyGrid()



def plot_electricity(fig):
    global lb,lheat,ls,lel,score_text
    ax = fig.gca()  # Get the current axes
    ax.set_xlim([0,48]) # Set the x-limits
    ax.set_ylim([0,600]) # Set the y-limits
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
    score_text = ax.text(
        0.98,
        0.98,
        f"Score: {int(point_score)}",
        transform=ax.transAxes,
        ha='right',
        va='top',
        fontsize=24,
        fontweight='bold',
        bbox=dict(facecolor='white', alpha=0.8, edgecolor='black')
    )

    plt.legend(loc='upper left')
    plt.grid(True)

def plot_heat(fig):
    global lheat
    ax = fig.gca()  # Get the current axes
    ax.set_xlim([0,48]) # Set the x-limits
    ax.set_ylim([0,600]) # Set the y-limits
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

fig2 = create_plot_on_monitor(monitors[1 if FullScreen else 0], plot_heat)  # Assign to monitor 0
plt.tight_layout()

def send_osc_to_panel():
    oscSenderControlPanel.send_message("/OvenAmount", energy_grid.powerplant.oven_amount/energy_grid.powerplant.oven_amount_max)
    oscSenderControlPanel.send_message("/PlantPower", energy_grid.powerplant.get_total_power_pct())
    oscSenderControlPanel.send_message("/WindPower", energy_grid.wind_generator.get_available_power(index)/energy_grid.wind_generator.max)
    oscSenderControlPanel.send_message("/SolarPower", energy_grid.sun_generator.get_available_power(index)/energy_grid.sun_generator.max)
    oscSenderControlPanel.send_message("/Acid", energy_grid.powerplant.get_acid_emission())
    oscSenderControlPanel.send_message("/CO", energy_grid.powerplant.get_CO_emission())
    oscSenderControlPanel.send_message("/PlantElectricPower", energy_grid.powerplant.get_electric_power_pct())
    oscSenderControlPanel.send_message("/OvenTemp", energy_grid.powerplant.get_oven_temperature_pct())
    oscSenderControlPanel.send_message("/CaCO3", energy_grid.powerplant.CaCO3_amount)
    oscSenderControlPanel.send_message("/NaOH", energy_grid.powerplant.NaOH_amount)
    oscSenderControlPanel.send_message("/TurbinePct", energy_grid.powerplant.get_electricity_pct())
    oscSenderControlPanel.send_message("/OvenAirFlow", energy_grid.powerplant.get_air_flow())
    oscSenderControlPanel.send_message("/Buy", energy_grid.electric_market.get()/energy_grid.electric_market.max)

def send_osc_to_oven_display():
    oscSenderOvenDisplay.send_message("/OvenIntensity", oven_disp_lookup.get(energy_grid.powerplant.get_total_power_pct()))

def send_osc_to_storage_display():
    oscSenderStorageDisplay.send_message("/WasteStorage", energy_grid.powerplant.get_storage_pct())

def send_osc_to_wall():
    oscSenderWall.send_message("/Wall", [
        energy_grid.powerplant.get_oven_temperature_pct(), # Ove
        energy_grid.powerplant.get_electricity_pct(),
        1 if energy_grid.wind_generator.isActive() else 0, 
        1 if energy_grid.sun_generator.isActive() else 0,
        1 if energy_grid.powerplant.isActive() else 0,
        energy_grid.electric_market.get()/energy_grid.electric_market.max,
        energy_grid.powerplant.CaCO3_amount,
        energy_grid.powerplant.NaOH_amount,
        ])

def send_osc_reset():
    oscSenderControlPanel.send_message("/Reset",1)
    oscSenderOvenDisplay.send_message("/Reset",1)
    oscSenderStorageDisplay.send_message("/Reset",1)
    oscSenderWall.send_message("/Reset",1)

# Non-blocking OSC sender using thread pool
def send_osc_async():
    """Send OSC data in background thread to avoid blocking rendering"""
    executor.submit(send_osc_to_panel)
    executor.submit(send_osc_to_oven_display)
    executor.submit(send_osc_to_storage_display)
    executor.submit(send_osc_to_wall)

def updatePlot():
    lel.set_xdata(x_values)
    lel.set_ydata(el_plot_values)
    score_text.set_text(f"Score: {int(point_score)}")

def updateHeatPlot():
    lheat.set_xdata(x_values)
    lheat.set_ydata(heat_plot_values)

def clear():
    global x_values, el_plot_values, b_values, heat_plot_values, s_values, index, run, t, td, reset_on_start, point_score
    global production_filter

    run = 0
    x_values = []
    el_plot_values = []
    heat_plot_values = []
    b_values = []
    s_values = []
    energy_grid.reset()
    index = 0
    t = 0
    td = 0
    reset_on_start = False
    point_score = 0

    updatePlot()
    updateHeatPlot()
    send_osc_reset()
    
def start(value):
    global run
    if value:
        if reset_on_start: 
            clear()
        run = 1
    else:
        run = 0


# Game loop thread - runs independently at fixed Ts rate
game_loop_thread = None
game_loop_running = False

def calculate_score(i):
    # Simple scoring based on how well production matches requirement
    score = 100.0
    
    # Electricity Punishment
    electricity_min = energy_grid.requirements.electricity.need_min_vector[i]
    electricity_need = energy_grid.requirements.electricity.need_vector[i]
    electricity_prod = energy_grid.get_total_electricity(i)

    if electricity_prod < electricity_min:
        score -= (electricity_min - electricity_prod) # Extra penalty for not meeting minimum electricity requirement
        score -= abs(electricity_prod - electricity_need) # Penalize electricity mismatch
    else:
        score -= abs(electricity_prod - electricity_need) * 0.1 # Penalize electricity mismatch

    # Heat Punishment
    heat_min  = energy_grid.requirements.heat.need_min_vector[i]
    heat_need = energy_grid.requirements.heat.need_vector[i]
    heat_prod = energy_grid.get_total_heat(i)

    if heat_prod < heat_min:
        score -= abs(heat_prod - heat_need) * 0.2 # Penalize heat mismatch
        score -= (heat_min - heat_prod)
    else: 
        score -= abs(heat_prod - heat_need) * 0.1 # Penalize heat mismatch

    # Emissions Punishment
    score -= abs(energy_grid.powerplant.oven_waste.acid_amount - energy_grid.powerplant.CaCO3_amount) * 10
    score -= abs(energy_grid.powerplant.oven_waste.co_amount   - energy_grid.powerplant.NaOH_amount) * 10

    score = max(1,score/10)

    # print(score)
    return score
    
    


def game_loop():
    """Separate game simulation loop running at fixed Ts rate (wall-clock)"""
    global index, run, t, td, reset_on_start, point_score
    
    while True:
        if run > 0:
            t = index * dt  # Simulation time in hours
            td = timeOfDay(t)
            energy_grid.calculate(index)
            x_values.append(t)
            el_plot_values.append(energy_grid.get_total_electricity(index))
            heat_plot_values.append(energy_grid.get_total_heat(index))
            
            # Send OSC data at game loop frequency (faster updates every Ts wall-clock)
            send_osc_async()

            point_score += calculate_score(index)
            # print(point_score)
            
            if t >= 48.0:
                run = 0
                reset_on_start = True
                print("Consumption {0}".format(index))
            
            index = index + 1
        
        # Sleep for exactly Ts wall-clock time to maintain fixed update frequency
        time.sleep(Ts)

# Animate Function for the plotting - UPDATE ONLY (no simulation)
def animate(i):
    if run > 0:
        updatePlot()

    time.sleep(0.01)

def animateHeat(i):
    if run > 0:
        updateHeatPlot()

    time.sleep(0.01)


def exit_program(event=None):
    """Close plots and exit the program."""
    global run
    run = 0
    try:
        server.shutdown()
        server.server_close()
    except Exception:
        pass
    try:
        plt.close('all')
    finally:
        raise SystemExit(0)


def on_key_press(event):
    if event.key == 'escape':
        exit_program()
    elif event.key == 'r':
        clear()
        start(True)


def bind_exit_keys():
    fig1.canvas.mpl_connect('key_press_event', on_key_press)
    fig2.canvas.mpl_connect('key_press_event', on_key_press)


def handle_sigint(signum, frame):
    exit_program()

# --------------------------------------------------------------
# ------------------------- OSC --------------------------------
# --------------------------------------------------------------
def oscAmountInOven(addr, value):
    energy_grid.powerplant.oven_amount = value
    print("[{0}] ~ {1}".format(addr, energy_grid.powerplant.oven_amount))

# Setup the OSC Functionality
dispatcher = dispatcher.Dispatcher()
parser = argparse.ArgumentParser()
parser.add_argument("--ip", default="0.0.0.0", help="The ip to listen on")
parser.add_argument("--port", type=int, default=7133, help="The port to listen on")
args = parser.parse_args()
dispatcher.map("/OvenAirFlow", lambda addr, value: energy_grid.powerplant.set_air_flow(value))
dispatcher.map("/Start",  lambda addr, value: start(value))
dispatcher.map("/Reset", lambda addr, value: clear())
# dispatcher.map("/AmountInOven", oscAmountInOven)
dispatcher.map("/UseWind", lambda addr, value: energy_grid.wind_generator.activate(value))
dispatcher.map("/UseSun", lambda addr, value: energy_grid.sun_generator.activate(value))
dispatcher.map("/UsePlant", lambda addr, value: energy_grid.powerplant.activate(value))
dispatcher.map("/FillOven", lambda addr, value: energy_grid.powerplant.fill_oven())
dispatcher.map("/CaCO3", lambda addr, value: energy_grid.powerplant.set_CaCO3_amount(value))
dispatcher.map("/NaOH", lambda addr, value: energy_grid.powerplant.set_NaOH_amount(value))
dispatcher.map("/EnergyDist", lambda addr, value: energy_grid.powerplant.set_turbine_pct(1.0-value))
dispatcher.map("/Buy", lambda addr, value: energy_grid.electric_market.buy())
dispatcher.map("/Sell", lambda addr, value: energy_grid.electric_market.sell())

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
bind_exit_keys()
signal.signal(signal.SIGINT, handle_sigint)
start(True)

# Start the game loop thread
game_loop_thread = Thread(target=game_loop)
game_loop_thread.daemon = True  # Make it a daemon thread
game_loop_thread.start()

# Start the Animation Function with optimized settings
# Use larger interval (100ms) since game updates are independent
ani1 = FuncAnimation(fig1, animate, interval=100, blit=False, cache_frame_data=False)
ani2 = FuncAnimation(fig2, animateHeat, interval=100, blit=False, cache_frame_data=False)

plt.figure(fig1.number)
fig1.canvas.manager.window.attributes('-fullscreen', FullScreen)
fig1.canvas.draw()

plt.figure(fig2.number)
fig2.canvas.manager.window.attributes('-fullscreen', FullScreen)
fig2.canvas.draw()

# Show the plots (this will block until the windows are closed)
try:
    plt.show()
except KeyboardInterrupt:
    exit_program()


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