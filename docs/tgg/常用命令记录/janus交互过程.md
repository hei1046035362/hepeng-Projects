完整的信令交换过程，参考：https://zhuanlan.zhihu.com/p/384777785

开启房间

创建session
接口：http://192.169.26.157:8088/janus
参数：{"janus":"create","transaction":"9kvpf3sMaZhT"}
返回：
{
  "janus": "success",
  "transaction": "9kvpf3sMaZhT",
  "data": {
    "id": 8764145571427419
  }
}

为这个session创建一个唯一的处理句柄编号，  ws需要把session_id写进参数中
接口:http://192.169.26.157:8088/janus/8764145571427419
参数：
{
  "janus": "attach",
  "plugin": "janus.plugin.videoroom",
  "opaque_id": "videoroomtest-OST4ws2sy4Xn",
  "transaction": "vZKwhQapPAaz"
}
返回：
{
  "janus": "success",
  "session_id": 8764145571427419,
  "transaction": "vZKwhQapPAaz",
  "data": {
    "id": 2963917153219219
  }
}

加入房间：
http://192.169.26.157:8088/janus/8764145571427419/2963917153219219
参数：
{
  "janus": "message",
  "body": {
    "request": "join",
    "room": 1234,
    "ptype": "publisher",
    "display": "123456"
  },
  "transaction": "UEFI9AWON4ox"
}
返回：
{
  "janus": "ack",
  "session_id": 8764145571427419,
  "transaction": "UEFI9AWON4ox"
}




心跳：
get接口
http://192.169.26.157:8088/janus/8764145571427419?rid=1735817068602&maxev=10
返回：
[
   {
      "janus": "keepalive"
   }
]





测试地址：
http://54.177.152.244
https://54.177.152.244
ws://54.177.152.244:8061
wss://54.177.152.244:8063




janus 部署在宿主机还是在容器里面更合适？最终部署是不是会单独放在一台设备上？
如果部署在容器中，则需要映射中继端口，中继端口本身是可配置的，会导致turn服务配置不太灵活。
如果全部部署在宿主机里面，则会有安全风险。


踢人
请求：
{
  "janus": "message", 
  "transaction": "lMWlAeyf3sFJ",
  "session_id": 1405504300044760,
  "handle_id": 6874020784569605,
  "body": {
    "request": "kick",
    "room": 1234,
    "secret": "adminpwd",
    "id": 7463353948953471
  }
}

janus会推送一个消息
[
   {
      "janus": "event",
      "session_id": 1840568176512251,
      "sender": 2451536478556918,
      "plugindata": {
         "plugin": "janus.plugin.videoroom",
         "data": {
            "videoroom": "event",
            "room": 1234,
            "leaving": "ok",
            "reason": "kicked"
         }
      }
   }
]




{
        "janus" : "attach",
        "plugin" : "<the plugin's unique package name>",
        "transaction" : "<random string>"
}