Upon Download
1. Rename the folder containing mictest.ino as "mictest".

Arduino Setting
 
1. Go to Arduino IDE's File/Preferences menu. Enter the following Additional boards manager URLs: 

https://dl.espressif.com/dl/package_esp32_index.json

2. Go to Tools/Board/Boards Manager menu. Search for ESP32. Install board support package for ESP32 from Espressif Systems

3. Go to Tools/Board/esp32 and select ESP32 Wrover Module

4. Go to Tools/Port and select the COM port that ESP32 is connected to

5. Open mictest.ino from File/Open

6. Click on Upload button to compile, upload and run.

7. Once running, select Tools/Serial Plotter. 

8. There should be a waveform on the Serial Plotter whenever there is a loud noise (e.g. knock on table). 

First commit

