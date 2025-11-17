1、Janus WebRTC Server负载均衡方案：高可用部署: https://blog.csdn.net/gitblog_00760/article/details/151277137
2、janus-cloud方案：https://github.com/OpenSight/janus-cloud/tree/master#
外网：pip install --user ./janus-cloud/
国内：pip install --user -i https://pypi.tuna.tsinghua.edu.cn/simple ./janus-cloud/
pip install --user -i  https://mirrors.aliyun.com/pypi/simple ./janus-cloud/


pip install --force-reinstall --no-cache-dir zope.event  -i  https://mirrors.aliyun.com/pypi/simple

哨兵运行：
janus-sentinel /home/hepeng/.local/opt/janus-cloud/conf/janus-sentinel.yml
nohup janus-sentinel /usr/local/opt/janus-cloud/conf/janus-sentinel.yml > /dev/null 2>&1 &
代理运行：

3、自研方案
janus上线后，后台会拿到这个区域的所有jauns的ip,由后台告诉客户端应该用哪个ip，客户端再发起视频连接



pip install /data/code/janus-cloud
cp  /home/apps/.local/opt/janus-cloud/conf{.bak/*,/}
nohup /home/apps/.local/bin/janus-proxy /home/apps/.local/opt/janus-cloud/conf/janus-proxy.yml > /dev/null 2>&1 &





