
# Kincony S3 Core Development Board
The core development board uses the ESP32-S3-WROOM-1U N16R8 chip and compatible with Visual Studio Code and PlatformIO with the ESP-IDF SDK.  The PlatformIO board that was selected is the `esp32s3box` given this board uses the same chip.  The core development board comes with RS485, I2C, 1-wire, and analog input ports.  The analog input ports can use voltage or current signals which are set by jumpers. The board comes with an ethernet port and has support for the SIM7600 4G module and RF 433MHz reciever module.

The demo application interfaces the following sensors:
- RS2E radar precipitation sensor with temperature and humidity sensor
- WDC2E ultrasonic anemometer
- BMP280 atmospheric pressure sensor
- DS18B20 ground temperature sensor

The RS2E and WDC2E sensors are configured as MODBUS RTUs and interfaced to the core development board via the RS-485 port.  The BMP280 sensor is interfaced to the core development board via I2C and the DS18B20 sensor is interfaced to the board via 1-wire.  Information on ESP-MODBUS and component download is available here: https://docs.espressif.com/projects/esp-modbus/en/latest/esp32/.  And drivers for the BMP280 and DS18B20 are available here: https://github.com/K0I05/esp32-s3.  Before compiling the application be sure to download the ESP-MODBUS component and copy the component to the `components` folder.

The demo application attempts to connect to a WiFi network, once connected, the system clock is synchronized over SNTP and then it will poll all four sensors at a user-defined interval (6-seconds).  As each sensor is polled the application prints the results via the serial debug port.

Serial debug port print example:
```
E (23447) RTU [APP]: ################## KINCONY-S3 ##################
I (23477) RTU [APP]: RS2E Air Temperature:        20.93°C
I (23517) RTU [APP]: RS2E Relative Humidity:      29.30 %
I (23527) RTU [APP]: RS2E Dewpoint Temperature:   2.35°C
I (23567) RTU [APP]: RS2E Precipitation Rate:     0.00 mm/h
I (23607) RTU [APP]: RS2E Precipitation Type:     No Rain
I (23647) RTU [APP]: WDC2E Wind Direction:        0°
I (23687) RTU [APP]: WDC2E Wind Speed:            0.00 m/s
I (23907) RTU [APP]: DS18B20 Ground Temperature:  22.25°C
I (23917) RTU [APP]: BMP280 Atmospheric Pressure: 994.46 hPa
```

Periodic memory probing example:
```
W (27447) RTU [APP]: Free Memory:       250940 bytes (0 bytes Consumed)
W (27447) RTU [APP]: Free Stack Memory: 1016 bytes (heap_size_task)
W (27447) RTU [APP]: Free Stack Memory: 1556 bytes (sample_sensor_task)
```


Copyright (c) 2024 Eric Gionet (gionet.c.eric@gmail.com)