# Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
# SPDX-License-Identifier: Apache-2.0

set(LibName node_router)

set(InstallIncludeDir node/router)

add_library(${LibName} SHARED "")
add_library(${PROJECT_NAME}::${LibName} ALIAS ${LibName})

target_sources(${LibName}
    PRIVATE
        ClientRemoteEndpoint.cc
        ListenServer.cc
        NodeRouter.cc
        NodeRouterManage.cc
        PeerManager.cc
        RemoteEndpoint.cc
        RouteMessage.cc
        RoutingTable.cc
        SessionNodeMap.cc
        SessionRoutingData.cc
        ThreadedNodeRouter.cc
)

set_property(TARGET ${LibName}
    PROPERTY PUBLIC_HEADER
        NodeRouterManage.h
        NodeRouterOptions.h
        pthread_create_interposer.h
)

target_include_directories(${LibName}
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

target_link_libraries(${LibName}
    PUBLIC
        ArrasCore::arras4_athena
        ArrasCore::exceptions
        ArrasCore::arras4_log
        ArrasCore::network
        ArrasCore::http
        ArrasCore::core_messages
        ArrasCore::message_impl
        ArrasCore::routing
        ArrasCore::shared_impl
        ${PROJECT_NAME}::node_messages
        Boost::filesystem
)

# If at Dreamworks add a SConscript stub file so others can use this library.
SConscript_Stub(${LibName})

# Use RUNPATH instead of RPATH
ArrasNode_link_options(${LibName})

install(TARGETS ${LibName}
        EXPORT ${ExportGroup}
        PUBLIC_HEADER DESTINATION include/${InstallIncludeDir})
