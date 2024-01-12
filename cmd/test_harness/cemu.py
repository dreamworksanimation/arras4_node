# Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
# SPDX-License-Identifier: Apache-2.0

import threading
import atexit

from service import CEMUService
from coord import Coord

coord = Coord()

cemuService = CEMUService(coord)

t = threading.Thread(target=cemuService.run)
t.daemon = True
t.start()

def shutdown():
    print("shutting down all nodes")
    coord.shutdownAll()

atexit.register(shutdown)
