# Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
# SPDX-License-Identifier: Apache-2.0

set(LibName node_messages)
set(ExportGroup ${PROJECT_NAME}Targets)

set(InstallIncludeDir node/messages)

add_library(${LibName} SHARED "")
add_library(${PROJECT_NAME}::${LibName} ALIAS ${LibName})

target_sources(${LibName}
    PRIVATE
        ClientConnectionStatusMessage.cc
        ComputationStatusMessage.cc
        RouterInfoMessage.cc
        SessionRoutingDataMessage.cc
)

set_property(TARGET ${LibName}
    PROPERTY PUBLIC_HEADER
        ClientConnectionStatusMessage.h
        ComputationStatusMessage.h
        RouterInfoMessage.h
        SessionRoutingDataMessage.h
)

target_include_directories(${LibName}
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

target_link_libraries(${LibName}
    PUBLIC
        ArrasCore::message_api
)

# If at Dreamworks add a SConscript stub file so others can use this library.
SConscript_Stub(${LibName})

# Use RUNPATH instead of RPATH
ArrasNode_link_options(${LibName})

install(TARGETS ${LibName}
        EXPORT ${ExportGroup}
        PUBLIC_HEADER DESTINATION include/${InstallIncludeDir})
