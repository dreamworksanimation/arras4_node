# Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
# SPDX-License-Identifier: Apache-2.0

set(LibName node_session)

set(InstallIncludeDir session)
set(exportGroup ${PROJECT_NAME}Targets)

add_library(${LibName} SHARED "")
add_library(${PROJECT_NAME}::${LibName} ALIAS ${LibName})

target_sources(${LibName}
    PRIVATE
        ArrasController.cc 
        ArrasSessions.cc 
        Computation.cc 
        ComputationConfig.cc 
        ComputationDefaults.cc 
        Session.cc 
        SessionConfig.cc
)

set_property(TARGET ${LibName}
    PROPERTY PUBLIC_HEADER
        ArrasController.h 
        ArrasSessions.h 
        Computation.h 
        ComputationConfig.h 
        ComputationDefaults.h 
        OperationError.h 
        Session.h 
        SessionConfig.h 
        SessionError.h
)

target_include_directories(${LibName}
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(${LibName}
    PUBLIC
        ArrasCore::arras4_log 
        ArrasCore::execute
        ArrasCore::shared_impl
        ArrasCore::core_messages
        ArrasCore::network
        ArrasNode::node_messages
)

# If at Dreamworks add a SConscript stub file so others can use this library.
SConscript_Stub(${LibName})

# Use RUNPATH instead of RPATH
ArrasNode_link_options(${LibName})

install(TARGETS ${LibName}
        EXPORT ${ExportGroup}
        PUBLIC_HEADER DESTINATION include/${InstallIncludeDir})
