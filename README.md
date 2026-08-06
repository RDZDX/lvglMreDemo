# Port and demo of [LVGL](https://github.com/lvgl/lvgl) for MRE platform - offline map viewer


Map tile setup (for you to do manually)
The app will look for tiles on the MRE filesystem at:

Map data:\
e:/osm/world/z/x/y.bin - World map data\
e:/osm/custom/z/x/y.bin - custom place map data with deeper zoom layers\
e:/osm/images/empty.bin

Key	Action\
LEFT_SOFT_KEY Zoom in\
RIGHT_SOFT_KEY Zoom out\
LEFT Pan west\
RIGHT Pan east\
UP Pan north\
DOWN Pan south\
OK center\

## File

- [lvglMreDemo.vxp](https://rdzdx.github.io/lvglMreDemo/lvglMreDemo.vxp)

![alt text](https://rdzdx.github.io/lvglMreDemo/picture.jpg)

## Credits

- esp32_offline_osm - [mryndzionek](https://github.com/mryndzionek/esp32_offline_osm)
- lvglMreDemo - [XimikBoda](https://github.com/XimikBoda/lvglMreDemo)
- CMake MRE template — [XimikBoda](https://github.com/XimikBoda/CmakeMreTemplate)
- TinyMRESDK — [XimikBoda](https://github.com/XimikBoda/TinyMRESDK)
