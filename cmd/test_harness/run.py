# Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
# SPDX-License-Identifier: Apache-2.0

import sys
from cemu import coord

sessionFile = sys.argv[1]
nodeId = sys.argv[2]
httpPort = int(sys.argv[3])
tcpPort = int(sys.argv[4])

ndata = {'id':nodeId, 'hostname':'localhost',
         'httpPort':httpPort, 'port':tcpPort}
s = coord.new(sessionFile)
n = coord.addNode(ndata)
s.alloc()

