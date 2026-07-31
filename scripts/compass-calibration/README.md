# QMC5883L Compass Calibration

## Steps:

- Install the Python binaries version 3.x.xx
- Install via PIP the following libraries : pyserial, matplotlib (no backend required)
- Compile and flash the compass sketch in Arduino IDE, don't forget to close the "Serial Monitor" as two programs cannot use the same serial port
- Click the "Start" button
- Move your magnetometer in all possible directions, collect raw x,y,z data
- Once you have 3 distinct circles visible, you can click the "Stop" button
- Copy the sensor data extremums printed in the logs, input them in your Arduino sketch, DONE
