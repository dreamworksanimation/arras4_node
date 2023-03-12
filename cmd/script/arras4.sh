#!/bin/bash


################################################################################
##
## Start the arras-core stack on the local host.
##
## It uses /tmp/arras as its working directory and generates logs there that may
## be helpful in debugging.
##
################################################################################

##
## Constants.
##

_PID=$$

CONSUL_VERSION="0.7.0"
HOST_IPADDR=$(hostname -i)
LIBEE_VER="libee-2.3.25-40"
PORTS="8080 8087 8500"
TERM_OPTIONS="--geometry=200x25+50+50"
TERM_OPTIONS+=" --hide-menubar"

## Branch on Docker Configuration
docker_mode=0

GIT="/rel/third_party/git/2.14.2/bin/git"

_JAVA_HOME="/rel/third_party/jdk/jdk1.8.0_45_64"
if [ ! -d ${_JAVA_HOME} ]; then
	_JAVA_HOME="/opt/jdk1.7.0_75_64"
	docker_mode=1
fi

CONSUL_DIR="/work/rd/arras/third_party/consul/$CONSUL_VERSION"
if [ ! -d ${CONSUL_DIR} ]; then
    CONSUL_DIR="/opt/consul"
fi    

CONSUL_UI_DIR="$CONSUL_DIR/html"

##
## Work directories.
##
WORK_DIR="/tmp/arras"
CONSUL_WORK_DIR="$WORK_DIR/consul"

#SESSION_CONFIG_DIR="$WORK_DIR/session_configs"
#SESSION_CONFIG_REPO="http://github.anim.dreamworks.com/Jose/arras4_session_configs.git"

##
## Paths to output log files.
##
ARRAS_SH_LOG="$WORK_DIR/arras_sh.log"
CONSUL_LOG="$WORK_DIR/consul_service.log"
COORDINATOR_LOG="$WORK_DIR/coordinator_service.log"
NODE_LOG="$WORK_DIR/node_service.log"
PIDS_LOG="$WORK_DIR/pids.$_PID.log"
PORT_LOG="$WORK_DIR/ports.log"
STRACE_DIR="$WORK_DIR/strace"

##
## The consul configuration that will later be written to a config file.
##
read -d "" CONSUL_CONFIG << EOF
{
  "advertise_addr" : "$HOST_IPADDR",
  "bootstrap_expect" : 1,
  "datacenter" : "arras-test",
  "data_dir" : "/tmp/arras/consul/data",
  "log_level" : "debug",
  "server" : true,
  "ui_dir" : "$CONSUL_UI_DIR"
}
EOF


## Global variables.
consul_host=""
distributed_on=0
do_window=true
global_opts=""
install_dir=""
is_primary=0
node_host_list=""
node_log_level=4
node_use_rez=""
node_over_subscribe=""
node_exclusive_user=""
node_exclusive_production=""
node_ipc_dir=""
strace_cmd=""
strace_on=0
tasks="all"
timeout_error=0
local_env=0
node_cores=""
max_node_memory=""
use_affinity=1
use_color=1
stack="local"
node_port_num=0
config_branch="develop"

##
## Set signal traps so we can try to ensure all our processes are stopped.
##
trap killall SIGHUP SIGINT SIGQUIT SIGTERM


##
## Check to see if any ports needed by the services are currently in use.
##
function check_ports()
{
    opts=""
    for p in $PORTS; do
        opts="$opts -e :$p\s"
    done

    # Run netstat and look for our ports in use.
    netstat -tulpn 2>/dev/null | \
        grep -E $opts \
        | awk '{printf "%s %s\n", $4, $7}' \
        | sed -r "s/:+/:/g" \
        | cut -d: -f2- \
        | sed "s/\// /" > $PORT_LOG

    # If the netstat command caught something, print out an warning  message
    if [ $( wc -l $PORT_LOG | awk '{print $1}' ) != "0" ]; then
        echo "--------------------------------------------------"
        echo "WARNING:"
        echo "--------------------------------------------------"
        echo "Static ports are currently in use!"
        echo
        echo "Port PID/CMD"
        cat $PORT_LOG
        echo
        echo "Please free up the ports and try again."
        echo
        echo "If this is being caused by a previous instance of"
        echo "arras.sh you can run 'arras.sh kill' or close the"
        echo "arras.sh terminal to kill the older processes."
        echo "--------------------------------------------------"
        exit 2	
    fi
}

##
## Dump some information about our environment.
##
function dumpEnvironment()
{
    # Some values may contain strings so change the default field separator
    # to a carriage return rather than a space temporarily.
    OLDIFS=$IFS
    IFS=$(echo -en "\n\b")

    # Log the current environment variables.
    log "Environment variables:"
	for e in $( env|sort ); do
        log "  $e"
    done
    log ""

    # Print the current process limits.
    log "Resource limits:"
    for l in $( ulimit -a|sort ); do
        log "  $l"
    done
    log ""

    # Restore the old separator.
    IFS=$OLDIFS
}

##
## Print the help message.
##
function help()
{
    echo
    echo "Usage: $0 [kill] [-h|--help]"
	echo
    echo "Options:"
    echo
    echo "  -h,--help    Prints out this marginally useful help message."
    echo
    echo "  kill         Kill arras.sh processes from a previous run."
    echo
    echo "  -l <level>   Set the log-level for the node service (default=4)."
    echo
    echo "  --no-window  Run arras.sh in the foreground without launching in a"
    echo "               separate gnome-terminal"
    echo
    echo "  --strace     Run coordinator under strace and output the"
    echo "               logs under /tmp/arras/strace. Good for debugging, bad"
    echo "               for your tmp space as it can fill up quickly."
    echo "               USE WITH CAUTION!"
    echo "   -env        sets environment variables in this script instead of using the rez-env"
    echo
    echo "   -cores <cores>{/<hyperthreads_per_core>}"
    echo
    echo "   -max_node_memory <bytes> How much memory to allow for the node process (can append k,m, or g)"
    echo
    echo "   -use_affinity <0|1>"
    echo
    echo "   --use-color <0|1>"
    echo "   --exclusive-user Reserves this node exclusively for use by the current user"
    echo "   --exclusive-production <production> Reserves this node exclusively for clients which specify a matching production in their session options."
    echo "   --ipc-dir <dir> Location to create IPC socket files will be created it it doesn't exist"
    echo ""
    echo ""
}

##
## Run some initialization tasks.
##
## These are meant to be run once per invocation of arras.sh by the user, and
## not once per component started.
##
function init()
{
    # Make sure the root working directory exists.
    mkdir -p $WORK_DIR

    # Perform some validation before we start. If a check fails, the check will
    # force this script to exit.
    validate

    # Perform some working directory maintenance.
    rm -f $WORK_DIR/*log
    rm -rf $CONSUL_WORK_DIR/data $CONSUL_WORK_DIR/conf

    if [ $strace_on -eq 1 ]; then
        mkdir -p $STRACE_DIR
    fi

    # Set global options that will need to be propagated to sub-processes.
    set_global_opts

    # Log the start of arras.sh
    log "Started arras.sh"
    log "- user: $USER"
    log "- host: `hostname`"
    log "- tasks: $tasks"
    log "- strace_on: $strace_on"
    log "- stack: $stack"

    # Log some information about the runtime enviroment.
    dumpEnvironment
}

##
## Attempt to shutdown all the processes started by this script.
##
## We need to do this since just closing the terminal the processes were launched
## in don't always kill the processes since the SIGHUP
##
function killall()
{
    log "Shutdown signal received"

    # Disable our traps so we don't get ourselves caught in a signal loop.
    trap - SIGHUP SIGINT SIGQUIT SIGTERM

    # Shut down the consul server by nicely asking to leave.
    stop_consul

    # We run the kill command in a loop with a max_tries because sometimes, the
    # processes don't exit right away.
    counter=0
    max_tries=3
    while [ $counter -lt $max_tries ]
    do
        # Increment the counter.
        counter=$((counter + 1))

        # For some reason, running ps directly causes a child to fork which will
        # show up in the pids result and would cause an infinite loop if we
        # didn't guard with a counter. Doing a ps and spitting the output to a
        # a log file doesn't cause this issue, so that's what we do here. Spit
        # a list of processes to a file and then process the contents of the
        # file.
        #
        # When processing, we take care not to include this process' pid or our
        # kill command would be, well, killed.
        ps -e -o pid,cmd > $PIDS_LOG
        pids=`cat $PIDS_LOG \
              | grep -v grep \
              | grep -E -e "arras4.sh (acap|consul|coordinator|node|load_consul)" \
                  -e "consul agent" \
                  -e "(acap|coordinator)\.jar" \
                  -e "arras4_node --dev" \
              | awk '{printf " %d", $1}' \
              | sort -nr \
              | sed "s/ $_PID//"`

        # Kill all the processes associated with this application.
        if [ "$pids" != "" ]
        then
            num=`wc -w <<< $pids`
            echo "$num child process(es) to kill"
            log "($counter) Child processes to kill: $pids"
            cmd="kill -9 $pids"
            log "$cmd"
            $cmd
        else
            echo "All child processes were killed"
            log "No child processes left to kill"
            break
        fi
    done

    exit
}

##
## Load engine and session configuration data into the local consul key/value
## store.
##
function load_consul()
{
    # Wait to make sure the consul service is up.
    wait_for_service "localhost:8500/v1/status/leader" 60 > /dev/null

    # The consul loader script will be in an sbin/ subdirectory relative to the
    # installation directory.
    consul_loader="$install_dir/sbin/consul_loader"
    
    # If we're in renderdev mode, we need to pass that to the consul_loader so it can use the appropriate options
    # and settings which differ from a normal mode
    renderdev=""
    if [ "$docker_mode" -eq 1 ]; then
        renderdev="--renderdev"
    fi

    # $GIT clone -b "$config_branch" "$SESSION_CONFIG_REPO" "$SESSION_CONFIG_DIR"

    # Set some parameters for retrying the loader script if an error occurs. The
    # max_tries variable determines how many times the loader will attempt to
    # to run and can be adjusted.
    max_tries=3
    status=1
    tries=1
    while [ "$status" -ne 0 ] && [ "$tries" -le "$max_tries" ]
    do
        # Run the consul_loader script and record the exit status.
        $consul_loader --env $stack --empty 2>&1 | tee -a $ARRAS_SH_LOG
        status=${PIPESTATUS[0]}

        if [ "$status" -ne 0 ]
        then
            log "Failed to load Consul data will try again ($tries/$max_tries tries)"
            sleep 1
            ((tries++))
        fi
    done

   #rm -rf "$SESSION_CONFIG_DIR"
}

##
## Log the specified string to the arras_sh.log file.
##
function log()
{
    timestamp=`date "+%Y-%m-%d %H:%M:%S,%N"`
    line="${timestamp:0:23} [arras_sh.$$] $1"
	echo $line >> $ARRAS_SH_LOG
}

##
## Start a new gnome-terminal to launch all the arras-core components in.
##
## We start by launching a new terminal then adding tabs to it for each
## component we want to run beyond the first one. The command that is run is
## actually this script but with different a argument for each component. Each
## tab of the new terminal will be named after the component it's running.
##
function new_window()
{
    log "Launching arras.sh terminal"
    log ""

    # Build the options to create tabs for each running task.
    cmd="gnome-terminal $TERM_OPTIONS"
    tabs=0
    for name in $tasks; do
        if [ $tabs -eq 0 ]; then
           cmd="$cmd --window -e \"$0 $name$global_opts\" --title=\"$name\""
        else
           cmd="$cmd --tab -e \"$0 $name$global_opts\" --title=\"$name\""
        fi
        tabs=$(( tabs + 1))
    done

    # Launch the terminal.
    eval $cmd
}

function no_window()
{
    echo "Running in non-windowed mode"

    # Start up each task.
    for name in $tasks; do
        echo "Starting $name"
        exec $0 $name$global_opts 2>&1 >> /dev/null &
    done
}

##
## Start a new gnome-terminal to launch distributed node processes on hosts
## listed in $node_host_list with each node launched in separate tabs.
##
## To launch the remote node processes, ssh is used. This may sometimes result
## in password prompts on certain hosts at which point your password must be
## entered in each tab prompting for one before the node process can be launched
## on the remote host.
##
## To get around this, you can authorize your public rsa key (assuming you
## have previously already generated a key) with the following commands:
##
##   cat ~/.ssh/id_rsa.pub >> ~/.ssh/authorized_keys 
##   chmod 640 ~/.ssh/authorized_keys
##
function node_window()
{
    echo "Launching nodes"

    # Initialize some variables we'll need to build our remote commands.
    consul_host=`hostname -s`
    node_opts="arras4_node "`echo $global_opts|sed "s/-d /-c $consul_host /g"`
    root_dir="/hosts/$consul_host"`pwd -P`
    ssh_opts="-t -t -x"
    term_opts=`echo $TERM_OPTIONS|sed "s/+50+50/+100+100/g"`

    # Build the options to create tabs for each running node.
    cmd="gnome-terminal $term_opts"
    tabs=0
    for node in `cat $node_host_list`; do
        echo "- $node ..."
        node_cmd="ssh $ssh_opts $node cd $root_dir && $0 $node_opts"
        if [ $tabs -eq 0 ]; then
           cmd="$cmd --window -e \"$node_cmd\" --title=\"node@$node\""
        else
           cmd="$cmd --tab -e \"$node_cmd\" --title=\"node@$node\""
        fi
        tabs=$(( tabs + 1))
    done

    eval $cmd
}


##
## Parse the current working directory into pieces of data we'll need later.
##
function parse_path()
{
    # Get the absolute path to the execution directory.
    exec_dir=`cd \`dirname $0\` && pwd`

    # To get the install directory, just strip the trailing "/bin" from it.
    install_dir=`echo $exec_dir | sed -r "s/\/?bin$//"`
}

##
## Set a global options variable that should be passed to all components when
## launched.
##
function set_global_opts()
{
    global_opts=" -l $node_log_level $node_use_rez $node_over_subscribe $node_exclusive_user $node_exclusive_production $node_ipc_dir -stack $stack "
    if [ $strace_on -eq 1 ]; then
        global_opts="$global_opts --strace"
    fi

    if [ $distributed_on -eq 1 ]; then
        global_opts="$global_opts -d"
    fi
    
    if [ $local_env -eq 1 ]; then
        global_opts="$global_opts -env"
    fi
    
    if [ $use_affinity -eq 0 ]; then
        global_opts="$global_opts -use_affinity 0"
    fi
    
    if [ $use_color -eq 0 ]; then
        global_opts="$global_opts --use-color 0"
    fi
    
    if [ "$node_cores" != "" ]; then
        global_opts="$global_opts -cores $node_cores"
    fi    
    
    if [ "$max_node_memory" != "" ]; then
        global_opts="$global_opts -max_node_memory $max_node_memory"
    fi    
}

##
## Set the environment required for building the java components.
##
function set_java_env()
{
    echo "Setting java development environment"
    export JAVA_HOME=$_JAVA_HOME
    PATH=${JAVA_HOME}/bin:${PATH}

    # In some Bamboo tests, the SecureRandom class that Tomcat uses to
    # generate an id takes a very long time -- 30-45 seconds as opposed to
    # 2 seconds -- so rather than use the default (blocking) /dev/random,
    # specify /dev/urandom instead.
    export JAVA_OPTS="-Djava.security.egd=file:/dev/./urandom"

    # Special-casing when running in Bamboo.
    if [ "BAMBOO_AGENT_HOME" != "" ]; then

        # Unset the LOGGING_CONFIG envar.
        #
        # This gets set in certain Bamboo tests (it looks like torch might be
        # setting it) and ends up pointing to a C++ logging configuration file
        # under /rel/cvs. Unfortunately, this is a variable that Tomcat uses
        # and so the services fail when trying to load the configuration.
        unset LOGGING_CONFIG
    fi
}

##
## Set the environment required by the nodes in order to find required .so's.'
##
function set_node_env()
{
    echo "Setting node environment"
    echo $install_dir
    
    # determine current folios
    core_dir=`grep arras_core $install_dir/.folio/sdks | cut -d " " -f2`
    moonray_dir=`grep arras_moonray $install_dir/.folio/sdks | cut -d " " -f2`    
    messages_dir=`grep arras_messages $install_dir/.folio/sdks | cut -d " " -f2`
    computation_api_dir=`grep arras_computation_api $install_dir/.folio/sdks | cut -d " " -f2`

    #setup paths for libs that are in folios
    CORE=$core_dir
    MOONRAY=$moonray_dir
    COMPUTATION=$computation_api_dir
    COMPONENTS=$install_dir
    MESSAGES=$messages_dir
    RENDERPREP="/rel/folio/arras_render_prep_computation/arras_render_prep_computation-1.0.0-latest"

    # We need to adjust some of the paths if we're going through /hosts.
    path_prefix=""
    if [[ $install_dir == *"/hosts/"* ]]; then
        path_prefix="/"`echo $install_dir|cut -d/ -f2,3`
    fi;

    # Only update the paths if the result of prepending the prefix to them is a
    # valid directory.
    if [ "$path_prefix" != "" ]; then
        echo "Prefixing paths with $path_prefix"

        if [ -d $path_prefix$CORE ]; then
            CORE=$path_prefix$CORE
        fi

        if [ -d $path_prefix$MOONRAY ]; then
            MOONRAY=$path_prefix$MOONRAY
        fi

        if [ -d $path_prefix$COMPUTATION ]; then
            COMPUTATION=$path_prefix$COMPUTATION
        fi

        if [ -d $path_prefix$MESSAGES ]; then
            MESSAGES=$path_prefix$MESSAGES
        fi
    fi;


    # Find paths for interesting directories.
    libee_path="/rel/folio/libee/$LIBEE_VER"
    render_ecosystem_dir=`grep render_ecosystem $install_dir/.folio/sdks | cut -d " " -f2`
    scene_rdl2_path=`grep scene_rdl2 $install_dir/.folio/sdks | cut -d " " -f2`
    lib_path="$install_dir/lib"

    #libs from folios
    libee_lib_path="$libee_path/lib"
    core_lib_path="$CORE/lib"
    moonray_lib_path="$MOONRAY/lib"
    computation_lib_path="$COMPUTATION/lib"
    components_lib_path="$COMPONENTS/lib"
    folios_lib_path="${core_lib_path}:${moonray_lib_path}:${computation_lib_path}:${components_lib_path}"

    # Dso paths from folios
    core_messages_path="$CORE/dso/messages/icc150_64"
    messages_path="$MESSAGES/dso/messages/icc150_64"
    moonray_folio_dso_path="$MOONRAY/dso/icc150_64"
    arras_components_dso_path="$COMPONENTS/dso/icc150_64"
    arras_components_dso_messages="$COMPONENTS/dso/messages"
    render_prep_dso_path="$RENDERPREP/dso/icc150_64"
    folios_dso_path="${moonray_folio_dso_path}:${core_messages_path}:${arras_components_dso_messages}:${arras_components_dso_path}:${render_prep_dso_path}"

    export LD_LIBRARY_PATH="${libee_path}:${libee_lib_path}:${lib_path}:${messages_path}:${LD_LIBRARY_PATH}"

    # the next line sets the dso path environment variable for messages, needed by the node.
    # If arras.sh was called using rez-env those variables are already declared
    if [ $local_env  -eq 1 ]; then
         export ARRAS_DSO_MESSAGES=${messages_path}
         export LD_LIBRARY_PATH="${folios_lib_path}:${folios_dso_path}:${LD_LIBRARY_PATH}"
         export RDL2_DSO_PATH="$MOONRAY/rdl2dso"
         export MOONRAY_DSO_PATH=$RDL2_DSO_PATH
    fi

    export SOFTMAP_PATH="${SOFTMAP_PATH}:${scene_rdl2_path}:${render_ecosystem_dir}:${install_dir}"
    export PATH="${PATH}:${install_dir}/bin:${MOONRAY}/bin:${COMPONENTS}/bin"

    # Lastly, set an environment variable to declare the node's ip address.
    # Some hosts have multiple interfaces so we need to make sure we advertise
    # one that is reachable from outside of the local host.
    export ARRAS_NODE_HTTP_IPADDR=$HOST_IPADDR

    echo ""
    echo "Resolved Environment:"
    echo ""
    env | sort

}

##
## Start a local consul instance.
##
function start_consul()
{
    echo "Starting consul"

    # Shut down any consul server that may have been left over since the last
    # test run.
    stop_consul

    # Create the consul working directories.
    mkdir -p $CONSUL_WORK_DIR/data $CONSUL_WORK_DIR/conf

    # Create the consul config file.
    echo $CONSUL_CONFIG > $CONSUL_WORK_DIR/conf/consul_config.json

    # If we're running in distributed mode, bind to all of the interfaces on the
    # localhost to make the client available to remote nodes.
    client_arg=""    
    if [ $distributed_on -eq 1 ]; then
        client_arg="-client 0.0.0.0"
    fi
    
    # If we're running in docker mode, also make our interface bindable to remote clients can use the UI
    if [ $docker_mode -eq 1 ]; then
        client_arg="-client 0.0.0.0"
    fi

    # Call the consul with the proper configuration.
    client_addr=``
    dc=`whoami`-`hostname -s`
    $CONSUL_DIR/sbin/consul agent $client_arg\
        -dc=$dc \
        -config-file=$CONSUL_WORK_DIR/conf/consul_config.json 2>&1 \
        | tee $CONSUL_LOG
}

##
## Start a local coordinator instance.
##
function start_coordinator()
{
    echo "Starting coordinator"

    # Wait to make sure the consul service is up.
    wait_for_service "localhost:8500/v1/status/leader" 60 > /dev/null

    # Prepare a java environment
    set_java_env

    # Run the coordinator jar file from the /tmp working directory.
    jarfile="$install_dir/lib/java/arras_coordinator.jar"
    cd $WORK_DIR
    ${strace_cmd}java $JAVA_OPTS -jar $jarfile 2>&1 | tee $COORDINATOR_LOG
}

#
# Start a local node instance.
#
function start_node()
{
    echo "Starting node"

    # The node needs to wait for the coordinator to start accepting requests.
    # wait_for_service "localhost:8087" 60
    # NOVADEV-5: node now waits for and gets coordinator IP:port from consul

    if [ "$consul_host" != "" ]; then
        consul_opt="--consul-host $consul_host"
        mkdir -p $WORK_DIR
    fi

    if [ "$node_cores" != "" ]; then
        node_cores_opt="--cores $node_cores"
    fi

    if [ "$max_node_memory" != "" ]; then
        max_node_memory_opt="--max_node_memory $max_node_memory"
    fi

    if [ $use_affinity -eq 0 ]; then
        use_affinity_opt="--use-affinity 0"
    fi

    if [ $use_color -eq 0 ]; then
        use_color_opt="--use-color 0"
    fi

    # Set the required environment variables to run the node and then run it.
    # This is only needed in a local arras.sh session
    if [[ $docker_mode -ne 0 ]] || [[ $local_env -eq 1 ]]; then
    	set_node_env
    fi

    arras4_node --dev -l $node_log_level $consul_opt $use_affinity_opt $use_color_opt $node_cores_opt $max_node_memory_opt $node_use_rez $node_over_subscribe $node_exclusive_user $node_exclusive_production $node_ipc_dir 2>&1 | tee $NODE_LOG
}

##
## Try to gracefully shut down consul.
##
function stop_consul()
{
    # Shut down consul by asking it nicely to leave.
    $CONSUL_DIR/sbin/consul leave 2>&1 > /dev/null
}

##
## Perform some validatetion checks.
##
function validate()
{
    # For now, we only validate available ports.
    check_ports
}

##
## Wait for the specified service to become responsive.
##
function wait_for_service()
{
    # Get the service to ping and the timeout values.
    service=$1
    timeout=$2

    if [ $is_primary -eq 0 ]; then
        echo "Waiting for service at $service ..."
    fi

    # Initialize the status and wait_time then go into a loop where we ping the
    # service each second until we reach the timeout.
    status=1
    wait_time=0
    timeout_error=0
    while [ "$status" -ne 0 ] && [ $wait_time -le $timeout ]
    do
        curl -X GET $service &> /dev/null
        status=$?
        sleep 1
        ((wait_time++))
        if [ $is_primary -eq 0 ]; then
            echo "- wait time is $wait_time secs (timeout at $timeout secs)"
        fi
    done

    # If we timed out, set the timeout_error flag.
    if [ $wait_time -ge $timeout ]; then
        timeout_error=1
    fi
}

##
## Retreive the port number node registered with consul
##
function get_node_port_num()
{
    consul_status_service="http://localhost:8500/v1/status/leader"
    consul_kv_service="http://localhost:8500/v1/kv/"
    consul_keys_url="arras/services/nodes?keys"
    consul_keys_service=$consul_kv_service$consul_keys_url

    # wait for consul to start
    echo "Waiting for arras-core components to start up..."
    wait_for_service $consul_status_service 60 > /dev/null

    # make sure at least one node is available
    wait_for_service $consul_keys_service 60 > /dev/null
    node_wait_time=0
    node_time_out=60
    node_status=1
    HTTP_STATUS=400
    timeout_error=0

    while [ "$HTTP_STATUS" -ne 200 ] && [ "$node_status" -ne 0 ] && [ $node_wait_time -le $node_time_out ]
    do
    http_response=$(curl -f --silent --write-out "HTTPSTATUS:%{http_code}" -X GET $consul_keys_service)

    # extract the body
    http_body=$(echo $http_response | sed -e 's/HTTPSTATUS\:.*//g')

    # extract the status
    HTTP_STATUS=$(echo $http_response | tr -d '\n' | sed -e 's/.*HTTPSTATUS://')

    if [ $HTTP_STATUS -eq 200  ]; then
      node_status=$?
      # check if the node_key_uuid contains the string "info"
      sub_string="info"
      
      if test "${http_body#*$sub_string}" != "$http_body"
      then
        node_key_uuid=$(curl -f -s -X GET $consul_keys_service | json_reformat | grep info | sed '{s/"//g;s/ //g}' )
        
      else
         echo "WARN: Node Key does not contain info, can not get the port number"
         echo "WARN: Check the arras.sh logs for possible errors."   
         exit 1
      fi
      res=$?
      sleep 1
      ((node_wait_time++))

      if [ $res -eq 7 ]; then
        echo "Could not connect to consul"
      elif [ $res -eq 22 ]
      then
           echo "404 error, could not find the resource"
      else
           # build the url to retrieve the port number from consul
           consul_info_service=$consul_kv_service$node_key_uuid
           
           http_response=$(curl --silent --write-out "HTTPSTATUS:%{http_code}" -X GET $consul_info_service\?raw)
           # extract the body
           http_body=$(echo $http_response | sed -e 's/HTTPSTATUS\:.*//g')
           

           # extract the status
           HTTP_STATUS=$(echo $http_response | tr -d '\n' | sed -e 's/.*HTTPSTATUS://')
           
           if [  $HTTP_STATUS -eq 200  ]; then
                sub_string="httpPort"
                if test "${http_body#*$sub_string}" != "$httpPort"
                then
                   
                   node_port_num=$(curl -s -X GET $consul_info_service\?raw | json_reformat | grep httpPort | awk '{print $2}' | sed 's/,//')
                   
                   res=$?
                    if [ $res -eq 7 ]; then
                         echo "Could not connect to consul"
                    elif [ $res -eq 22 ]; then
                         echo "404 error, could not find the resource"
                    fi
                else
                         echo "WARN: The response from consul does not contain portNumber, can not get the port number"
                         echo "WARN: Check the arras.sh logs for possible errors."   
                exit 1
                fi
            fi
      fi
    fi
    done
    # If we timed out, set the timeout_error flag.
    if [ $wait_time -ge $timeout ]; then
        timeout_error=1
    fi
}

##
## Orchestrate the startup of the various ArrasCore components.
##

# Process the command-line arguments:
while [[ $# > 0 ]]; do

    # Get the next argument.
    arg="$1"
    shift

    case $arg in

        -c)
            consul_host=$1
            shift
            ;;
        consul)
            tasks="consul"
            ;;
        coordinator)
            tasks="coordinator"
            ;;
        -d|--dist|--distributed)
            distributed_on=1
            ;;
        -cores)
            node_cores=$1
            shift
            ;;
        -max_node_memory)
            max_node_memory=$1
            shift
            ;;
        -use_affinity)
            use_affinity=$1
            shift
            ;;
        --use-color)
            use_color=$1
            shift
            ;;
        -h|--help)
            help
            exit
            ;;
        kill)
            tasks="kill"
            ;;
        -l)
            node_log_level=$1
            shift
            ;;
        --over-subscribe)
            node_over_subscribe="--over-subscribe" 
            ;;
        --exclusive-user)
            node_exclusive_user="--exclusive-user" 
            ;;
        --exclusive-production)
            node_exclusive_production="--exclusive-production $1"
            shift 
            ;;    
        --ipc-dir)
            node_ipc_dir="--ipc-dir $1"
            shift 
            ;;                      
        --use-rez)
            node_use_rez="--use-rez"
            ;;
        load_consul)
            tasks="load_consul"
            ;;
        node)
            tasks="node"
            ;;
        --no-window)
            do_window=false
            ;;
        --node-list)
            node_host_list=$1
            shift
            ;;
        --strace)
            strace_on=1
            ;;
        -env)
           local_env=1
           echo "Using local environment variables"
           ;;
        -stack)
           stack=$1
           shift;
           ;;
           

        # Throw an error if the user passed in an argument we don't recognize.
        *)
            echo "ERROR: Unsupported argument: $arg"
            exit 1
    esac
done

# If this is the invoking shell, then this is the primary shell and we need to
# run some extra tasks.
if [ "$tasks" == "all" ]; then

    is_primary=1

    # If tasks is "all", replace it with a real list of components. If we're to
    # run in distibuted mode, leave the node out. Otherwise, add all of the
    # arras-core components in the stack.
    if [ $distributed_on -eq 1 ]; then
        tasks="consul load_consul acap coordinator"
    else
        tasks="consul load_consul acap coordinator node"
    fi

    # Perform some setup.
    init

# If this is a component, we'll need to do some prep work as well.
else
    # Parse the current directory into parts we'll need for various functions.
    parse_path

    # If strace is on, initialize the strace command.
    if [ $strace_on -eq 1 ]; then
        strace_cmd="strace -ftto $STRACE_DIR/$tasks.strace "
    fi
fi

# Depending on what argument was passed in, start a different component.
case $tasks in

    consul)
        start_consul
        ;;
    coordinator)
        start_coordinator
        ;;
    node)
        start_node
        ;;
    load_consul)
        load_consul
        ;;
    kill)
        killall
        exit
        ;;
    *)
        # If the DISPLAY is not set, don't attempt to open a window.
        if [ -z "$DISPLAY" ]; then
            echo "DISPLAY not set! Starting in non-window mode."
            do_window=false
        fi

        if [ "$do_window" = true ]; then
            new_window
        else
            no_window
        fi
esac

# If this is not the primary shell, just sit around and spin our wheels to give
# someone a chance to look at the output. Otherwise, the window tab would just
# close after exit.
if [ $is_primary -eq 0 ]; then

    if [ "$tasks" == "load_consul" ]; then
        echo
        echo "Consul load completed."
        echo
    else
        echo
        echo "*** $1 exited (CTRL-C to exit arras.sh) ***"
    fi

    while :
    do
        sleep 10
    done

else
   
    # If we're running in distributed mode, wait for the coordinator instead.
       if [ $distributed_on -eq 1 ]; then
        echo "Running in distributed mode."
        echo "Waiting for arras-core components to start up..."
        service="localhost:8087"
        # If a list of nodes was specifed open a node on each host in the list.
        if [[ ( ! -z "$node_host_list") && ( -f $node_host_list ) ]]; then
            node_window
        fi
       else
        # By default, wait for the node service to become responsive before
        # returning control of the shell.
        get_node_port_num
        service="http://localhost:$node_port_num"
       fi

    # Wait for our target service, and if a timeout occurs, print a warning.
    wait_for_service $service 60
    if [ $timeout_error -eq 0 ]; then
        echo
        echo "Started arras-core components"
    else
        echo "WARN: Got tired of waiting."
        echo "WARN: Check the arras.sh logs for possible errors."
    fi

    if [ "$do_window" = false ]; then
        echo "(CTRL-C to quit)"
        wait
    fi
fi

