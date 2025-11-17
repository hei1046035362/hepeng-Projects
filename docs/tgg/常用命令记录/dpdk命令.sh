# 加载驱动
modprobe uio
insmod /usr/src/linux-headers-5.15.0-122-generic/extra/dpdk/igb_uio.ko
# 网口驱动更换(内核驱动换igb_uio)
ifconfig eth1 down
/usr/local/bin/dpdk-devbind.py -u eth1
/usr/local/bin/dpdk-devbind.py -b igb_uio eth1
# 分配大页
echo 1024 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages


# 查看网口收发包信息
/usr/local/bin/dpdk-proc-info -- 0xff --xstats
