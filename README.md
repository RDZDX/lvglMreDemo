# Port and demo of [LVGL](https://github.com/lvgl/lvgl) for MRE platform


Map tile setup (for you to do manually)
The app will look for tiles on the MRE filesystem at:

Code\
e:/osm/gdansk/z/x/y.bin\
e:/osm/world/z/x/y.bin\
e:/osm/images/empty.bin


Key	Action\
UP / NUM2	Zoom in\
DOWN / NUM8	Zoom out\
LEFT / NUM4	Pan west\
RIGHT / NUM6	Pan east\
NUM1	Pan north\
NUM7	Pan south\
OK / Left softkey	Resume auto-animation\
On first key press → pauses the auto-timer, switches to manual control. Pan step scales with zoom level (larger steps when zoomed out, finer when zoomed in).


## Credits

- esp32_offline_osm - [mryndzionek](https://github.com/mryndzionek/esp32_offline_osm)
- lvglMreDemo - [XimikBoda](https://github.com/XimikBoda/lvglMreDemo)
- CMake MRE template — [XimikBoda](https://github.com/XimikBoda/CmakeMreTemplate)
- TinyMRESDK — [XimikBoda](https://github.com/XimikBoda/TinyMRESDK)
