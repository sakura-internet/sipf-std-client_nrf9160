#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

add_subdirectory_ifdef(CONFIG_DOWNLOAD_CLIENT_AUTH download_client)
add_subdirectory_ifdef(CONFIG_FOTA_DOWNLOAD_AUTH fota_download)

