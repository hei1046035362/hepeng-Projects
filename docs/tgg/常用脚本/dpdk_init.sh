# 加载驱动
modprobe uio
insmod /usr/src/linux-headers-$(uname -r)/extra/dpdk/igb_uio.ko
# 网口驱动更换(内核驱动换igb_uio)
eth1_pci=$(/usr/local/bin/dpdk-devbind.py -s|grep "eth1"|awk '{print $1}')
ifconfig eth1 down
/usr/local/bin/dpdk-devbind.py -u eth1
/usr/local/bin/dpdk-devbind.py -b igb_uio $eth1_pci
# 分配大页
echo 1024 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages
