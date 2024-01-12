# Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
# SPDX-License-Identifier: Apache-2.0

add_subdirectory(router)
add_subdirectory(noderouter)

set(CmdName arras4_node)

add_executable(${CmdName})

target_sources(${CmdName}
    PRIVATE
        ArrasNode.cc
        BanList.cc
        ConfigurationClient.cc
        ConsulClient.cc
        HardwareFeatures.cc
        main.cc
        NodeService.cc
        PreemptionMonitor.cc
        ServiceClient.cc
        UrlRouter.cc
)

target_link_libraries(${CmdName}
    PUBLIC
        ArrasCore::shared_impl
        ArrasCore::execute
        ArrasCore::http
        ArrasCore::httpserver
        ArrasCore::arras4_log
        ArrasCore::arras4_athena
        ${PROJECT_NAME}::node_session
        Boost::program_options
        Boost::filesystem
        pthread
)

# prevent use of crash reporter since it isn't yet supported
target_compile_definitions(${CmdName} PRIVATE DONT_USE_CRASH_REPORTER)

# Use RUNPATH instead of RPATH
ArrasNode_link_options(${CmdName})

install(TARGETS ${CmdName}
        EXPORT ${ExportGroup}
        RUNTIME DESTINATION bin)
