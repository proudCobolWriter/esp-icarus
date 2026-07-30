import serial.tools.list_ports
import threading, time, sys, os

import matplotlib.widgets as widgets
import matplotlib.pyplot as plt
import matplotlib
import numpy as np

from queue import Queue

# CONSTANTS

BAUD_RATE = 115200
MAX_POINTS = 250

# ENDOFCONSTANTS


def scanports(portsdict):
    ports = serial.tools.list_ports.comports()

    for port, desc, hwid in sorted(ports):
        print(f"{port}: {desc} [{hwid}]")
        portsdict[port] = desc

    return portsdict


def printsep():
    print("*" * os.get_terminal_size()[0])


def promptuser(portsdict):
    choice = input("Choose the COM port you want... ").upper()

    if choice.strip() == "":
        choice = list(portsdict.keys())[0]
    else:
        if choice not in portsdict:
            if f"COM{choice}" in portsdict:
                choice = f"COM{choice}"
            elif f"/dev/ttyUSB{choice}" in portsdict:
                choice = f"/dev/ttyUSB{choice}"
            else:
                return None

    return choice


class CompassGUI:
    def __init__(self, mcu):
        self.is_running = False
        self.mcu = mcu

        print("Active MLP backend:", matplotlib.get_backend())

        plt.ion()

        self.fig, self.ax = plt.subplots(figsize=(10, 7))

        plt.subplots_adjust(left=0.15, bottom=0.2, right=0.85)  # room for the buttons

        self.xy_data = Queue(maxsize=MAX_POINTS)
        self.yz_data = Queue(maxsize=MAX_POINTS)
        self.zx_data = Queue(maxsize=MAX_POINTS)

        (self.x_cloud,) = self.ax.plot(
            [], [], "o", markersize=5, label="XY plane", alpha=1.0
        )
        (self.y_cloud,) = self.ax.plot(
            [], [], "o", markersize=5, label="YZ plane", alpha=1.0
        )
        (self.z_cloud,) = self.ax.plot(
            [], [], "o", markersize=5, label="ZX plane", alpha=1.0
        )

        self.ax.set_title("QMC5883L Calibration")
        self.ax.legend()

        pos = self.ax.get_position()

        self.constants = {"btn_width": 0.12, "btn_height": 0.05, "btn_y": 0.05}

        pos_left = pos.x0
        pos_right = pos.x1 - self.constants["btn_width"]
        pos_mid = (pos_left + pos_right) / 2

        pos_left = pos_mid - self.constants["btn_width"] * 1.25
        pos_right = pos_mid + self.constants["btn_width"] * 1.25

        ax_start = self.fig.add_axes(
            (
                pos_left,
                self.constants["btn_y"],
                self.constants["btn_width"],
                self.constants["btn_height"],
            )
        )
        ax_reset = self.fig.add_axes(
            (
                pos_right,
                self.constants["btn_y"],
                self.constants["btn_width"],
                self.constants["btn_height"],
            )
        )
        ax_stop = self.fig.add_axes(
            (
                pos_mid,
                self.constants["btn_y"],
                self.constants["btn_width"],
                self.constants["btn_height"],
            )
        )

        self.btn_start = widgets.Button(ax_start, "Start")
        self.btn_reset = widgets.Button(ax_reset, "Reset")
        self.btn_stop = widgets.Button(ax_stop, "Stop")

        self.btn_start.on_clicked(self.start)
        self.btn_reset.on_clicked(self.reset)
        self.btn_stop.on_clicked(self.stop)

        self.points_text = self.fig.text(
            (0.75 + 1) / 2,
            0.525,
            f"Points:\n0/{MAX_POINTS}",
            fontsize=10,
            horizontalalignment="center",
            verticalalignment="center",
            family="monospace",
        )

        self.update()

    def start(self, _):
        if self.is_running:
            return

        self.is_running = True
        print("Started")

        self.thread = threading.Thread(target=self.listen_serial, daemon=True)
        self.thread.start()

    def stop(self, _):
        if not self.is_running:
            return

        self.is_running = False
        print("Stopped")

    def reset(self, _):
        print("Resetting points")

        for q in [self.xy_data, self.yz_data, self.zx_data]:
            with q.mutex:
                q.queue.clear()
                q.all_tasks_done.notify_all()
                q.not_full.notify_all()

        self.update()

    def update(self):
        self.x_cloud.set_data(
            [coord[0] for coord in self.xy_data.queue],
            [coord[1] for coord in self.xy_data.queue],
        )
        self.y_cloud.set_data(
            [coord[0] for coord in self.yz_data.queue],
            [coord[1] for coord in self.yz_data.queue],
        )
        self.z_cloud.set_data(
            [coord[0] for coord in self.zx_data.queue],
            [coord[1] for coord in self.zx_data.queue],
        )

        self.ax.relim()
        self.ax.autoscale_view()
        self.ax.set_aspect("equal", "box")

        self.fig.canvas.draw_idle()
        self.fig.canvas.flush_events()

        self.points_text.set_text(f"Points:\n{self.xy_data.qsize()}/{MAX_POINTS}")

    def listen_serial(self):
        try:
            while self.is_running:
                if self.mcu.in_waiting > 0:
                    line = self.mcu.readline()
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

                        if hasattr(self, "xCenter"):
                            x -= self.xCenter
                            y -= self.yCenter
                            z -= self.zCenter

                        if self.xy_data.full():
                            self.xy_data.get()
                            self.yz_data.get()
                            self.zx_data.get()

                        self.xy_data.put((x, y))
                        self.yz_data.put((y, z))
                        self.zx_data.put((z, x))

                        self.update()
                    except ValueError:
                        print("Wrong line!")
                        continue
        except serial.SerialException:
            print("Serial bridge was broken!")
        finally:
            print("Worker thread stopped")
            self.done()

    def queueBounds(self, q):
        if len(q.queue) == 0:
            return 0.0, 0.0
        values = [v[0] for v in q.queue]
        return min(values), max(values)

    def requeueWOffset(self, q, offset1, offset2):
        coords_pool = []

        while q.empty():
            coords_pool.append(q.get())

        for coord in coords_pool:
            c1, c2 = coord
            q.put((c1 - offset1, c2 - offset2))

    def done(self):
        xmin, xmax = self.queueBounds(self.xy_data)
        ymin, ymax = self.queueBounds(self.yz_data)
        zmin, zmax = self.queueBounds(self.zx_data)

        printsep()
        print("Calibration results:")
        print(
            f"X enveloppe: {xmin} - {xmax} | Y enveloppe: {ymin} - {ymax} | Z enveloppe: {zmin} - {zmax}"
        )
        print(f"Raw:{xmin},{xmax},{ymin},{ymax},{zmin},{zmax}")
        printsep()

        self.xCenter, self.yCenter, self.zCenter = (
            (xmax + xmin) / 2,
            (ymax + ymin) / 2,
            (zmax + zmin) / 2,
        )

        ax_calib = self.fig.add_axes(
            (
                self.ax.get_position().x1 + self.constants["btn_width"],
                self.constants["btn_y"],
                self.constants["btn_width"],
                self.constants["btn_height"],
            )
        )

        self.btn_calib = widgets.Button(ax_calib, "Calibrate")
        self.btn_calib.on_clicked(self.recenter)

        self.fig.canvas.draw_idle()

    def recenter(self, _):
        if not self.xCenter or self.thread.is_alive():
            return

        self.requeueWOffset(self.xy_data, self.xCenter, self.yCenter)
        self.requeueWOffset(self.yz_data, self.yCenter, self.zCenter)
        self.requeueWOffset(self.zx_data, self.zCenter, self.xCenter)

        print("Recalculated point positions")

        self.update()


def main():
    choices = {}

    scanports(choices)
    printsep()

    if len(choices) == 0:
        print("No COM ports were found, aborting...")
        sys.exit(0)

    choice = promptuser(choices)

    if choice:
        print(f"Port {choice} was chosen")
    else:
        print("That COM port was not found, aborting...")
        sys.exit(0)

    mcu = serial.Serial(choice, BAUD_RATE, timeout=1)
    compass = CompassGUI(mcu)

    try:
        plt.show(block=True)
    except KeyboardInterrupt:
        print("Interrupted, aborting...")
    finally:
        compass.is_running = False
        if mcu.is_open:
            mcu.close()


if __name__ == "__main__":
    main()
