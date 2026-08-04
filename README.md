# Port and demo of [LVGL](https://github.com/lvgl/lvgl) for MRE platform


Map tile setup (for you to do manually)
The app will look for tiles on the MRE filesystem at:

Map data:\
e:/osm/world/z/x/y.bin\
e:/osm/images/empty.bin


Key	Action\
LEFT_SOFT_KEY Zoom in\
RIGHT_SOFT_KEY Zoom out\
LEFT Pan west\
RIGHT Pan east\
UP Pan north\
DOWN Pan south\
OK center\

## Credits

- esp32_offline_osm - [mryndzionek](https://github.com/mryndzionek/esp32_offline_osm)
- lvglMreDemo - [XimikBoda](https://github.com/XimikBoda/lvglMreDemo)
- CMake MRE template — [XimikBoda](https://github.com/XimikBoda/CmakeMreTemplate)
- TinyMRESDK — [XimikBoda](https://github.com/XimikBoda/TinyMRESDK)
