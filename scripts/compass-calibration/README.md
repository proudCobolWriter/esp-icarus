# QMC5883L Compass Calibration

This script is meant to find with a neat graphical interface the hard-iron offsets, basically finding the centroids of the clouds of points and centering them in the middle with the others.
If you want to find the soft-iron calibration matrix of dim(M)=3, refer to the [MotionCal](http://www.pjrc.com/teensy/beta/imuread/MotionCal.exe) software as shown in [Figure 4](./examples/Figure_4.png).

## Steps:

- Install the Python binaries version 3.x.xx
- Install via PIP the following libraries : pyserial, matplotlib (no backend required)
- Compile and flash the compass sketch in Arduino IDE, don't forget to close the "Serial Monitor" as two programs cannot use the same serial port
- Click the "Start" button
- Move your magnetometer in all possible directions, collect raw x,y,z data
- Once you have 3 distinct circles visible, you can click the "Stop" button
- Copy the sensor data extremums printed in the logs, input them in your Arduino sketch, DONE

## Images:

<img src="./examples/Figure_2.png>" />
<img src="./examples/Figure_3.png>" />
