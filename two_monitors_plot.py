# This script demonstrates how to create two separate matplotlib plots and display 
# them on two different monitors using the xrandr command to get monitor information. 
# It uses the TkAgg backend for window management and positions the windows according 
# to the monitor configurations. The script creates a sine wave plot on one monitor 
# and a cosine wave plot on the other monitor, both in fullscreen mode.

import subprocess
import re
import matplotlib
matplotlib.use('TkAgg')  # Use TkAgg backend for window management
import matplotlib.pyplot as plt
import numpy as np

plt.rcParams['toolbar'] = 'None'  # Disable the interactive toolbar

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
    fig = plt.figure(figsize=(width/100, height/100))  # Size in inches, approximate
    plot_func(fig)
    # Position the window
    fig.canvas.manager.window.geometry(f"{width}x{height}+{x}+{y}")
    fig.canvas.manager.window.deiconify()  # Make the window visible
    fig.canvas.manager.window.update()  # Update the window to apply position
    return fig

def plot_sine(fig):
    x = np.linspace(0, 10, 100)
    y = np.sin(x)
    plt.plot(x, y)
    plt.title('Sine Wave')

def plot_cosine(fig):
    x = np.linspace(0, 10, 100)
    y = np.cos(x)
    plt.plot(x, y)
    plt.title('Cosine Wave')

if __name__ == "__main__":
    monitors = get_monitor_info()
    print("Detected monitors:", monitors)
    if len(monitors) < 2:
        print("Need at least two monitors connected.")
        exit(1)

    plt.ioff()  # Turn off interactive mode to prevent blocking

    # Create plots on each monitor
    fig1 = create_plot_on_monitor(monitors[1], plot_sine)  # Assign to monitor 1
    plt.tight_layout()
    fig2 = create_plot_on_monitor(monitors[0], plot_cosine)  # Assign to monitor 0
    plt.tight_layout()

    plt.figure(fig1.number)
    fig1.canvas.manager.window.attributes('-fullscreen', True)
    fig1.canvas.draw()

    plt.figure(fig2.number)
    fig2.canvas.manager.window.attributes('-fullscreen', True)
    fig2.canvas.draw()


    plt.show()    















