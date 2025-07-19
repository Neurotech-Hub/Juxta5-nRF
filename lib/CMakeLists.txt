# Libraries Directory
#
# Copyright (c) 2024 NeurotechHub
# SPDX-License-Identifier: Apache-2.0

# Add subdirectories for each library
add_subdirectory_ifdef(CONFIG_JUXTA_FRAM juxta_fram)
add_subdirectory_ifdef(CONFIG_JUXTA_FRAMFS juxta_framfs) 