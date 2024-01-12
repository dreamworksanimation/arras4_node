# Copyright 2023-2024 DreamWorks Animation LLC and Intel Corporation
# SPDX-License-Identifier: Apache-2.0

set(CmdName arras4_router)

add_executable(${CmdName})

target_sources(${CmdName}
    PRIVATE
        main.cc
)

target_link_libraries(${CmdName}
    PUBLIC
        ArrasCore::arras4_log
        ArrasCore::arras4_athena
        ArrasCore::message_impl 
        ArrasCore::shared_impl 
        ArrasCore::routing 
        ArrasCore::core_messages
        ArrasCore::network 
        ArrasCore::http 
        ArrasCore::httpserver 
        ArrasCore::exceptions
        ${PROJECT_NAME}::node_router
        Boost::date_time 
        Boost::program_options 
        Boost::filesystem 
        pthread 
        dl
)

# prevent use of crash reporter since it isn't yet supported
target_compile_definitions(${CmdName} PRIVATE DONT_USE_CRASH_REPORTER)

# Use RUNPATH instead of RPATH
ArrasNode_link_options(${CmdName})

install(TARGETS ${CmdName}
        EXPORT ${ExportGroup}
        RUNTIME DESTINATION bin)
