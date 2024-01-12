# Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
# SPDX-License-Identifier: Apache-2.0

def update_test2(s):
    c = s.definition["computations"]
    # disconnect client -> tb
    del c["tb"]["messages"]["(client)"]
    # disconnect ta -> taa
    del c["taa"]["messages"]["ta"]
    # connect ta -> tbb
    c["tbb"]["messages"]["ta"] = "*"
    s.rebuild()
    
