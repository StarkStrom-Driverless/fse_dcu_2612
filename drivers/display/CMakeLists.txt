# SPDX-License-Identifier: Apache-2.0

zephyr_library_named(fse_display_drivers)
zephyr_library_sources_ifdef(CONFIG_HX8357 display_hx8357.c)
zephyr_include_directories(.)
