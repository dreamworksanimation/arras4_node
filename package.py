# Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
# SPDX-License-Identifier: Apache-2.0

# -*- coding: utf-8 -*-
import sys

name = 'arras4_node'

@early()
def version():
    """
    Increment the build in the version.
    """
    _version = '4.7.1'
    from rezbuild import earlybind
    return earlybind.version(this, _version)

description = 'Arras package'

authors = ['Dreamworks Animation R&D - JoSE Team',
           'psw-jose@dreamworks.com']

help = ('For assistance, '
        "please contact the folio's owner at: psw-jose@dreamworks.com")

if 'scons' in sys.argv:
    build_system = 'scons'
    build_system_pbr = 'bart_scons-10'
else:
    build_system = 'cmake'
    build_system_pbr = 'cmake_modules'

variants = [
    ['os-CentOS-7', 'refplat-vfx2021.0'],
    ['os-CentOS-7', 'refplat-vfx2022.0'],
    ['os-rocky-9', 'refplat-vfx2021.0'],
    ['os-rocky-9', 'refplat-vfx2022.0'],
]

sconsTargets = {}
for variant in variants:
    sconsTargets[variant[0]] = ['@install']

requires = [
    'arras4_core-4.10'
]

private_build_requires = [
    build_system_pbr,
    'gcc-9.3.x'
]

def commands():
    prependenv('PATH', '{root}/bin:{root}/sbin')
    prependenv('LD_LIBRARY_PATH', '{root}/lib')


uuid = '2798ce16-36b2-46a7-80ac-7d90994706d'

config_version = 0
