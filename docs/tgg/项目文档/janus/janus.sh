#!/bin/bash -e

# singleton
PID_FILE="/var/run/janus.pid"
log_dir="/var/log/janus"

if [ -f $PID_FILE ]; then
    PID=$(cat $PID_FILE)
    if ps -p $PID > /dev/null; then
        exit 1
    fi
fi

echo $$ > $PID_FILE

start_services() {
    # run turnserver
    # sudo nohup turnserver -L 0.0.0.0 --min-port 50000 --max-port 60000  -a -u root:1 -v -f -r 54.177.152.244 > /dev/null 2>&1 &
    # turnserver_pid=`$?`

    turnserver_name="turnserver"
    if ! pgrep -f $turnserver_name > /dev/null; then
        turnserver -c /etc/turnserver.conf -o
    fi
    #turnserver_pid=`pidof turnserver`

    check_count=0
    check_interval=5
    max_checks=3

    # check if turnserver runs normal
    while [ $check_count -lt $max_checks ]; do
        if ! ps -p `pidof turnserver` > /dev/null; then
            echo "start turnserer failed"
            break
        fi

        echo "turnserver is running, checked times: $check_count"
        check_count=$((check_count + 1))
        sleep $check_interval
    done

    # run janus
    if [ $check_count -eq $max_checks ]; then
        echo "turnserver runs normal, start run janus."
        # sudo nohup /opt/tgg_janus/bin/janus --debug-level=5 --log-file="/var/log/janus/janus.log" > /dev/null 2>&1 &
        /opt/tgg_janus/bin/janus -b --debug-level=5 --log-file="/var/log/janus/janus.log"
    fi
}

kill_process() {
    local process_name="$1"

    local pids=$(pidof $process_name)

    if [ -n "$pids" ]; then
        echo "try to kill $process_name : $pids"
	local pid_array=($pids)
        for pid in "${pid_array[@]}"; do
	    echo "kill $pid"
            kill $pid
        done
        sleep 2
        remaining_pids=$(pidof $process_name)
        if [ -n "$remaining_pids" ]; then
            echo "try to force kill $process_name"
            remaining_pid_array=($remaining_pids)
            for remaining_pid in "${remaining_pid_array[@]}"; do
                kill -9 $remaining_pid
		echo "kill -9 $process_name:$remaining_pid"
            done
        else
            echo "all process of $process_name stoped normally."
        fi
    else
        echo "$process_name stoped success."
    fi
}

#kill_process "turnserver"

stop_services() {
    kill_process "janus"
    kill_process "turnserver"
}

print_help() {
    echo "usage:"
    echo "$0 start"
    echo "$0 stop"
    echo "$0 restart"
}

input_param=$1

if [ ! -d "$log_dir" ]; then
    mkdir -p "$log_dir"
fi

if [ "$input_param" = "start" ]; then
    start_services
elif [ "$input_param" = "stop" ]; then
    stop_services
elif [ "$input_param" = "restart" ]; then
    stop_services
    start_services
else
    print_help
fi
