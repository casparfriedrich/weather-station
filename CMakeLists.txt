cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(weather-station)

target_sources(app PRIVATE src/main.c)
# target_sources(app PRIVATE src/easy_mqtt.c)
target_sources(app PRIVATE src/easy_wifi.c)
target_sources(app PRIVATE src/time.c)
