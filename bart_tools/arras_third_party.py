# Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
# SPDX-License-Identifier: Apache-2.0

'''
Third party installs for libee
'''
from os import path
import glob

def tbb(env):
    # Variables defining the location of the build of 3rd party library to use.
    root_dir = '/rel/third_party/intelcompiler64/composer_xe'
    intel_name = 'tbb'
    tbb_4_1_3 = 'composer_xe_2013.4.183'
    tbb_3_0_8 = 'composerxe-2011.5.220'
    intel_major = 2

    name_libs_tbb = ['tbb', 'tbbmalloc', 'tbbmalloc_proxy']
    name_lib_tbb_mallocs = ['tbbmalloc' 'tbbmalloc_proxy']

    for lib in set(name_libs_tbb):
        if lib in name_lib_tbb_mallocs:
            tbb_version = tbb_3_0_8
            tbb_lib_dir = 'lib/intel64/cc4.1.0_libc2.4_kernel2.6.16.21'
        else:
            tbb_version = tbb_4_1_3
            tbb_lib_dir = 'lib/intel64/gcc4.1'


        # Copy library into the INSTALL_DIR.
        if env['COMPILER_LABEL'].endswith('mic'):
            env.DWAInstallSDKFile(path.join(root_dir, tbb_4_1_3, intel_name, 'lib/mic', 'lib%s.so.%d' % (lib, intel_major)),
                                    'lib/lib%s${SHLIBSUFFIX}.%d' % (lib, intel_major),
                                    copy=True)
        else:
            env.DWAInstallSDKFile(path.join(root_dir, tbb_version, intel_name, tbb_lib_dir, 'lib%s.so.%d' % (lib, intel_major)),
                                    'lib/lib%s${SHLIBSUFFIX}.%d' % (lib, intel_major),
                                    copy=True)

def jsoncpp(env):
    # Variables defining the location of the build of 3rd party library to use.
    name = 'jsoncpp'
    lib_name = "json"
    version = '0.5'
    patch = 0
    root_dir = '/rel/third_party'
    if env['COMPILER_LABEL'].endswith('mic'):
        # jsoncpp 0.5.0 was built for icc140_mic(icc_mic) and icc150_mic
        if env['COMPILER_LABEL'] == 'icc_mic':
            build_variant = 'icc140_mic'
        else:
            build_variant = env['COMPILER_LABEL']
    else:
        build_variant = 'linux-gcc-4.1.2'

    # Derived location variables.
    base_dir = path.join(root_dir, '%s/%s.%d'% (name, version, patch))
    explicit_name = '%s-%s.%d' % (name, version, patch)
    include_dir = path.join(base_dir, 'include')
    lib_dir = path.join(base_dir, 'libs', build_variant)
    full_lib_name = '%s_%s_libmt' % (lib_name, build_variant)

    # Copy library into the INSTALL_DIR
    env.DWAInstallSDKFile(path.join(lib_dir, 'lib%s.so' % full_lib_name ), 
                        'lib/lib%s${SHLIBSUFFIX}' % lib_name,
                        copy=True)
        

def mkl(env):
    # Variables defining the location of the build of 3rd party library to use.
    root_dir = '/rel/third_party/intelcompiler64/composer_xe/composer_xe_2011_sp1.11.339'
    name = 'mkl'
    name_libs_mkl = ['mkl_core', 'mkl_sequential', 'mkl_intel_lp64']
    name_mt_libs_mkl = ['mkl_core', 'mkl_intel_thread', 'mkl_intel_lp64']
    #/rel/third_party/intelcompiler64/composer_xe/composer_xe_2011_sp1.11.339/mkl/lib/intel64/libmkl_avx.so
    #/rel/third_party/intelcompiler64/composer_xe/composer_xe_2011_sp1.11.339/mkl/lib/intel64/libmkl_def.so
    name_libs_mkl_extra_libee = Split("mkl_avx mkl_def")
    version = '10.3'
    patch = 11
    build = 0

    # Derived location variables.
    base_dir = path.join(root_dir, name)
    lib_dir = path.join(base_dir, 'lib/intel64')

    # Link library into the INSTALL_DIR.
    for lib in set(name_libs_mkl + name_mt_libs_mkl + name_libs_mkl_extra_libee):
        env.DWAInstallSDKFile(path.join(lib_dir, 'lib%s.so' % lib),
                                   'lib/lib%s$SHLIBSUFFIX' % lib,
                                   copy=True)

def boost(env):
    lib_dir = '/rel/depot/third_party_build/boost/rhel6-1.54.0-2/lib'
    boost_libs = [path.basename(lib) for lib in 
                    glob.glob(lib_dir + '/*gcc48-mt*') if 'python' not in lib]
    for lib in boost_libs:
        env.DWAInstallSDKFile(path.join(lib_dir, lib), 'lib/%s' % lib, copy=True)

def icclibs(env):
    lib_dir = '/rel/third_party/intelcompiler64/composer_xe/composer_xe_2013_sp1.3.174/compiler/lib/intel64'
    compiler_libs = ['libintlc.so.5', 'libsvml.so', 'libimf.so', 'libirng.so', 'libiomp5.so']
    for lib in compiler_libs:
        env.DWAInstallSDKFile(path.join(lib_dir, lib), 'lib/%s' % lib, copy=True)

def google(env):
    lib_dir = '/rel/third_party/google/protobuf/2.6.1/lib'
    google_libs = ['libprotobuf.so.9']
    # Optionally use the 3.0.0 version
    #lib_dir = '/rel/third_party/google/protobuf/3.0.0b2/lib'
    #google_libs = ['libprotobuf.so.10']
    for lib in google_libs:
        env.DWAInstallSDKFile(path.join(lib_dir, lib), 'lib/%s' % lib, copy=True)

def log4cplus(env):
    if env['COMPILER_LABEL'].endswith('mic'):
        lib_dir = '/rel/depot/third_party_build/log4cplus/1.0.3-1/opt-ws5-x86_64-icc_mic/lib'
    else:
        lib_dir = '/rel/depot/third_party_build/log4cplus/1.0.3-1/opt-ws5-x86_64-gccWS5_64/lib'
    log_libs = ['liblog4cplus-1.0.so.3']
    for lib in log_libs:
        env.DWAInstallSDKFile(path.join(lib_dir, lib), 'lib/%s' % lib, copy=True)

def ilmbase(env):
    lib_dir = '/rel/depot/third_party_build/ilmbase/1.0.3-1/opt-ws5-x86_64-gccWS5_64/lib'
    ilm_libs = glob.glob(lib_dir + '/*.so.7')
    for lib in ilm_libs:
        env.DWAInstallSDKFile(lib, 'lib/%s' % path.basename(lib), copy=True)

def opencv(env):
    lib_dir = '/work/rd/arras/third_party/opencv/2.4.6.2/install/linux/20131023/lib'
    opencv_libs = glob.glob(lib_dir + '/*.so.2.4')
    for lib in opencv_libs:
        env.DWAInstallSDKFile(lib, 'lib/%s' % path.basename(lib), copy=True)

def ffmpeg(env):
    lib_dir = '/work/rd/arras/third_party/ffmpeg/install/lib'
    ff_libs = [lib for lib in glob.glob(lib_dir + '/*') if lib.endswith('so.55') 
                                                        or lib.endswith('so.52')
                                                        or lib.endswith('so.2')]
    for lib in ff_libs:
        env.DWAInstallSDKFile(lib, 'lib/%s' % path.basename(lib), copy=True)
        
def vpx(env):
    lib_dir = '/work/rd/arras/third_party/vpx/lib'
    vpx_libs = glob.glob(lib_dir = "/*.so.3")
    for lib in vpx_libs:
        env.DWAInstallSDKFile(lib, 'lib/%s' % path.basename(lib), copy=True)
    
def linux64(env):
    lib_dir = '/work/rd/arras/third_party/CoreAVC/Linux/linux_x64'
    linux_libs = ['libcoreavc_sdk.so']
    for lib in linux_libs:
        env.DWAInstallSDKFile(path.join(lib_dir, lib), 'lib/%s' % lib, copy=True)

def qt(env):
    lib_dir = '/rel/depot/third_party_build/qt/4.8.2-0/opt-ws6-x86_64-icc150_64/lib'
    qt_libs = [lib for lib in glob.glob(lib_dir + '/*') if lib.endswith('so.4')]
    for lib in qt_libs:
        env.DWAInstallSDKFile(lib, 'lib/%s' % path.basename(lib), copy=True)

def gts(env):
    lib_dir = '/rel/depot/third_party_build/gts/0.7.6-1/opt-ws5-x86_64-gccWS5_64/lib'
    gts_libs = ['libgts-0.7.so.5']
    for lib in gts_libs:
        env.DWAInstallSDKFile(path.join(lib_dir,lib), 'lib/%s' % lib, copy=True)

def fbx(env):
    lib_dir = '/rel/third_party/fbx/2012.2c/lib/gcc4/x64'
    fbx_libs = ['libfbxsdk-2012.2.so']
    for lib in fbx_libs:
        env.DWAInstallSDKFile(path.join(lib_dir,lib), 'lib/%s' % lib, copy=True)

def svm(env):
    lib_dir = '/rel/third_party/libsvm/libsvm-3.18/lib'
    svm_libs = ['libsvm.so']
    for lib in svm_libs:
        env.DWAInstallSDKFile(path.join(lib_dir,lib), 'lib/%s' % lib, copy=True)

def embree(env):
    lib_dir = '/rel/folio/embree_arras/embree_arras-2.3.3.x.0.0.1-1/lib'
    embree_libs = ['libembree.so']
    for lib in embree_libs:
        env.DWAInstallSDKFile(path.join(lib_dir,lib), 'lib/%s' % lib, copy=True)

def oiio(env):
    lib_dir = '/rel/folio/openimageio_arras/openimageio_arras-1.4.13.x.0.0.1-2/lib'
    oiio_libs = ['libOpenImageIO.so']
    for lib in oiio_libs:
        env.DWAInstallSDKFile(path.join(lib_dir,lib), 'lib/%s' % lib, copy=True)

def openexr(env):
    lib_dir = '/rel/folio/openexr/openexr-2.1.0.d.w.a.1.2-5/lib'
    openexr_libs = ['libImath-Imf_DWA_2_1.so.11', 'libIlmImf-Imf_DWA_2_1.so.21', 'libHalf.so.11', 'libIlmThread-Imf_DWA_2_1.so.11', 'libIexMath-Imf_DWA_2_1.so.11', 'libIex-Imf_DWA_2_1.so.11']
    for lib in openexr_libs:
        env.DWAInstallSDKFile(path.join(lib_dir,lib), 'lib/%s' % lib, copy=True)
