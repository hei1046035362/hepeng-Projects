gateway-worker通信流程

client      |       gw     |        bw
        <-->|tcp   8282    |
当前使用 <-->|ws    9282    |
            |tcp   2900    |<---> 当前使用
            |ws    3900    |<--->