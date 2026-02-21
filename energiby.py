import argparse
import time
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
import numpy as np
import numpy.matlib

import math
from pythonosc import dispatcher
from pythonosc import osc_server
from pythonosc import udp_client
from threading import Thread 

from pprint import pprint
import urllib.request

import json

#plt.style.use('fivethirtyeight')
plt.rcParams['toolbar'] = 'None'


oscSenderTeensy = udp_client.SimpleUDPClient("127.0.0.1",7134)

# Variables used for the live plot
global x_values, y_values, bio_raw, index, run, t, td

# Data about the electrical system
hours_vector = np.linspace(0,48,49,True)

# TIME       0     1     2     3     4     5     6     7     8     9     10    11    12    13    14    15    16    17    18    19    20    21    22    23    24 
mw_needed = [24.0, 26.0, 27.0, 28.5, 32.5, 37.0, 39.0, 41.0, 40.0, 37.0, 32.0, 27.0, 21.0, 17.0, 16.0, 12.0, 18.0, 23.0, 29.0, 32.0, 26.0, 20.0, 16.0, 20.0,
             22.0, 25.0, 27.0, 29.0, 33.0, 38.0, 40.0, 40.0, 39.0, 37.0, 32.0, 27.0, 21.0, 17.0, 16.0, 12.0, 18.0, 23.0, 29.0, 32.0, 26.0, 20.0, 18.0, 20.0, 24.0]
mw_needed = np.array(mw_needed) + 5
print(mw_needed)

time_vector = np.zeros(481)
need_vector = np.zeros(481)
need_min_vector = np.zeros(481)
need_max_vector = np.zeros(481)

lastNeed = mw_needed[0]

for x in range(481):
    t = 0.1 * x
    alpha = 0.05
    lastNeed = np.interp(t, hours_vector, mw_needed)*alpha + lastNeed*(1.0-alpha)
    need_vector[x] = lastNeed
    need_min_vector[x] = lastNeed - 5
    need_max_vector[x] = lastNeed + 5
    time_vector[x] = t


steps = 0
firstStep = True

# Add create consumption range
# e1 = 0.1
# e2 = 0.1
# consumption_min = mw_needed*(1.0-abs(e1))
# consumption_max = mw_needed*(1.0+abs(e2))

print(hours_vector.shape)
print(mw_needed.shape)


def timeOfDay(t):
    while(t > 24.0):
        t -= 24.0
    return t


x_values = []
y_values = []
b_values = []
v_values = []
s_values = []
index = 0
run = 0
t = 0
td = 0

production_alpha = 0.9 # Alpha value for 1st order lowpass filter
production_value = need_vector[0]

oven_amount_initial = 13.0
oven_amount = oven_amount_initial
oven_amount_max = 26.0
oven_amount_ok_min = 8.0
oven_amount_ok_max = 18.0
oven_amount_to_fill = 4.0
oven_consumption_rate = 0.3

storage_amount = 64.0
storage_amount_max = 64.0





def fillOven():
    global oven_amount, storage_amount
    spaceInOven = oven_amount_max - oven_amount
    if storage_amount >= oven_amount_to_fill and spaceInOven >= oven_amount_to_fill:
        oven_amount    = oven_amount    + oven_amount_to_fill
        storage_amount = storage_amount - oven_amount_to_fill
    elif spaceInOven >= oven_amount_to_fill:
        oven_amount    = oven_amount + storage_amount
        storage_amount = 0


# ------------------------------------------------------------------------------------------- #
# ---------------------------------- Vind generator ----------------------------------------- #
# ------------------------------------------------------------------------------------------- #
vind_n      = 0
vind_N      = 15
vind_mean   = 20
vind_sd     = 15
vind_alpha  = 0.01 # Alpha value for 1st order lowpass filter
vind_a1     = 0.1  # 
vind_v1     = vind_mean
vind_value  = vind_mean
vind_tmp    = vind_mean
vind_vector = np.zeros(481)

def vind():
    global vind_value, vind_v1, vind_tmp, vind_n, vind_sd
    if vind_n >= vind_N:
        vind_tmp = np.random.normal(vind_mean,vind_sd)
        print(vind_tmp)
        vind_n = 0
    else:
        vind_n = vind_n+1
    vind_v1 = vind_tmp*vind_a1 + vind_v1*(1-vind_a1) # Compute 1st lowpass filter
    vind_value = vind_v1*vind_alpha + vind_value*(1-vind_alpha) # Compute 2nd lowpass filter
    vind_value = max(vind_value,0)
    return vind_value

def makeNewVindParameters():
    global vind_value, vind_v1, vind_tmp, vind_n, vind_sd, vind_mean, vind_vector
    # Reset Vind
    vind_mean   = np.random.normal(10.0, 10.0)
    if vind_mean<0:
        vind_mean = 0
    vind_sd   = abs(np.random.normal(0.0, 15.0))

    vind_v1     = vind_mean
    vind_value  = vind_mean
    vind_tmp    = vind_mean
    for x in range(481):
        vind_vector[x] = vind()


# ------------------------------------------------------------------------------------------- #
# ---------------------------------- Sol generator ------------------------------------------ #
# ------------------------------------------------------------------------------------------- #
sol_alpha  = 0.1 # Alpha value for 1st order lowpass filter
sol_max    = 0.07 * mw_needed.mean()
sol_v1  = 0.0
sol_value  = 0.0
sol_vector = np.zeros(481)


def sol(td):
    global sol_value, sol_v1
    sol = 0.0
    if td > 5 and td < 13: 
        sol_alpha = 0.1
        sol = 1.0
    else:
        sol = 0.0
        sol_alpha = 0.05 

    sol *= sol_max
    sol_v1 = sol*sol_alpha + sol_v1*(1-sol_alpha) # Compute 2nd lowpass filter
    sol_value = sol_v1*sol_alpha + sol_value*(1-sol_alpha) # Compute 2nd lowpass filter
    return sol_value

def makeNewSolVector():
    global sol_value, sol_v1, sol_vector
    sol_value = 0
    sol_v1 = sol_value
    for x in range(481):
        td = timeOfDay(0.1*x)
        sol_vector[x] = sol(td)


# ------------------------------------------------------------------------------------------- #
# ---------------------------------- Engegi Produktion -------------------------------------- #
# ------------------------------------------------------------------------------------------- #
bio_max = 60 # MW
bio_raw = 0.0
bio_alpha_up   = 0.008 # Alpha value for 1st order lowpass filter
bio_alpha_down = 0.004 # Alpha value for 1st order lowpass filter
bio_alpha_empty = 0.01 # Alpha value for 1st order lowpass filter
bio_v1 = 0.0
bio_value = need_vector[0]

def bio():
    global bio_value, bio_v1, oven_amount
    #bio_v1 = bio_raw*bio_alpha + bio_v1*(1-bio_alpha) # Compute 2nd lowpass filter
    bio_v1 = bio_raw
    oven_factor = 0.8 + 0.3*oven_amount/oven_amount_max
    bio_factor = 1.0

    consumption = (0.3 + 0.7*bio_v1/bio_max) * oven_factor * oven_consumption_rate

    if oven_amount > oven_amount_ok_max+0.5:
        consumption *= 1 + (oven_amount - oven_amount_ok_max)
    elif oven_amount < oven_amount_ok_min-0.5:
        bio_factor = max(1 - 0.02*(oven_amount_ok_min - oven_amount), 0.0)

        
    oven_amount = max(oven_amount - consumption, 0.0)

    if oven_amount == 0.0:
        bio_factor = 0
        bio_value = bio_value*(1-bio_alpha_empty) # Compute 2nd lowpass filter
    else: 
        bio_new = bio_v1 * bio_factor
        if bio_new > bio_value:
            bio_value = bio_new*bio_alpha_up + bio_value*(1-bio_alpha_up) # Compute 2nd lowpass filter
        elif bio_new < bio_value:
            bio_value = bio_new*bio_alpha_down + bio_value*(1-bio_alpha_down) # Compute 2nd lowpass filter


    return bio_value

# ------------------------------------------------------------------------------------------- #
# ---------------------------------- Silo --------------------------------------------------- #
# ------------------------------------------------------------------------------------------- #
silo_capacity = 185 # MWh
silo_amount_initial = 70 # MWh
silo_amount = silo_amount_initial
silo_charge = False
silo_charge_rate_max = 18 # MW
silo_charge_rate = 0

silo_use = False
silo_use_rate_max = 18 # MW
silo_use_rate = 0

silo_alpha = 0.3

def silo_init():
    global silo_amount
    silo_amount = silo_amount_initial

def silo_amount_pct():
    return silo_amount / silo_capacity

def silo_charge_process(index):
    global silo_amount, silo_charge_rate

    if silo_charge:
        max_charge_rate = max(min(bio_value-need_vector[index],silo_charge_rate_max),0)
        if silo_amount >= silo_capacity:
            max_charge_rate = 0.0
        silo_charge_rate = max_charge_rate*silo_alpha + silo_charge_rate*(1.0-silo_alpha) # Process Lowpass Filter
    else:
        silo_charge_rate = silo_charge_rate*(1.0-silo_alpha) # Process Lowpass Filter

    silo_charge_k = silo_charge_rate / 10.0

    silo_amount = silo_amount + silo_charge_k 

    if silo_amount > silo_capacity:
        silo_amount = silo_capacity

    return silo_charge_rate

def silo_use_process(index):
    global silo_amount, silo_use_rate

    if silo_use:
        max_use_rate = max(min(need_vector[index]-bio_value,silo_use_rate_max),0)
        silo_use_rate = max_use_rate*silo_alpha + silo_use_rate*(1.0-silo_alpha) # Process Lowpass Filter
    else:
        silo_use_rate = silo_use_rate*(1.0-silo_alpha) # Process Lowpass Filter


    silo_use_k = silo_use_rate / 10.0

    if silo_use_k > silo_amount:
        ret = silo_amount * 10
        silo_amount = 0
        return ret 
    else:
        silo_amount = silo_amount - silo_use_k

    return silo_use_rate


# Compute the Total Production
production_alpha = 0.5 # Alpha value for 1st order lowpass filter
production_value = need_vector[0]

def production(index):
    global production_value
    # p = vind_vector[index] + sol_vector[index] + bio()
    p = bio() - silo_charge_process(index) + silo_use_process(index)
    production_value = p*production_alpha + production_value*(1-production_alpha)
    return production_value


# Define the Figure and axes
fig, (ax1) = plt.subplots(nrows=1, ncols=1, figsize=(8, 4))
plt.subplots_adjust(bottom=0.25)
fig.set_facecolor('grey')
ax1.set_xlim([0,48]) # Set the x-limits
ax1.set_ylim([0,70]) # Set the y-limits
ax1.set_xlabel('Time [h]')
ax1.set_ylabel('Power [MW]')
ax1.set_xticks([0  ,6  ,12  ,18  ,24 ,30 ,36  ,42  ,48])
xlabels = ['0:00','6:00','12:00','18:00','0:00','6:00','12:00','18:00','0:00']
ax1.set_xticklabels(xlabels)

# Plot the Range as to stay within 
ax1.fill_between(time_vector, need_min_vector, need_max_vector, label="Behov")

lb, = ax1.plot(x_values,b_values,'r-', label="Produktion") # Create a line with the data
#lv, = ax1.plot(x_values,v_values,'b-', label="Vind") # Create a line with the data
l,  = ax1.plot(x_values,y_values,'k-', label="Energi Til Net") # Create a line with the data
#ls, = ax1.plot(x_values,s_values,'k-', label="Energi til Net") # Create a line with the data

ax1.legend(loc='upper left')
plt.grid(True)


def sendElData():
    oscSenderTeensy.send_message("/ElData", [vind_vector[index], sol_vector[index], bio_value/bio_max, oven_amount, storage_amount, production_value, need_min_vector[index], run, silo_amount_pct(), t])

def updatePlot():
    l.set_xdata(x_values)
    l.set_ydata(y_values)
    lb.set_xdata(x_values)
    lb.set_ydata(b_values)
    #lv.set_xdata(x_values)
    #lv.set_ydata(v_values)
    #ls.set_xdata(x_values)
    #ls.set_ydata(s_values)
    sendElData()


def clear():
    global x_values, y_values, b_values, v_values, s_values, bio_value, index, run, t, td
    global vind_v1, vind_value, vind_tmp, production_value
    global storage_amount, oven_amount
    global steps
    global silo_amount

    x_values = []
    y_values = []
    b_values = []
    v_values = []
    s_values = []
    storage_amount = storage_amount_max
    oven_amount = oven_amount_initial
    makeNewVindParameters()
    makeNewSolVector()
    bio_value = need_vector[0]
    index = 0
    t = 0
    td = 0
    production_value = need_vector[0]
    steps = 0

    # Init Silo
    silo_init()

    updatePlot()



# Animate Function for the plotting
def animate(i):
    global index, run, t, td, steps
    if run > 0:
        t = index * 0.1
        td = timeOfDay(t)
        y = production(index)
        x_values.append(t)
        y_values.append(y)
        b_values.append(bio_value)
        v_values.append(vind_vector[index])
        s_values.append(sol_vector[index])
        if t >= 48.0:
            run = 0
            print("Consumption {0}".format(index))

        updatePlot()     
        index = index+1

    time.sleep(.02*5)

# --------------------------------------------------------------
# ------------------------- OSC --------------------------------
# --------------------------------------------------------------
# Function to recieve value over osc 
def oscValue(addr, value):
    global bio_raw, bio_max
    bio_raw = value * bio_max
    print("[{0}] ~ {1}".format(addr, bio_raw))

def oscCmd(addr, value):
    global x_values, y_values, index, run, silo_charge, silo_use
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
        fillOven()
    elif value == 'ChargeSiloT':
        silo_charge = True
    elif value == 'ChargeSiloF':
        silo_charge = False
    elif value == 'UseSiloT':
        silo_use = True
    elif value == 'UseSiloF':
        silo_use = False
    elif value == 'Reset':
        run = 0
        clear()
        
    print("[{0}] ~ {1}".format(addr, value))

def oscAmountInOven(addr, value):
    global amountInOven
    amountInOven = value
    print("[{0}] ~ {1}".format(addr, bio_raw))

# Setup the OSC Functionality
dispatcher = dispatcher.Dispatcher()
parser = argparse.ArgumentParser()
parser.add_argument("--ip", default="0.0.0.0", help="The ip to listen on")
parser.add_argument("--port", type=int, default=7133, help="The port to listen on")
args = parser.parse_args()
dispatcher.map("/filter", print)
dispatcher.map("/value", oscValue)
dispatcher.map("/cmd", oscCmd)
dispatcher.map("/AmountInOven", oscAmountInOven)

server = osc_server.ThreadingOSCUDPServer((args.ip, args.port), dispatcher)
print("Serving on {}".format(server.server_address))


# Start Osc in a Thread
oscThread = Thread(target = server.serve_forever)
oscThread.start()

clear()

# Start the Animation Funtion
ani = FuncAnimation(fig, animate, interval = 10)

# Show the plot on screen
plt.ioff()
plt.tight_layout()
#figman = plt.get_current_fig_manager()
#figman.full_screen_toggle()
plt.show()


