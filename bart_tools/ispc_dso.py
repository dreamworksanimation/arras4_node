# Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
# SPDX-License-Identifier: Apache-2.0


'''
Methods for creating rdl2 dsos from ispc code

The shader writer is expected to author:
   dsoName.json
   dsoName.ispc
   dsoName.cc

From dsoName.json, we produce the itermediate targets
   attributes.cc - expected to be included by dsoName.cc
   attributesISPC.cc - built as dso source
   attributes.isph - expected to be included by dsoName.ispc
   jit.h - optionally included by dsoName.cc

and then pass this source to build the rdl dso and proxy dso
'''

import dwa_install

import json
import sys
import os.path
from collections import OrderedDict


def IspcKeyType(rdl2Type):
    '''
    From the rdl2::Type, determine the corresponding ISPC key type.
    Not all rdl2 attributes types are supported in ISPC code.
    '''
    dataType = ''
    if rdl2Type == 'Bool':
        dataType += 'BoolAttrKeyISPC'
    elif rdl2Type == 'Int':
        dataType += 'IntAttrKeyISPC'
    elif rdl2Type == 'Float':
        dataType += 'FloatAttrKeyISPC'
    elif rdl2Type == 'Vec2f':
        dataType += 'Float2AttrKeyISPC'
    elif rdl2Type == 'Vec3f' or rdl2Type == 'Rgb':
        dataType += 'Float3AttrKeyISPC'
    elif rdl2Type == 'Vec4f' or rdl2Type == 'Rgba':
        dataType += 'Float4AttrKeyISPC'
    else:
        dataType += 'UNKNOWN_ATTR_TYPE'
    return dataType

def IspcType(rdl2Type):
    '''
    From the rdl2::Type, determine the corresponding ISPC shader type.
    Not all rdl2 attributes types are supported in ISPC shader code.
    '''
    shaderType = ''
    if rdl2Type == 'Bool':
        shaderType += 'bool'
    elif rdl2Type == 'Int':
        shaderType += 'int'
    elif rdl2Type == 'Float':
        shaderType += 'float'
    elif rdl2Type == 'Vec2f':
        shaderType += 'Vec2f'
    elif rdl2Type == 'Vec3f':
        shaderType += 'Vec3f'
    elif rdl2Type == 'Rgb':
        shaderType = 'Color'
    return shaderType

def JitType(rdl2Type):
    '''
    From the rdl2::Type, determine the corresponding Jit Type
    Not all rdl2 attributes types are supported in Jit.
    '''
    jitType = ''
    if rdl2Type == 'Bool':
        jitType += 'shading::Jit::Bool'
    elif rdl2Type == 'Int':
        jitType += 'shading::Jit::Int'
    elif rdl2Type == 'Float':
        jitType += 'shading::Jit::Float'
    elif rdl2Type == 'Vec2f':
        jitType += 'shading::Jit::Vec2f'
    elif rdl2Type == 'Vec3f':
        jitType += 'shading::Jit::Vec3f'
    elif rdl2Type == 'Rgb':
        jitType = 'shading::Jit::Color'
    return jitType

def DualType(fType):
    '''
    Given a float type (Color, Vec3f, float, etc...)
    return the corresponding Dual type (ColorDual, Vec3Dual, etc...)
    '''
    dualType = ''
    if fType == 'float':
        dualType = 'Dual'
    elif fType == 'Color':
        dualType = 'ColorDual'
    elif fType == 'Vec2f':
        dualType = 'Vec2Dual'
    elif fType == 'Vec3f':
        dualType = 'Vec3Dual'
    
    return dualType

def shaderTypeConv(shaderType):
    if shaderType == 'Material':
        return 'Shadable'
    else:
        return shaderType

def cap(name):
    return name[0].upper() + name[1:]

def getAttrFn(name):
    return 'get' + cap(name)

def evalAttrFn(name):
    return 'eval' + cap(name)

def dEvalAttrFn(name):
    return 'dEval' + cap(name)

def evalAttrBoundFn(name):
    return 'eval' + cap(name) + 'Bound'

def dEvalAttrBoundFn(name):
    return 'dEval' + cap(name) + 'Bound'

def evalAttrUnBoundFn(name):
    return 'eval' + cap(name) + 'UnBound'

def dEvalAttrUnBoundFn(name):
    return 'dEval' + cap(name) + 'UnBound'

def evalCompFn(name):
    return 'evalComp' + cap(name)

def dEvalCompFn(name):
    return 'dEvalComp' + cap(name)

def evalNormalFn(name):
    return 'evalNormal' + cap(name)

def dEvalNormalFn(name):
    return 'dEvalNormal' + cap(name)

def evalNormalBoundFn(name):
    return 'evalNormal' + cap(name) + 'Bound'

def dEvalNormalBoundFn(name):
    return 'dEvalNormal' + cap(name) + 'Bound'

def evalNormalUnBoundFn(name):
    return 'evalNormal' + cap(name) + 'UnBound'

def dEvalNormalUnBoundFn(name):
    return 'dEvalNormal' + cap(name) + 'UnBound'

def jitEvalMapFn(attrName):
    return 'evalMap' + cap(attrName)

def jitDEvalMapFn(attrName):
    return 'dEvalMap' + cap(attrName)

def defineGetAttrFn(shaderType, retType, attrName):
    text = ('inline uniform ' + retType +
            '\n' + getAttrFn(attrName) + 
            '(const uniform ' + shaderType + ' * uniform obj) {\n' +
            '    return ' + getAttrFn(retType) + '(obj, ' + attrName + ');\n}\n')
    return text

def jitDeclareGetAttrFn(shaderType, retType, attrName):
    text = ('uniform ' + retType +
            ' ' + getAttrFn(attrName) +
            '(const uniform ' + shaderType + ' * uniform obj);\n')
    return text

def jitDeclareEvalMapFn(shaderType, attrName):
    # evalMapAttr
    text = ('varying Color ' + jitEvalMapFn(attrName) + '(const uniform ' +
            shaderType + ' * uniform object, uniform ShadingTLState *uniform tls, ' +
            'const varying State &state);\n')

    # dEvalMapAttr
    text += ('varying ColorDual ' + jitDEvalMapFn(attrName) + '(const uniform ' +
             shaderType + ' * uniform object, uniform ShadingTLState *uniform tls, ' +
             'const varying State &state);\n')

    return text


def jitDefineEvalAttrBoundFn(shaderType, retType, attrName):
    # evalAttrNameBound
    text = ('varying ' + retType + '\n' + evalAttrBoundFn(attrName) +
            '(const uniform ' + shaderType +
            ' * uniform object, uniform ShadingTLState *uniform tls, ' + 
            'const varying State &state) {\n')
    if retType == 'Color':
        text +=('    SHADING_JIT_EVAL_BOUND_COLOR(object, tls, state, ' +
                getAttrFn(attrName) + ', ' + jitEvalMapFn(attrName) + ');\n')
    elif retType == 'float':
        text +=('    SHADING_JIT_EVAL_BOUND_FLOAT(object, tls, state, ' +
                getAttrFn(attrName) + ', ' + jitEvalMapFn(attrName) + ');\n')
    elif retType == 'Vec2f':
        text +=('    SHADING_JIT_EVAL_BOUND_VEC2F(object, tls, state, ' +
                getAttrFn(attrName) + ', ' + jitEvalMapFn(attrName) + ');\n')
    elif retType == 'Vec3f':
        text +=('    SHADING_JIT_EVAL_BOUND_VEC3F(object, tls, state, ' +
                jitEvalMapFn(attrName) + ');\n')
    text += '}\n'

    # dEvalAttrNameBound
    text += ('varying ' + DualType(retType) + '\n' + dEvalAttrBoundFn(attrName) +
             '(const uniform ' + shaderType +
             ' * uniform object, uniform ShadingTLState *uniform tls, ' + 
             'const varying State &state) {\n')
    if retType == 'Color':
        text +=('    SHADING_JIT_DEVAL_BOUND_COLOR(object, tls, state, ' +
                getAttrFn(attrName) + ', ' + jitDEvalMapFn(attrName) + ');\n')
    elif retType == 'float':
        text +=('    SHADING_JIT_DEVAL_BOUND_FLOAT(object, tls, state, ' +
                getAttrFn(attrName) + ', ' + jitDEvalMapFn(attrName) + ');\n')
    elif retType == 'Vec2f':
        text +=('    SHADING_JIT_DEVAL_BOUND_VEC2F(object, tls, state, ' +
                getAttrFn(attrName) + ', ' + jitDEvalMapFn(attrName) + ');\n')
    elif retType == 'Vec3f':
        text +=('    SHADING_JIT_DEVAL_BOUND_VEC3F(object, tls, state, ' +
                jitDEvalMapFn(attrName) + ');\n')
    text += '}\n'

    return text

def jitDefineEvalAttrUnBoundFn(shaderType, retType, attrName):
    # evalAttrNameUnBound
    text = ('varying ' + retType + '\n' + evalAttrUnBoundFn(attrName) +
            '(const uniform ' + shaderType +
            ' * uniform object, uniform ShadingTLState *uniform tls, ' + 
            'const varying State &state) {\n')
    if retType == 'Color':
        text +=('    SHADING_JIT_EVAL_UNBOUND_COLOR(object, ' +
                getAttrFn(attrName)+ ');\n')
    elif retType == 'float':
        text +=('    SHADING_JIT_EVAL_UNBOUND_FLOAT(object, ' +
                getAttrFn(attrName) + ');\n')
    elif retType == 'Vec2f':
        text +=('    SHADING_JIT_EVAL_UNBOUND_VEC2F(object, ' +
                getAttrFn(attrName) + ');\n')
    elif retType == 'Vec3f':
        text +=('    SHADING_JIT_EVAL_UNBOUND_VEC3F(object, ' +
                getAttrFn(attrName) + ');\n')
    text += '}\n'

    # dEvalAttrNameUnBound
    text += ('varying ' + DualType(retType) + '\n' + dEvalAttrUnBoundFn(attrName) +
             '(const uniform ' + shaderType +
             ' * uniform object, uniform ShadingTLState *uniform tls, ' + 
             'const varying State &state) {\n')
    if retType == 'Color':
        text +=('    SHADING_JIT_DEVAL_UNBOUND_COLOR(object, ' +
                getAttrFn(attrName)+ ');\n')
    elif retType == 'float':
        text +=('    SHADING_JIT_DEVAL_UNBOUND_FLOAT(object, ' +
                getAttrFn(attrName) + ');\n')
    elif retType == 'Vec2f':
        text +=('    SHADING_JIT_DEVAL_UNBOUND_VEC2F(object, ' +
                getAttrFn(attrName) + ');\n')
    elif retType == 'Vec3f':
        text +=('    SHADING_JIT_DEVAL_UNBOUND_VEC3F(object, ' +
                getAttrFn(attrName) + ');\n')
    text += '}\n'

    return text

def jitDeclareEvalAttrFn(shaderType, retType, attrName):
    # evalAttrName()
    text = ('varying ' + retType + '\n' + evalAttrFn(attrName) +
            '(const uniform ' + shaderType +
            ' * uniform object, uniform ShadingTLState *uniform tls, ' +
            'const varying State &state);\n')

    # dEvalAttrName()
    text += ('varying ' + DualType(retType) + '\n' + dEvalAttrFn(attrName) +
             '(const uniform ' + shaderType +
             ' * uniform object, uniform ShadingTLState *uniform tls, ' +
             'const varying State &state);\n')

    return text

def defineEvalAttrFn(shaderType, retType, attrName):
    # evalAttrName(obj, tls, state);
    text = ('inline varying ' + retType +
            '\n' + evalAttrFn(attrName) +
            '(const uniform ' + shaderType + ' * uniform obj, ' +
            'uniform ShadingTLState *uniform tls, ' +
            'const varying State &state) {\n' +
            '    return ' + evalAttrFn(retType) +
            '(obj, tls, state, ' + attrName + ');\n}\n')

    # dEvalAttrName(obj, tls, state);
    text += ('inline varying ' + DualType(retType) +
             '\n' + dEvalAttrFn(attrName) +
             '(const uniform ' + shaderType + ' * uniform obj, ' +
             'uniform ShadingTLState *uniform tls, ' +
             'const varying State &state) {\n' +
             '    return ' + dEvalAttrFn(retType) +
             '(obj, tls, state, ' + attrName + ');\n}\n')

    return text

def defineEvalCompFn(shaderType, compName, attrColor, attrFactor, attrShow):
    # evalCompName
    text = 'inline varying Color\n'
    text +=(evalCompFn(compName) +
            '(const uniform ' + shaderType + ' * uniform obj, ' +
            'uniform ShadingTLState *uniform tls, ' +
            'const varying State &state)\n')
    text += '{\n'
    text += '    Color result = Color_ctor(0.f);\n'
    text += '    if (' + getAttrFn(attrShow) + '(obj)) {\n'
    text += '        const uniform float factor = ' + getAttrFn(attrFactor) + '(obj);\n'
    text += '        if (!isZero(factor)) {\n'
    text += '            result = ' + evalAttrFn(attrColor) + '(obj, tls, state) * factor;\n'
    text += '        }\n'
    text += '    }\n'
    text += '    return result;\n'
    text += '}\n'

    # dEvalCompName
    text += 'inline varying ColorDual\n'
    text +=(dEvalCompFn(compName) +
            '(const uniform ' + shaderType + ' * uniform obj, ' +
            'uniform ShadingTLState *uniform tls, ' +
            'const varying State &state)\n')
    text += '{\n'
    text += '    ColorDual result = ColorDual_ctor(sBlack);\n'
    text += '    if (' + getAttrFn(attrShow) + '(obj)) {\n'
    text += '        const uniform float factor = ' + getAttrFn(attrFactor) + '(obj);\n'
    text += '        if (!isZero(factor)) {\n'
    text += '            result = ' + dEvalAttrFn(attrColor) + '(obj, tls, state) * factor;\n'
    text += '        }\n'
    text += '    }\n'
    text += '    return result;\n'
    text += '}\n'

    return text

def jitDefineEvalNormalBoundFn(shaderType, compName, attrNameNormal, attrNameBlend):
    # evalNormalAttrBound
    text = 'varying Vec3f\n'
    text +=(evalNormalBoundFn(compName) + '(const uniform ' + shaderType +
            ' * uniform object, uniform ShadingTLState *uniform tls, ' +
            'const varying State &state) {\n')
    text +=('    SHADING_JIT_EVAL_BOUND_NORMAL(object, tls, state, ' +
            jitEvalMapFn(attrNameNormal) + ', ' + getAttrFn(attrNameBlend) + ');\n')
    text += '}\n'

    # dEvalNormalAttrBound
    text += 'varying Vec3Dual\n'
    text +=(dEvalNormalBoundFn(compName) + '(const uniform ' + shaderType +
            ' * uniform object, uniform ShadingTLState *uniform tls, ' + 
            'const varying State &state) {\n')
    text +=('    SHADING_JIT_DEVAL_BOUND_NORMAL(object, tls, state, ' +
            jitDEvalMapFn(attrNameNormal) + ', ' + getAttrFn(attrNameBlend) + ');\n')
    text += '}\n'

    return text

def jitDefineEvalNormalUnBoundFn(shaderType, compName):
    # evalNormalAttrUnBound
    text = 'varying Vec3f\n'
    text +=(evalNormalUnBoundFn(compName) + '(const uniform ' + shaderType +
            ' * uniform object, uniform ShadingTLState *uniform tls, ' + 
            'const varying State &state) {\n')
    text += '    SHADING_JIT_EVAL_UNBOUND_NORMAL(state);\n'
    text += '}\n'

    # dEvalNormalAttrUnBound
    text += 'varying Vec3Dual\n'
    text +=(dEvalNormalUnBoundFn(compName) + '(const uniform ' + shaderType +
            ' * uniform object, uniform ShadingTLState *uniform tls, ' + 
            'const varying State &state) {\n')
    text += '    SHADING_JIT_DEVAL_UNBOUND_NORMAL(state);\n'
    text += '}\n'

    return text

def jitDeclareEvalNormalFn(shaderType, compName):
    # evalNormalAttr
    text = 'varying Vec3f '
    text +=(evalNormalFn(compName) + '(const uniform ' + shaderType +
            ' * uniform object, uniform ShadingTLState *uniform tls, ' + 
            'const varying State &state);\n')

    # dEvalNormalAttr
    text += 'varying Vec3Dual '
    text +=(dEvalNormalFn(compName) + '(const uniform ' + shaderType +
            ' * uniform object, uniform ShadingTLState *uniform tls, ' + 
            'const varying State &state);\n')

    return text

def defineEvalNormalFn(shaderType, normal, attrNameNormal, attrNameBlend):
    # evalNormalAttrName
    text = 'inline Vec3f\n'
    text +=(evalNormalFn(normal) + '(const uniform ' + shaderType +
            ' * uniform obj, uniform ShadingTLState *uniform tls, ' + 
            'const varying State &state)\n')
    text += '{\n'
    text +=('    return evalNormal(obj, tls, state, ' + attrNameNormal +
            ', ' + attrNameBlend + ');\n')
    text += '}\n'

    # dEvalNormalAttrName
    text += 'inline Vec3Dual\n'
    text +=(dEvalNormalFn(normal) + '(const uniform ' + shaderType +
            ' * uniform obj, uniform ShadingTLState *uniform tls, ' + 
            'const varying State &state)\n')
    text += '{\n'
    text +=('    return dEvalNormal(obj, tls, state, ' + attrNameNormal +
            ', ' + attrNameBlend + ');\n')
    text += '}\n'

    return text

def jitDefineExports():
    text = 'uniform Map * uniform exportMapPtrType() {}\n'
    text += 'uniform ShadingTLState * uniform exportTlsPtrType() {}\n'
    text += 'varying State * uniform exportStatePtrType() {}\n'
    text += 'varying Color exportColorType() {}\n'
    text += 'varying ColorDual exportColorDualType() {}\n'
    return text

def jitDefineAttrSubMacro(shader):
    text = '#define ' + shader['name'].upper() + '_JIT_DEFINE_ATTR_SUB(attrSub) \\\n'
    text += 'shading::Jit::AttrSub attrSub[] = { \\\n'
    for attr, data in shader['attributes'].iteritems():
        jitType = JitType(data['type'])
        if jitType != '':
            text += '    { "' + attr + '",\\\n'
            text += '      ' + jitType + ', \\\n'
            text += '      (intptr_t) &' + attr + ', \\\n'
            text += '      (intptr_t) &this->get(' + attr + '), \\\n'
            if 'flags' in data and 'FLAGS_BINDABLE' in data['flags']:
                text += '      (intptr_t) this->getBinding(' + attr + ') \\\n'
            else:
                text += '      (intptr_t) nullptr \\\n'
            text += '    }, \\\n'
    text += '    { nullptr, \\\n'
    text += '      shading::Jit::NoType, \\\n'
    text += '      (intptr_t) nullptr, \\\n'
    text += '      (intptr_t) nullptr, \\\n'
    text += '      (intptr_t) nullptr \\\n'
    text += '    } \\\n'
    text += '};\n'
    return text

def jitDefineNormSubMacro(shader):
    text = '#define ' + shader['name'].upper() + '_JIT_DEFINE_NORM_SUB(normSub) \\\n'
    text += 'shading::Jit::NormalSub normSub[] = { \\\n'
    if 'normals' in shader:
        for normal, data in shader['normals'].iteritems():
            text += '    {"' + normal + '", \\\n'
            text += '      (intptr_t) &' + data['value'] + ', \\\n'
            text += '      (intptr_t) &' + data['dial'] + ', \\\n'
            text += '      (intptr_t) this->getBinding(' + data['value'] + ') \\\n'
            text += '    }, \\\n'
    text += '    { nullptr, \\\n'
    text += '      (intptr_t) nullptr, \\\n'
    text += '      (intptr_t) nullptr, \\\n'
    text += '      (intptr_t) nullptr \\\n'
    text += '    } \\\n'
    text += '};\n'
    return text

def jitDefineMembersMacro(shaderName):
    text = '#define ' + shaderName + '_JIT_MEMBERS()\\\n'
    text += '    shading::Jit mJit;\\\n'
    text += '    virtual llvm::Function *generateLlvm(llvm::Module *mod, bool fastEntry, \\\n'
    text += '                                         llvm::Function *entries[]) const   \\\n'
    text += '    {\\\n'
    text += '        ' + shaderName + '_JIT_DEFINE_ATTR_SUB(attrSub); \\\n'
    text += '        ' + shaderName + '_JIT_DEFINE_NORM_SUB(normSub); \\\n'
    text += '        return mJit.linkIntoModule(mod, this, attrSub, normSub, fastEntry, entries); \\\n'
    text += '    }\n'
    return text


def jitUpdateMacro_0(sname):
    text  = '#define ' + sname.upper() + '_JIT_UPDATE()                               \\\n'
    text += '{                                                                        \\\n'
    text += '    /* delete any existing jit functions, reset to built-in */           \\\n'
    text += '    mJit.releaseFunction();                                              \\\n'
    return text

def jitUpdateMacro_1(sname):
    text  = '    rdl2::SceneVariables const &vars =                                   \\\n'
    text += '        mSceneClass.getSceneContext()->getSceneVariables();              \\\n'
    text += '    int useJit = vars.get(rdl2::SceneVariables::sJitCompileShaders);     \\\n'
    return text

def jitUpdateMacro_2(sname):
    text  = '        /* try to jit a function */                                      \\\n'
    text += '        std::string jitError;                                            \\\n'
    text += '        ' + sname.upper() + '_JIT_DEFINE_ATTR_SUB(attrSub);              \\\n'
    text += '        ' + sname.upper() + '_JIT_DEFINE_NORM_SUB(normSub);              \\\n'
    return text

def jitUpdateMacro_3():
    text  = '}\n' 
    return text

def jitDefineUpdateMacroDsp(sname):
    text  = jitUpdateMacro_0(sname)
    text += '    mDisplaceFuncv = (rdl2::DisplaceFuncv) ispc::' + sname + '_getDisplaceFunc();\\\n'
    text += jitUpdateMacro_1(sname)
    text += '    if (useJit != shading::JIT_MODE_OFF) {                               \\\n'
    text += jitUpdateMacro_2(sname)
    text += '        shading::Jit::DisplaceFunc func =                                \\\n'
    text += '            mJit.getDisplaceFunction(this, jitError, attrSub, normSub);  \\\n'
    text += '        if (!func) {                                                     \\\n'
    text += '            error("JIT failure: ", jitError);                            \\\n'
    text += '        } else {                                                         \\\n'
    text += '            mDisplaceFuncv = (rdl2::DisplaceFuncv) func;                 \\\n'
    text += '        }                                                                \\\n'
    text += '    }                                                                    \\\n'
    text += jitUpdateMacro_3()
    return text

def jitDefineUpdateMacroMap(sname):
    text  = jitUpdateMacro_0(sname)
    text += '    mSampleFuncv = (rdl2::SampleFuncv) ispc::' + sname + '_getSampleFunc();\\\n'
    text += '    mSampleADFuncv = (rdl2::SampleADFuncv) ispc::' + sname + '_getSampleADFunc();\\\n'
    text += jitUpdateMacro_1(sname)
    text += '    if (useJit == shading::JIT_MODE_SHALLOW) {                           \\\n'
    text += jitUpdateMacro_2(sname)
    text += '        shading::Jit::MapSampleFuncs funcs =                             \\\n'
    text += '            mJit.getMapSampleFuncs(this, jitError, attrSub, normSub);    \\\n'
    text += '        if (!funcs.sampleFunc || !funcs.sampleADFunc) {                  \\\n'
    text += '            error("JIT failure: ", jitError);                            \\\n'
    text += '        } else {                                                         \\\n'
    text += '            mSampleFuncv = (rdl2::SampleFuncv) funcs.sampleFunc;         \\\n'
    text += '            mSampleADFuncv = (rdl2::SampleADFuncv) funcs.sampleADFunc;   \\\n'
    text += '        }                                                                \\\n'
    text += '    }                                                                    \\\n'
    text += jitUpdateMacro_3()
    return text

def jitDefineUpdateMacroMat(sname):
    text  = jitUpdateMacro_0(sname)
    text += '    mShadeFuncv = (rdl2::ShadeFuncv) ispc::' + sname + '_getShadeFunc(); \\\n'
    text += jitUpdateMacro_1(sname)
    text += '    if (useJit != shading::JIT_MODE_OFF) {                               \\\n'
    text += jitUpdateMacro_2(sname)
    text += '        shading::Jit::MaterialShadeFunc func =                           \\\n'
    text += '            mJit.getMaterialShadeFunc(this, jitError, attrSub, normSub); \\\n'
    text += '        if (!func) {                                                     \\\n'
    text += '            error("JIT failure: ", jitError);                            \\\n'
    text += '        } else {                                                         \\\n'
    text += '            mShadeFuncv = (rdl2::ShadeFuncv) func;                       \\\n'
    text += '        }                                                                \\\n'
    text += '    }                                                                    \\\n'
    text += jitUpdateMacro_3()
    return text



def BuildIspcDsoSource(target, source, env):
    '''
    Create attributes.cc (target[0]), attributesISPC.cc (target[1]),
    attributes.isph (target[2]), jit.h (target[3]), labels.h (target[4]),
    and labels.isph (target[5]) from dsoName.json (source[0])
    '''
    # load the shader attribute definition from the json file
    shader = json.loads(open(str(source[0])).read(), object_pairs_hook=OrderedDict)

    # unlike standard rdl2 dsos, we place the shader attributes into
    # a non-anonymous namespace so we can set the (global) ispc
    # attribute keys to the same names.  at the end of attributes.cc
    # we set 'using namespace ns' which should have a similar effect
    # to using an anonymous namespace.
    ns = shader['name'] + '_attr'

    # create attributes.cc (target[0])
    # included by dsoName.cc
    text  = '#include <scene_rdl2/scene/rdl2/rdl2.h>\n'
    text += 'using namespace arras;\n'
    text += 'using namespace arras::rdl2;\n'
    text += 'RDL2_DSO_ATTR_DECLARE_NS(' + ns + ')\n'
    for attr, data in shader['attributes'].iteritems():
        text += 'rdl2::AttributeKey<%s> %s;\n' % (data['type'], attr)
    text += 'RDL2_DSO_ATTR_DEFINE(%s)\n' % (shader['type'])
    
    # Establish set of attribute keywords that have specific meaning.
    # Any data who's key is not in this list will be added as metadata
    dataKeywords = ['type', 'name', 'default', 'flags', 'interface', 'group', 'enum']
    
    for attr, data in shader['attributes'].iteritems():
        text += ('%s::%s = sceneClass.declareAttribute<%s>("%s"' %
                 (ns, attr, data['type'], data['name']))
        if 'default' in data:
            text += ', %s' % data['default']
        if 'flags' in data:
            text += ', %s' % data['flags']
        if 'interface' in data:
            text += ', %s' % data['interface']
        text += ');\n'
        if 'group' in data:
            text += ('sceneClass.setGroup("%s", %s::%s);\n' %
                     (data['group'], ns, attr))
        if 'enum' in data:
            for enum, value in data['enum'].iteritems():
                text += ('sceneClass.setEnumValue(%s::%s, %s, "%s");\n' %
                         (ns, attr, value, enum))
        # Add rest as metadata, including "comment"
        for metaKey, metaStr in data.iteritems():
            if not metaKey in dataKeywords :
                text += ('sceneClass.setMetadata(%s::%s, "%s", "%s");\n' %
                         (ns, attr, metaKey, metaStr))
    if 'labels' in shader:
        text += 'static const char *labels[] = {\n'
        for variable, label in shader['labels'].iteritems():
            text += '    "%s",\n' % label
        text += '    nullptr\n};\n'
        text += 'sceneClass.declareDataPtr("labels", labels);\n'
            
    text += 'RDL2_DSO_ATTR_END\n'
    text += 'using namespace ' + ns + ';\n'
    f = open(str(target[0]), "wb")
    f.write(text)
    f.close()

    # create attributesISPC.cc (target[1])
    # this file creates the global ispc attribute keys
    # namespace ns { extern rdl2::AttributeKey<Type> attrName; }
    # TypeAttrKeyISPC *attrName = (TypeAttrKeyISPC *) &ns::attrName;
    text  = '#include <scene_rdl2/scene/rdl2/rdl2.h>\n'
    text += '#include <scene_rdl2/scene/rdl2/ISPCSupport.h>\n'
    text += 'using namespace arras;\n'
    text += 'using namespace arras::rdl2;\n'
    for attr, data in shader['attributes'].iteritems():
        # if the keytype is unknown, then very likely this attribute is
        # not supported in ispc and exists only for c++ code
        keyType = IspcKeyType(data['type'])
        if keyType != 'UNKNOWN_ATTR_TYPE':
            text += ('namespace ' + ns + ' { extern rdl2::AttributeKey<%s> %s;' %
                     (data['type'], attr) + ' }\n')
            text += ('%s *%s = (%s *) &%s::%s;\n' %
                     (keyType, attr, keyType, ns, attr))
    f = open(str(target[1]), "wb")
    f.write(text)
    f.close()

    # create attributes.isph (target[2])
    # included by dsoName.ispc
    # extern uniform <Type>AttrKeyISPC * uniform attrKey;
    assert str(target[2]).endswith('.isph')
    text = '#pragma once\n'
    text += '#include <'
    # if we are a dso being built within arras, we do not prepend
    # the folio name 'arras' when including Shading.isph
    if env.File('#arras/lib/rendering/shading/ispc/Shading.isph').exists() == False:
        text += 'arras/'
    text += 'rendering/shading/ispc/Shading.isph>\n'
    text += '#include <scene_rdl2/scene/rdl2/rdl2.isph>\n'
    text += '#include <scene_rdl2/scene/rdl2/ISPCSupport.h>\n'
    for attr, data in shader['attributes'].iteritems():
        # if the keytype is unknown, then very likely this attribute is
        # not supported in ispc and exists only for c++ code
        keyType = IspcKeyType(data['type'])
        if keyType != 'UNKNOWN_ATTR_TYPE':
            text += '//-------------------------------------------------\n'
            text += '// ' + attr + '\n'
            text += '//-------------------------------------------------\n'
            text += 'extern uniform ' + keyType + ' * uniform ' + attr + ';\n'
            ispcType = IspcType(data['type'])
            shaderType = shaderTypeConv(shader['type'])
            needsEval = False
            if ispcType == 'Color' or ispcType == 'float' or ispcType == 'Vec2f' or ispcType == 'Vec3f':
                if 'flags' in data and 'FLAGS_BINDABLE' in data['flags']:
                    needsEval = True
            text += '#ifdef JIT\n'
            text += jitDeclareGetAttrFn(shaderType, ispcType, attr)
            if needsEval:
                text += jitDeclareEvalMapFn(shaderType, attr)
                text += jitDeclareEvalAttrFn(shaderType, ispcType, attr)
                text += jitDefineEvalAttrBoundFn(shaderType, ispcType, attr)
                text += jitDefineEvalAttrUnBoundFn(shaderType, ispcType, attr)
            text += '#else\n'
            text += defineGetAttrFn(shaderType, ispcType, attr)
            if needsEval:
                text += defineEvalAttrFn(shaderType, ispcType, attr)
            text += '#endif\n'
    if 'components' in shader:
        for comp, data in shader['components'].iteritems():
            shaderType = shaderTypeConv(shader['type'])
            text += '//-------------------------------------------------\n'
            text += '// Component ' + comp + '\n'
            text += '//-------------------------------------------------\n'
            text += defineEvalCompFn(shaderType, comp, data['color'],
                                     data['factor'], data['show'])
    if 'normals' in shader:
        for normal, data in shader['normals'].iteritems():
            text += '//-----------------------------------------------------\n'
            text += '// Normal ' + normal + '\n'
            text += '//-----------------------------------------------------\n'
            shaderType = shaderTypeConv(shader['type'])
            text += '#ifdef JIT\n'
            text += jitDeclareEvalNormalFn(shaderType, normal)
            text += jitDefineEvalNormalBoundFn(shaderType, normal, data['value'], data['dial'])
            text += jitDefineEvalNormalUnBoundFn(shaderType, normal)
            text += '#else\n'
            text += defineEvalNormalFn(shaderType, normal, data['value'], data['dial'])
            text += '#endif\n'
    text += '#ifdef JIT\n'
    text += jitDefineExports()
    text += '#endif\n'
    f = open(str(target[2]), "wb")
    f.write(text)
    f.close()

    # create jit.h (target[3])
    # optionally included by shaders that support jit
    assert str(target[3]).endswith('.h')
    text = '#pragma once\n'
    text += '#include <'
    # if we are a dso being built within arras, we do not prepend
    # the folio name 'arras' when including Jit.h
    if env.File('#arras/lib/rendering/shading/ispc/Jit.h').exists() == False:
        text += 'arras/'
    text += 'rendering/shading/ispc/Jit.h>\n'
    text += jitDefineAttrSubMacro(shader)
    text += jitDefineNormSubMacro(shader)
    text += jitDefineMembersMacro(shader['name'].upper())
    if shader['type'] == 'Map':
        text += jitDefineUpdateMacroMap(shader['name'])
    elif shader['type'] == 'Displacement':
        text += jitDefineUpdateMacroDsp(shader['name'])
    elif shader['type'] == 'Material':
        text += jitDefineUpdateMacroMat(shader['name'])
    f = open(str(target[3]), 'wb')
    f.write(text)
    f.close()

    # create labels.h
    # optionally included by shaders that assign labels to lobes
    assert str(target[4]).endswith('.h')
    text = '#pragma once\n'
    if 'labels' in shader:
        val = 1
        for variable, label in shader['labels'].iteritems():
            text += 'static const int %s = %d;\n' % (variable, val)
            val = val + 1
    f = open(str(target[4]), 'wb')
    f.write(text)
    f.close()

    # create labels.isph
    # optionally included by shaders that assign labels to lobes
    assert str(target[5]).endswith('.isph')
    text = '#pragma once\n'
    if 'labels' in shader:
        val = 1
        for variable, label in shader['labels'].iteritems():
            text += 'static const uniform int %s = %d;\n' % (variable, val)
            val = val + 1
    f = open(str(target[5]), 'wb')
    f.write(text)
    f.close()

def DWAIspcDso(parentEnv, target, ccSource, ispcSource, jsonSource, **kwargs):
    '''
    return a dso based on source (.cc, .ispc files) and attribute_source (.json)
    several source files are generated from the .json
    '''

    # autogenerate attributes.cc, attributesISPC.cc, attributes.isph,
    # jit.h, labels.h, and labels.isph
    # from the json source in the directory of the target
    targetDir = os.path.dirname(target.abspath)
    autogen = [targetDir + '/attributes.cc',
               targetDir + '/attributesISPC.cc',
               targetDir + '/attributes.isph',
               targetDir + '/jit.h',
               targetDir + '/labels.h',
               targetDir + '/labels.isph']
    builder = parentEnv.Command(autogen, jsonSource, BuildIspcDsoSource)
    # Scons is smart enough to know that changes to the program
    # code of the BuildIspcDsoSource() function should trigger a rebuild of
    # the builder's targets, but it is not smart enough to know that
    # changes to the program text of functions that BuildIspcDsoSource()
    # calls should also trigger a change.  For example, adding a
    # print 'foo' to BuildIspcDsoSource() will trigger a rebuild of
    # the targets as expected, but adding a print 'foo' to
    # IspcKeyType() (a function called by BuildIspcDsoSource()) will not.
    # The solution to this problem that I've come up with is to depend the builder
    # on this file itself.  I use BuildIspcDsoSource.func_code.co_filename
    # instead of __file__ because __file__ can be either the .py or compiled
    # .pyc file while the former is always and unambiguously the .py file.
    # The other, less desirable solution would be to remove all the function
    # calls from BuildIspcDsoSource(), but that would substantially reduce
    # the readability of an already fairly complicated function.
    parentEnv.Depends(builder, BuildIspcDsoSource.func_code.co_filename)

    # now build the dso with the correct paths
    env = parentEnv.Clone();
    env.AppendUnique(ISPCFLAGS = [
            "-Iarras/lib",
            "-I" + env.Dir(env.subst('$BUILD_DIR')).abspath + '/arras/lib',
            "-I" + targetDir
            ])
    # need to set the cpp path to the build dir of the dso and
    # the ispc header autogenerate location
    ispcHeaderDir = (
        '/'.join(os.path.dirname(target.srcnode().path).split('/')[2:]))
    env.AppendUnique(CPPPATH = [targetDir,
                                (env['BUILD_DIR'] + '/autogenerate/' +
                                 ispcHeaderDir)
                                ])

    sources  = [ccSource]
    sources += [autogen[1]]
    ispc_output = env.IspcSource([ispcSource])
    sources += ispc_output

    dso = env.DWARdl2Dso(target, sources,
                         RDL2_ATTRIBUTES_SOURCE = autogen[0], **kwargs)
    bc = env.IspcBitcode(ispcSource, **kwargs)
    dso.update({'bc' : bc})
    return dso

def DWAInstallIspcDso(env, dsoinfo):
    env.DWAInstallRdl2Dso(dsoinfo)
    if 'bc' in dsoinfo:
        # Install the llvm bitcode file into rdl2dso
        dwa_install.installTargetIntoDir(env, '@install_dso', 'rdl2dso', dsoinfo['bc'])

def generate(env):
    env.AddMethod(DWAIspcDso)
    env.AddMethod(DWAInstallIspcDso)

def exists(env):
    return True
