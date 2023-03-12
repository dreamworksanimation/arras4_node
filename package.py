# Copyright 2023 DreamWorks Animation LLC and Intel Corporation
# SPDX-License-Identifier: Apache-2.0

# -*- coding: utf-8 -*-
import sys

name = 'arras4_node'

@early()
def version():
    """
    Increment the build in the version.
    """
    _version = '4.7.0'
    from rezbuild import earlybind
    return earlybind.version(this, _version)

description = 'Arras package'

authors = ['Dreamworks Animation R&D - JoSE Team',
           'psw-jose@dreamworks.com']

help = ('For assistance, '
        "please contact the folio's owner at: psw-jose@dreamworks.com")

if 'cmake' in sys.argv:
    build_system = 'cmake'
    build_system_pbr = 'cmake_modules'
else:
    build_system = 'scons'
    build_system_pbr = 'bart_scons-10'

variants = [
    ['refplat-vfx2021.0']
]

sconsTargets = {}
for variant in variants:
    sconsTargets[variant[0]] = ['@install']

requires = [
    'arras4_core-4.10'
]

private_build_requires = [
    build_system_pbr,
    'gcc'
]

def commands():
    prependenv('PATH', '{root}/bin:{root}/sbin')


uuid = '2798ce16-36b2-46a7-80ac-7d90994706d'

config_version = 0
