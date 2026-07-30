import serial.tools.list_ports
import sys, os

import matplotlib.pyplot as plt
import numpy as np

from queue import Queue

ports = serial.tools.list_ports.comports()
choices = {}

BAUD_RATE = 9600
MAX_POINTS = 5000

for port, desc, hwid in sorted(ports):
    print(f"{port}: {desc} [{hwid}]")
    choices[port] = desc

print("*" * os.get_terminal_size()[0])

if len(choices) == 0:
    print("No COM ports were found, aborting...")
    sys.exit(0)

choice = input("Choose the COM port you want... ").upper()

if choice.strip() == "":
    choice = list(choices.keys())[0]
else:
    if choice not in choices:
        if f"COM{choice}" in choices:
            choice = f"COM{choice}"
        elif f"/dev/ttyUSB{choice}" in choices:
            choice = f"/dev/ttyUSB{choice}"
        else:
            print("That COM port was not found, aborting...")
            sys.exit(0)

print(f"Port {choice} was chosen")

x_data = Queue(maxsize=MAX_POINTS)
y_data = Queue(maxsize=MAX_POINTS)
z_data = Queue(maxsize=MAX_POINTS)



def display_plot():
    plt.ion()

    global fig, ax
    fig, ax = plt.subplots()

    global x_cloud, y_cloud, z_cloud
    x_cloud, = ax.plot([], [], "o", markersize=5, label="XY plane", alpha=1.0)
    y_cloud, = ax.plot([], [], "o", markersize=5, label="YZ plane", alpha=1.0)
    z_cloud, = ax.plot([], [], "o", markersize=5, label="ZX plane", alpha=1.0)

    ax.set_title("QMC5883L Calibration")
    ax.legend()

def update_plot():
    x_cloud.set_data([coord[0] for coord in x_data.queue], [coord[1] for coord in x_data.queue])
    y_cloud.set_data([coord[0] for coord in y_data.queue], [coord[1] for coord in y_data.queue])
    z_cloud.set_data([coord[0] for coord in z_data.queue], [coord[1] for coord in z_data.queue])

    ax.relim()
    ax.autoscale_view()
    ax.set_aspect("equal", "box")

    fig.canvas.draw_idle()
    fig.canvas.flush_events()

display_plot()

mcu = serial.Serial(choice, BAUD_RATE, timeout=1)

try:
    while True:
        if mcu.in_waiting > 0:
            line = mcu.readline()
            if not line:
                continue

            text = line.decode("utf-8", "ignore").strip()
            if not text or not text.startswith("Raw:"):
                continue

            try:
                text = text[4:]

                values = text.split(",")
                if len(values) != 9:
                    continue

                mag_values = values[6:]
                x, y, z = [int(v) for v in mag_values]

                x_data.put((x, y))
                y_data.put((y, z))
                z_data.put((z, x))

                update_plot()
            except ValueError:
                print("Wrong line!")
                continue
except KeyboardInterrupt:
    print("Interrupted, aborting....")
finally:
    mcu.close()