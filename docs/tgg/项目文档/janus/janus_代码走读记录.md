web端videoroom无法重复start stop，
    因为在tgg代码(janus_videoroom_leave_or_unpublish)中stop的时候把room给destroy掉了，janus自身是不destroy房间的
    
    
attach操作，创建并绑定ice处理线程


tsn：epool+线程  处理函数为in_event


ice的in_event:
    janus_ice_in_event
    nice_new_event

事件轮训处理，轮训遍历事件插件hash表，逐个处理，有多个事件插件时，会复制消息体，只有一个时直接传递
events的in_event:  
    janus_events_handler_in_event
    
    
    
tsn

tsn_thread_ctx

poller   epool_fd
slots   
    mailbox
    event
    handler
    poller   epool_fd
    slot_id
slot_size  slots的数量

poller_event_t
    in_event       read事件
    out_event      write事件
    timer_event    定时任务
    in_user_data   事件参数 
    out_user_data
    timer_user_data
    
    
configure发送完offersdp收到ack后不等待answer sdp，就会开始发送trickles，所以sdp和trickle在后台是并行处理的


configure ok之后  发送一个get请求获取webrtcup的命令



joined
    这中间会收到一个leaving   why?
configured  客户端过了很久才收到这个消息
....
   post  trickle  completed:ture 之后会收到一个webrtcup
webrtcup