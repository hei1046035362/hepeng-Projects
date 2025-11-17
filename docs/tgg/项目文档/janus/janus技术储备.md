webrtc指南：https://webrtc.mthli.com/connection/ice-connection-sorting/
    介绍P2P  ICE交互过程 SDP会话描述介绍等
    



attach消息会返回一个 id和sessionid
sender:3739180264730271
session_id:3234296967596975
room:1234
id:6733495644514596
private_id:1479463826

janus.c:





    
janux自定义事件标签
TGG_USER_EVENT



混音降噪



rtp:  传输音视频数据
rtcp: 传输音视频控制数据，如统计数据，基本都是定时发送
sctp: 流控制，点到点之间会有多个连接，根据需求(比如网络流畅度)进行切换不同的连接传送数据
sdp： 媒体类型，音视频编解码信息，ICE和STUN信息,dtls指纹等，    消息体封装在jsep中


trickle的作用是上报candidate信息(可选连接地址)，上报完成后会发送一条complete的trickle信息



videoroom中的helper：用来同时给多个连接发送音视频数据，遍历订阅者(subscribers)，逐个发送rtp数据包


svc:  自适应比特率，多视图传输，质量分层    还没有调试过，不知道具体怎么用

REMB 反馈接收端当前网络状况的消息，主要用于告知发送端接收端能够处理的最大比特率   用途自适应比特率控制,网络拥塞检测,优化视频质量

PLI  指示接收端丢失视频关键帧的信令消息  用途：恢复视频解码，提高视频稳定性


nohup turnserver -L 0.0.0.0 --min-port 50000 --max-port 60000  -a -u root:1 -v -f -r 54.177.152.244 > /dev/null 2>&1 &

--enable-data-channels

./configure --prefix=/opt/janus --enable-websockets --enable-data-channels 

CFLAGS="-Og -g3 -ggdb3 -fno-omit-frame-pointer" ./configure  --prefix=/opt/tgg_janus --enable-websockets --disable-plugin-echotest --disable-plugin-recordplay --disable-plugin-sip --disable-plugin-videocall --disable-plugin-nosip --disable-plugin-streaming --disable-plugin-textroom --disable-plugin-voicemail --enable-json-logger --enable-kafkamq-event-handler

turnserver -c /etc/turnserver.conf -o
/usr/local/bin/janus -b --debug-level=7 --log-file=/var/log/janus.log --stun-server=54.177.152.244:3478

/opt/tgg_janus/bin/janus --debug-level=5 --log-file=/var/log/janus.log --stun-server=54.177.152.244:8059

/opt/tgg_janus/bin/janus --debug-level=5 --log-file=/var/log/janus.log --stun-server=192.169.26.189:8059

relay-threads=40
listening-device=ens5
relay-device=ens5
listening-port=3478
tls-listening-port=5349
lt-cred-mech
min-port=30000
max-port=65000
#realm=webrtcTest
#no-loopback-peers
#no-multicast-peers
mobility
#cert=/etc/turn_server_cert.pem
cert=/etc/nginx/ssl/teamgaga.com.pem
#pkey=/etc/turn_server_pkey.pem
pkey=/etc/nginx/ssl/teamgaga.com.key
fingerprint
user=root:1
no-cli
external-ip=172.31.9.203
listening-ip=54.177.152.244
realm=janus-dev.teamgaga.com
relay-ip=54.177.152.244
pidfile=/var/run/turnserver.pid
dh2066





1、信令和流媒体分开
2、级联，集群
3、av可靠性传输，引入webrtc
4、质量检测   首帧延迟，卡顿率，ice的成功率


退出前，房间id列表落盘，重启后先发送一下房间id列表
guild_id  改成community_id 
janus ip写入redis
只有禁言，没有禁视频
开启 core 配置中的 reclaim_session_timeout
    videorom配置中的audiolevel_event、audio_active_packets、audio_level_average 
