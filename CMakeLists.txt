cmake_minimum_required (VERSION 3.8)

project ("lvglMreDemo")

set(APP_NAME "lvglMreDemo")
set(DEVELOPER_NAME "XimikBoda")
set(RAM "1500" CACHE STRING "Ram requirement (in kb)")
set(IMSI "123456789" CACHE STRING "Imsi of first SIM-card")
set(API "File SIM_card ProMng" CACHE STRING "Api for app")
set(APPID "0") #App id, special values: -1 is personal (imsi), 0 is in developing
set(CERTID "1" CACHE STRING "") #Cert id use from you cert, 1 is personal (imsi)
set(CERT "none" CACHE STRING "")
option(BACKGROUND "App can work background" OFF)

set(MRE_SDK "$ENV{MRE_SDK}" CACHE STRING "MRE SDK dir")
set(TinyMRESDK "$ENV{TinyMRESDK}" CACHE STRING "TinyMRESDK dir")

if(BACKGROUND)
    add_definitions(-DSUPPORT_BG)
endif()

add_subdirectory(resourses)
add_subdirectory(core)
add_subdirectory(mreapi)
add_subdirectory(vxp)
add_subdirectory(run)

add_definitions(-DLV_CONF_INCLUDE_SIMPLE)
include_directories(./include)
set(BUILD_SHARED_LIBS FALSE)
add_subdirectory(lvgl)

add_subdirectory(lv_port_mre)

add_subdirectory(main)
