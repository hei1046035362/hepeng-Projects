*** 参考文档：https://blog.csdn.net/weixin_51718990/article/details/140773278

### 安装janus依赖的开发包
apt install libmicrohttpd-dev libjansson-dev \
	libssl-dev libsofia-sip-ua-dev libglib2.0-dev \
	libopus-dev libogg-dev libcurl4-openssl-dev liblua5.3-dev \
	libconfig-dev pkg-config libtool automake meson ninja-build sudo \
	libevent-dev vim git wget


### 设置预编译宏
export PKG_CONFIG_PATH=/usr/local/lib/x86_64-linux-gnu/pkgconfig:/usr/lib/x86_64-linux-gnu/pkgconfig:

手动安装libnice
git clone https://gitlab.freedesktop.org/libnice/libnice
cd libnice
meson --prefix=/usr build && ninja -C build && sudo ninja -C build install
cd ..

### 手动安装libsrtp
wget https://github.com/cisco/libsrtp/archive/v2.2.0.tar.gz
tar xfv v2.2.0.tar.gz
cd libsrtp-2.2.0
./configure --prefix=/usr --enable-openssl
make shared_library && sudo make install
cd ..

### 安装websocket
git clone https://libwebsockets.org/repo/libwebsockets
cd libwebsockets
#If you want the stable version of libwebsockets, uncomment the next line
# git checkout v4.3-stable
mkdir build
cd build
#See https://github.com/meetecho/janus-gateway/issues/732 re: LWS_MAX_SMP
#See https://github.com/meetecho/janus-gateway/issues/2476 re: LWS_WITHOUT_EXTENSIONS
cmake -DLWS_MAX_SMP=1 -DLWS_WITHOUT_EXTENSIONS=0 -DCMAKE_INSTALL_PREFIX:PATH=/usr -DCMAKE_C_FLAGS="-fpic" ..
make && sudo make install
cd ../..


### 编译janus
sh autogen.sh
./configure --prefix=/opt/janus --enable-websockets
make && sudo make install


### 生成默认配置文件
cd /opt/janus/etc/janus
#拷贝文件
sudo cp janus.jcfg.sample janus.jcfg
sudo cp janus.transport.http.jcfg.sample janus.transport.http.jcfg
sudo cp janus.transport.websockets.jcfg.sample janus.transport.websockets.jcfg
sudo cp janus.plugin.videoroom.jcfg.sample janus.plugin.videoroom.jcfg
sudo cp janus.transport.pfunix.jcfg.sample janus.transport.pfunix.jcfg
sudo cp janus.plugin.streaming.jcfg.sample janus.plugin.streaming.jcfg
sudo cp janus.plugin.recordplay.jcfg.sample janus.plugin.recordplay.jcfg
sudo cp janus.plugin.voicemail.jcfg.sample janus.plugin.voicemail.jcfg
sudo cp janus.plugin.sip.jcfg.sample janus.plugin.sip.jcfg
sudo cp janus.plugin.nosip.jcfg.sample janus.plugin.nosip.jcfg
sudo cp janus.plugin.textroom.jcfg.sample  janus.plugin.textroom.jcfg
sudo cp janus.plugin.echotest.jcfg.sample janus.plugin.echotest.jcfg
sudo cp janus.plugin.audiobridge.jcfg.sample janus.plugin.audiobridge.jcfg

### 生成私有证书
mkdir cert
cd cert
openssl genrsa -out key.pem 2048  # 私钥
openssl req -new -x509 -key key.pem -out cert.pem -days 1095  # 证书
#一直按回车即可

### 安装nginx(可选)
wget https://nginx.org/download/nginx-1.26.2.tar.gz  # 下载压缩包
tar xvzf nginx-1.26.2.tar.gz  # 解压
cd nginx-1.26.2/  # 进入
./configure --with-http_ssl_module  # 配置支持https
make && sudo make install  # 编译和安装    

### 安装和启动turnserver穿透服务器
wget http://coturn.net/turnserver/v4.5.0.7/turnserver-4.5.0.7.tar.gz
tar xfz turnserver-4.5.0.7.tar.gz
cd turnserver-4.5.0.7
./configure 
make && sudo make install

### 各个程序的配置填写请参考文档：https://blog.csdn.net/weixin_51718990/article/details/140773278


### 配置
#### janus配置
/opt/janus/etc/janus.jcfg
stun_server = "172.17.0.2"  # 容器的内网地址

nat_1_1_mapping = "54.177.152.244" # 容器的公网地址

turn_server = "172.17.0.2"
turn_user = "root"
turn_pwd = "1"
#其余配置参考：https://blog.csdn.net/weixin_51718990/article/details/140773278


## 启动  xxx:123456是当前系统的登录账号
sudo nohup turnserver -L 0.0.0.0 --min-port 50000 --max-port 60000  -a -u root:1 -v -f -r nort.gov &


# 启动nginx
/usr/local/nginx/sbin/nginx
# 启动turnserver
sudo nohup turnserver -L 0.0.0.0 --min-port 50000 --max-port 60000  -a -u root:1 -v -f -r nort.gov &

### 稍等几秒，等前面的启动完成再启动janus，同时启动会有概率janus起不来
# 启动janus
/opt/janus/bin/janus --debug-level=5

killall turnserver
killall nginx
killall janus


sudo nohup turnserver -L 54.177.152.244 --min-port 50000 --max-port 60000  -a -u root:1 -v -f -r nort.gov &

./turnserver -L 0.0.0.0 --relay-ip "172.17.0.3" external-ip "54.177.152.244"  --min-port 40000 --max-port 60000  -a -u root:1 -v -f -r nort.gov

sudo nohup turnserver -L 0.0.0.0 --min-port 50000 --max-port 60000  -a -u root:1 -v -f -r 54.177.152.244 &

容器化部署

docker pull ubuntu:22.04

docker run -itd \
    --name janus_container \
    -p 8088:8088 \
    -p 8188:8188 \
    -p 8089:8089 \
    -p 8989:8989 \
    ubuntu:22.04
	
docker exec -it janus_container bash

apt-get update
apt-get install -y build-essential libssl-dev libsrtp2-dev libsofia-sip-ua-dev libglib2.0-dev libopus-dev libogg-dev libcurl4-openssl-dev liblua5.3-dev libmicrohttpd-dev pkg-config gengetopt libtool automake cmake git libevent-dev

git clone https://github.com/meetecho/janus-gateway.git

http:8088
https:8089
ws:8188
wss:8989

    -v /opt/janus/etc/janus:/opt/janus/etc/janus \

docker run -itd \
    --name janus_dev \
    -v /data/janus:/data/janus/expose \
    -p 3478:3478 \
    -p 8060:8088 \
    -p 8061:8188 \
    -p 8062:8089 \
    -p 8063:8989 \
    janus_container:v1.0
    
    
docker run -itd \
    --name janus_dev \
    -v /data/janus:/data/janus/expose \
    --net=host -d \
    -p 3478:3478 \
    -p 3478:3478/udp \
    janus_container:1.0




docker run -itd \
    --name turnserver \
    --net=host -d \
    janus:1.0


docker run -itd \
    --name janus_dev \
    -v /data/janus:/data/janus/expose \
    janus:1.0

edge://flags/#unsafely-treat-insecure-origin-as-secure






同一个容器中启动：
1、容器：
docker run -itd \
    --name janus_dev \
    -v /opt/janus:/data/janus/expose \
    -p 3478:3478 \
    -p 8060:8088 \
    -p 8061:8188 \
    -p 8062:8089 \
    -p 8063:8989 \
    tgg-janus:latest

2、settings.json里面要把端口8088改成8060，8089改成8062

3、turnserver  启动
sudo nohup turnserver -L 0.0.0.0 --min-port 50000 --max-port 60000  -a -u root:1 -v -f -r 54.177.152.244 &

4、janus.jcfg 
turn_server 172.17.0.2
stun_server 172.17.0.2
nat_1_1_mapping = "54.177.152.244"

5、janus 启动
/opt/janus/bin/janus --debug-level=5

janus配置文件详解：https://blog.csdn.net/tanningzhong/article/details/89394192


建议配置：
8核16线程  16G或32G  500G存储空间  带宽至少上下行9Mbps，建议带宽100Mbps上下行对称带宽


docker run -itd \
    --name janus \
    -v /data/janus:/data/janus/expose \
    -p 3478:3478 \
    -p 8060:8088 \
    -p 8061:8188 \
    -p 8062:8089 \
    -p 8063:8989 \
    janus_container:v1.0






自用设备上配置：
docker run -itd \
    --name janus_dev \
    -v /data/janus:/data/janus/expose \
    -p 3478:3478 \
    -p 8060:8088 \
    -p 8061:8188 \
    -p 8062:8089 \
    -p 8063:8989 \
    janus:1.0
    
    





go环境搭建错误解决：
gstream的问题：sudo apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev