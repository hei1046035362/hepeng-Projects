## 前言
配置 Redis 集群的基本要求
主节点：至少 3 个主节点。
从节点：如果设置每个主节点有 1 个从节点，则需要总共 6 个节点（3 主 + 3 从）。

## 前期准备
### 1、创建目录(方便使用docker-compose一次性创建并启动多个容器)
```
mkdir /data/redis-cluster
cd /data/redis-cluster
```
### 2、更新 Docker Compose 文件
确保您的 docker-compose.yml 文件包含 6 个节点，其中 3 个是主节点，3 个是从节点。以下是一个示例配置：
yaml文件内容：
```
version: '3'

services:
  redis-node-1:
    image: redis:7.0
    ports:
      - "7001:6379"
    volumes:
      - ./data/redis-node-1:/data
    command: ["redis-server", "--cluster-enabled", "yes", "--cluster-config-file", "/data/nodes.conf", "--cluster-node-timeout", "5000", "--appendonly", "yes", "--requirepass", "bZSCEI3VyV", "--masterauth", "bZSCEI3VyV"]

  redis-node-2:
    image: redis:7.0
    ports:
      - "7002:6379"
    volumes:
      - ./data/redis-node-2:/data
    command: ["redis-server", "--cluster-enabled", "yes", "--cluster-config-file", "/data/nodes.conf", "--cluster-node-timeout", "5000", "--appendonly", "yes", "--requirepass", "bZSCEI3VyV", "--masterauth", "bZSCEI3VyV"]

  redis-node-3:
    image: redis:7.0
    ports:
      - "7003:6379"
    volumes:
      - ./data/redis-node-3:/data
    command: ["redis-server", "--cluster-enabled", "yes", "--cluster-config-file", "/data/nodes.conf", "--cluster-node-timeout", "5000", "--appendonly", "yes", "--requirepass", "bZSCEI3VyV", "--masterauth", "bZSCEI3VyV"]

  redis-node-4:
    image: redis:7.0
    ports:
      - "7004:6379"
    volumes:
      - ./data/redis-node-4:/data
    command: ["redis-server", "--cluster-enabled", "yes", "--cluster-config-file", "/data/nodes.conf", "--cluster-node-timeout", "5000", "--appendonly", "yes", "--requirepass", "bZSCEI3VyV", "--masterauth", "bZSCEI3VyV"]

  redis-node-5:
    image: redis:7.0
    ports:
      - "7005:6379"
    volumes:
      - ./data/redis-node-5:/data
    command: ["redis-server", "--cluster-enabled", "yes", "--cluster-config-file", "/data/nodes.conf", "--cluster-node-timeout", "5000", "--appendonly", "yes", "--requirepass", "bZSCEI3VyV", "--masterauth", "bZSCEI3VyV"]

  redis-node-6:
    image: redis:7.0
    ports:
      - "7006:6379"
    volumes:
      - ./data/redis-node-6:/data
    command: ["redis-server", "--cluster-enabled", "yes", "--cluster-config-file", "/data/nodes.conf", "--cluster-node-timeout", "5000", "--appendonly", "yes","--requirepass", "bZSCEI3VyV", "--masterauth", "bZSCEI3VyV"]
```

### 3、启动集群
在更改 docker-compose.yml 后，重新启动集群以应用更改：
```
docker-compose down
docker-compose up -d
```
## 创建集群
### 1、重启所有容器
如果docker logs redis-cluster-redis-node-1-1 显示的信息中有No cluster configuration found的话，可能会有问题，就需要重启所有的redis节点，
```
for i in `seq 1 6`;do docker restart redis-cluster-redis-node-$i-1; done
```
### 2、记录每个容器ip
进入每个容器记录下ip
```
docker exec -it redis-cluster-redis-node-1-1 /bin/bash
apt update && apt install iproute2 -y && ip a
```
### 3、建立集群，在容器(随便一个)中输入：
```
redis-cli -a bZSCEI3VyV --cluster create 172.26.0.2:6379 172.26.0.3:6379 172.26.0.4:6379 172.26.0.5:6379 172.26.0.6:6379 172.26.0.7:6379 --cluster-replicas 1
```
如果报错信息中有信息,大概率是集群信息没有清理
进入redis容器，清理集群相关信息，每个ip都要执行一次
```
redis-cli -h 172.26.0.2 -p 6379 -a bZSCEI3VyV
CLUSTER RESET HARD
```
然后重新执行
```
redis-cli -a bZSCEI3VyV --cluster create 172.26.0.2:6379 172.26.0.3:6379 172.26.0.4:6379 172.26.0.5:6379 172.26.0.6:6379 172.26.0.7:6379 --cluster-replicas 1
```

## 验证集群
完成后，您可以通过以下命令在宿主机中检查集群状态：
redis-cli -c -p 7001 cluster info
