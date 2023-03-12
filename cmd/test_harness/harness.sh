#!/bin/bash

tasks="cemu router service"
nodeid=7aac86c2-c5b3-4cae-bb56-35e8facc6a7d
httpport=56915
tcpport=46128
sessionfile=test.sd
coordport=8888

if [ -z $DEBUG_NODESERVICE ]; then
   tvs=""
else
   tvs="/rel/third_party/totalview64/current/bin/totalview --args"
fi
if [ -z $DEBUG_NODEROUTER ]; then
   tvr=""
else
   tvr="/rel/third_party/totalview64/current/bin/totalview --args"
fi

if [ -z $REZ_ARRAS4_NODE_VERSION ]; then
    echo "Run in an arras4_node rez 2 environment"
    exit
fi

## start all the tasks listed in $tasks, each
## one in a separate tab. An individual task
## is started by invoking this script with the
## name of the task as an argument
function start_tasks()
{
    cmd="gnome-terminal $TERM_OPTIONS"
    tabs=0
    for name in $tasks; do
        if [ $tabs -eq 0 ]; then
            cmd="$cmd --window --geometry 132x24 -e \"$0 $name\" --title=\"$name\""
        else
            cmd="$cmd --tab -e \"$0 $name\" --title=\"$name\""
        fi
        tabs=$(( tabs + 1))
    done

    eval $cmd
}

function start_router()
{
    $tvr arras4_standalone_router --nodeid $nodeid\
            --ipcName /tmp/arrasnodeipc-$nodeid\
            --inetPort $tcpport
    while :
    do
        sleep 10
    done
}

function start_service()
{
    # wait for noderouter to start
    sleep 2
    $tvs nodeservice --nodeId $nodeid --httpPort $httpport --coordinatorBaseUrl "http://localhost:$coordport/coordinator/1"
    while :
    do
        sleep 10
    done
}

function start_cemu()
{
    export PYTHONPATH=/rel/lang/python/packages/tornado/5.1.1/python-2.7/python:/rel/lang/python/packages/singledispatch/3.4.0.3/python-2.7/python:/rel/lang/python/packages/backports_abc/0.5/python-2.7/python
    python2.7 -i run.py $sessionfile $nodeid $httpport $tcpport
}

while [[ $# > 0 ]]; do

    # Get the next argument.
    arg="$1"
    shift
    case $arg in

        router)
            tasks="router"
            ;;
        service)
            tasks="service"
            ;;
        cemu)
            tasks="cemu"
            ;;
        *)
            echo "ERROR: Unsupported argument: $arg"
            exit 1
    esac
done

case $tasks in

    router)
        start_router
        ;;
    service)
        start_service
        ;;
    cemu)
        start_cemu
        ;;
    *)
        start_tasks
esac


