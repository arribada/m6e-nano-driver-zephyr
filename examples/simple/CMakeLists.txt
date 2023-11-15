cmake_minimum_required(VERSION 3.13.1)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
set(ZEPHYR_MODULES ../../drivers/m6e_nano)

project(simple LANGUAGES C)

target_sources(simple PRIVATE src/main.c)
add_subdirectory_ifdef(CONFIG_M6E_NANO ../../driver/m6e_nano m6e-nano)
