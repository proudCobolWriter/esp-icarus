import serial.tools.list_ports
import threading, random, time, sys, os

import matplotlib.widgets as widgets
import matplotlib.pyplot as plt
import matplotlib
import numpy as np

from queue import Queue

# CONSTANTS

BAUD_RATE = 115200
MAX_POINTS = 1000
REFRESH_RATE = 10  # how many times a second the GUI updates (=/= sensor polling rate)

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

        self.xCenter, self.yCenter, self.zCenter = 0, 0, 0

        self.should_finish = False
        self.update_required = True
        self.update()

        interval = int(1 / REFRESH_RATE * 1000)

        self.timer = self.fig.canvas.new_timer(interval)
        self.timer.add_callback(self.update)
        self.timer.start()

    def start(self, _):
        if self.is_running:
            return

        self.set_btn_calib(False)  # remove that calib button

        if self.mcu.is_open:
            self.mcu.reset_input_buffer()

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

        self.xCenter, self.yCenter, self.zCenter = 0, 0, 0

        self.update_required = True

    def update(self):
        if getattr(self, "should_finish"):
            self.should_finish = False
            self.done()
            return

        if not self.update_required:
            return

        with self.xy_data.mutex, self.yz_data.mutex, self.zx_data.mutex:
            xy_snapshot = list(self.xy_data.queue)
            yz_snapshot = list(self.yz_data.queue)
            zx_snapshot = list(self.zx_data.queue)

        self.x_cloud.set_data([c[0] for c in xy_snapshot], [c[1] for c in xy_snapshot])
        self.y_cloud.set_data([c[0] for c in yz_snapshot], [c[1] for c in yz_snapshot])
        self.z_cloud.set_data([c[0] for c in zx_snapshot], [c[1] for c in zx_snapshot])

        self.ax.relim()
        self.ax.autoscale_view()
        self.ax.set_aspect("equal", "box")

        self.fig.canvas.draw_idle()

        self.points_text.set_text(f"Points:\n{self.xy_data.qsize()}/{MAX_POINTS}")

        self.update_required = False

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

                        x -= self.xCenter
                        y -= self.yCenter
                        z -= self.zCenter

                        if self.xy_data.full():
                            with self.xy_data.mutex, self.yz_data.mutex, self.zx_data.mutex:
                                if len(self.xy_data.queue) > 0:
                                    self.xy_data.queue.popleft()
                                if len(self.yz_data.queue) > 0:
                                    self.yz_data.queue.popleft()
                                if len(self.zx_data.queue) > 0:
                                    self.zx_data.queue.popleft()

                        self.xy_data.put((x, y))
                        self.yz_data.put((y, z))
                        self.zx_data.put((z, x))

                        self.update_required = True
                    except ValueError:
                        print("Wrong line!")
                        continue
        except serial.SerialException:
            print("Serial bridge was broken!")
        finally:
            print("Worker thread stopped")
            self.should_finish = True
            self.update_required = False

    def queueBounds(self, q):
        with q.mutex:
            if len(q.queue) == 0:
                return 0.0, 0.0
            values = [v[0] for v in q.queue]
            return min(values), max(values)

    def requeueWOffset(self, q, offset1, offset2):
        with q.mutex:
            coords_pool = list(q.queue)
            q.queue.clear()

            for c1, c2 in coords_pool:
                q.queue.append((c1 - offset1, c2 - offset2))

    def computeAllBounds(self):
        self.xmin, self.xmax = self.queueBounds(self.xy_data)
        self.ymin, self.ymax = self.queueBounds(self.yz_data)
        self.zmin, self.zmax = self.queueBounds(self.zx_data)

    def computeEnveloppeCentroid(self):
        # we add to the current center to keep track of the offset, because the sensor itself, is not calibrated
        offsetX, offsetY, offsetZ = (
            (self.xmax + self.xmin) / 2,
            (self.ymax + self.ymin) / 2,
            (self.zmax + self.zmin) / 2,
        )
        self.xCenter += offsetX
        self.yCenter += offsetY
        self.zCenter += offsetZ
        return (offsetX, offsetY, offsetZ)

    def done(self):
        self.computeAllBounds()

        printsep()
        print("Calibration results:")
        print(
            f"X enveloppe: {self.xmin} - {self.xmax} | Y enveloppe: {self.ymin} - {self.ymax} | Z enveloppe: {self.zmin} - {self.zmax}"
        )
        print(
            f"Raw:{self.xmin},{self.xmax},{self.ymin},{self.ymax},{self.zmin},{self.zmax}"
        )
        printsep()

        self.set_btn_calib()  # creates the calib button

    def calibrate(self, _):
        if self.thread.is_alive():
            return

        offsetX, offsetY, offsetZ = self.computeEnveloppeCentroid()

        self.requeueWOffset(self.xy_data, offsetX, offsetY)
        self.requeueWOffset(self.yz_data, offsetY, offsetZ)
        self.requeueWOffset(self.zx_data, offsetZ, offsetX)

        self.computeAllBounds()
        self.computeEnveloppeCentroid()

        self.update_required = True
        print("Recalculated point positions")

    def set_btn_calib(self, active=True):
        if active == True:
            if hasattr(self, "btn_calib"):
                return

            pos = self.ax.get_position()
            pos_right = pos.x1

            ax_calib = self.fig.add_axes(
                (
                    pos_right + self.constants["btn_width"],
                    self.constants["btn_y"],
                    self.constants["btn_width"],
                    self.constants["btn_height"],
                )
            )

            self.btn_calib = widgets.Button(ax_calib, "Calibrate")
            self.btn_calib.on_clicked(self.calibrate)
        else:
            if not hasattr(self, "btn_calib"):
                return

            self.btn_calib.ax.remove()
            del self.btn_calib

        self.fig.canvas.draw_idle()


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
