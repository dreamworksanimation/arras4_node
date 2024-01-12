# Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
# SPDX-License-Identifier: Apache-2.0

import sys
from cemu import coord

s = coord.new("test.sd")
gone = False

print("*> start node")
print("*> start()")

def start():
    s.alloc()
    s.launch()
    print("*> when ready, go()")

def go():
    global gone
    s.go()
    if not gone:
        s.engineReady()
        print("*> start client")
        gone = True
    print("*> resize(size)")

def resize(n):
    s.definition["computations"]["testcomp"]["arrayExpand"] = n
    s.rebuild()
    s.alloc()
    s.modify()
    print("*> when ready, go()")

    
