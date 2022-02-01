#
# Copyright (c) 2021 SAKURA internet Inc.
#
# SPDX-License-Identifier: MIT
#

zephyr_include_directories(include)

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_SIPF
	src/sipf_client_http.c
	src/sipf_auth.c
    src/sipf_object.c
	src/sipf_file.c
)
