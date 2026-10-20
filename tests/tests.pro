# SPDX-FileCopyrightText: 2026 ccontino84
# SPDX-License-Identifier: LGPL-2.1-only

# One test binary per area:
#   core      settings, crash guards, log, controller, D-Bus, loading, update-env
#   gestures  the rules, the scene, the router, one-handed mode, zoom, back,
#             putting back, the controller with the modes
TEMPLATE = subdirs
SUBDIRS = core gestures
