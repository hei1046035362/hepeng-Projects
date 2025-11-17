请求 create
{"janus":"create","transaction":"9kvpf3sMaZhT"}
响应
{
    "janus": "success",
    "transaction": "9kvpf3sMaZhT",
    "data": {
        "id": 3858662133750281
    }
}



请求attach
{
    "janus" : "attach",
    "plugin" : "janus.plugin.videoroom",
    "session_id": 6394820540255295,
    "transaction" : "9kvpf3sMaZhT"
}
响应
{
    "janus": "success",
    "session_id": 8861270979574943,
    "transaction": "9kvpf3sMaZhT",
    "data": {
        "id": 8795712852013455
    }
}

请求join:
{
  "janus": "message",
  "session_id": 6394820540255295,
  "handle_id": 6596128584171780,
  "body": {
    "request": "join",
    "room": 1234,
    "ptype": "publisher",
    "display": "123456"
  },
  "transaction": "UEFI9AWON4ox"
}
响应
{
    "janus": "event",
    "session_id": 6394820540255295,
    "transaction": "UEFI9AWON4ox",
    "sender": 6596128584171780,
    "plugindata": {
        "plugin": "janus.plugin.videoroom",
        "data": {
            "videoroom": "joined",
            "room": 1234,
            "description": "Demo Room",
            "id": 3684097038445061,
            "private_id": 3836130111,
            "publishers": [],
            "joined": 1740381375
        }
    }
}