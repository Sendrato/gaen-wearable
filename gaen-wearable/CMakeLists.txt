# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)

#find_package(Zephyr HINTS $ENV{ZEPHYR_BASE})
include($ENV{ZEPHYR_BASE}/cmake/app/boilerplate.cmake NO_POLICY_SCOPE)

project(ct_wearable)

target_sources(app
        PRIVATE
            src/main.c

            src/ct_app_en.c
            src/ct_app_enc.c
            src/ct_settings.c
            src/ct_crypto.c
            src/ct_db.c

            src/tinycrypt/hkdf.c

            src/bluetooth/basa.c
            src/bluetooth/ctsa.c
            src/bluetooth/disa.c

            src/util/reboot.c
            src/util/ui.c
            src/util/battery.c
        )

zephyr_include_directories(src)
zephyr_include_directories(src/bluetooth)
zephyr_include_directories(src/util)
