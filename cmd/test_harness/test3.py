# Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
# SPDX-License-Identifier: Apache-2.0

import sys
from cemu import coord

s = coord.new("test3.sd")
gone = False

print("*> start node")
print("*> start()")

def go():
    global gone
    s.go()
    if not gone:
        s.engineReady()
        print("*> start client")
        gone = True
    print("*> resize(size)")

def goWhenReady():
    s.waitForReady()
    go()

def start():
    s.alloc()
    s.launch()
    goWhenReady()

def resize(n):
    s.definition["computations"]["testcomp"]["arrayExpand"] = n
    s.rebuild()
    s.alloc()
    s.modify()
    goWhenReady()

    
