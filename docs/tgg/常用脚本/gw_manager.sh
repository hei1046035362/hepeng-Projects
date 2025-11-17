#!/bin/bash

# 进程启动命令列表（按顺序执行）
PROCESSES=(
  "/data/code/tgg_gateway/adapter/micro_thread/tgg/gwrcv --proc-id=0"
  "/data/code/tgg_gateway/adapter/micro_thread/tgg/gwrcv --proc-id=1"
  "/data/code/tgg_gateway/adapter/micro_thread/tgg/gwcliprc"
  "/data/code/tgg_gateway/adapter/micro_thread/tgg/gwregister"
)

# PID存储文件（用于停止时逆序处理）
PID_FILE="/tmp/gw_processes.pid"

# 启动所有进程（顺序启动，proc-id=0优先）
start() {
  # 先启动 proc-id=0
  nohup ${PROCESSES[0]} > /dev/null 2>&1 &
  local main_pid=$!
  echo "启动主进程 (proc-id=0) PID: $main_pid"
  echo "$main_pid:${PROCESSES[0]}" > $PID_FILE

  # 等待10秒确保主进程就绪
  sleep 10

  # 启动其他进程
  for cmd in "${PROCESSES[@]:1}"; do
    nohup $cmd > /dev/null 2>&1 &
    local pid=$!
    echo "启动进程 [$cmd] PID: $pid"
    echo "$pid:$cmd" >> $PID_FILE
  done
  echo "所有进程已启动！"
}

# 停止所有进程（逆序停止，proc-id=0最后退出）
stop() {
  if [ ! -f "$PID_FILE" ]; then
    echo "PID文件不存在，进程可能未运行"
    return
  fi

  # 从PID文件读取进程列表（逆序处理）
  mapfile -t pids < <(tac $PID_FILE)
  for line in "${pids[@]}"; do
    local pid=${line%%:*}
    local cmd=${line#*:}
    if ps -p $pid > /dev/null; then
      echo "停止进程 [$cmd] PID: $pid"
      kill $pid
      sleep 1
    fi
  done

  # 单独处理 proc-id=0（确保最后退出）
  local main_pid=$(head -n1 $PID_FILE | cut -d: -f1)
  if ps -p $main_pid > /dev/null; then
    echo "停止主进程 (proc-id=0) PID: $main_pid"
    kill $main_pid
  fi

  rm -f $PID_FILE
  echo "所有进程已停止！"
}

# 重启功能（先逆序停止，再顺序启动）
restart() {
  stop
  sleep 3
  start
}

# 根据输入参数执行对应操作
case "$1" in
  start)
    start
    ;;
  stop)
    stop
    ;;
  restart)
    restart
    ;;
  *)
    echo "用法: $0 {start|stop|restart}"
    exit 1
esac