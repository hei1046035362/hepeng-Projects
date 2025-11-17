# 编译前需要设置宏
export PKG_CONFIG_PATH=/usr/local/lib/x86_64-linux-gnu/pkgconfig
export FF_PATH=/data/code/tgg_gateway


# 编译带dpdk和openssl的单个g++编译命令
export PKG_CONFIG_PATH=/usr/local/lib/x86_64-linux-gnu/pkgconfig
g++ -g encrypt.cpp -o encrypt -lssl -lcrypto $(pkg-config --static --libs libdpdk)

g++ -g ./packtest.cpp comm/Encrypt.o comm/RedisClient.o tgg_comm/tgg_bw_cache.o tgg_comm/tgg_common.o tgg_comm/tgg_bwcomm.o dpdk_init.o tgg_comm/tgg_lock.o -lstdc++ -lhiredis -I./ -L../ -lmt -lfstack -L${FF_PATH}/lib $(pkg-config --static --libs libdpdk) -lrt -lm -ldl -lcrypto -lssl -lz



# 检查安装路径（通常为 /usr/bin/gcc-12）
which gcc-12

# 设置符号链接（可选）
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-12 100
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-12 100

sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-11 100
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-11 100


API URL
https://free.v36.cm
API KEY
sk-D5egKGvoyXguzSuE7bB8B3D8Ee0b4eA58c11FeFaAd2e6618



git config --global --get http.proxy
git config --global --get https.proxy

git config --global http.proxy tt.teamgaga.com:443  // v2rayn上配置的代理服务器地址和端口

git config --global --unset https.proxy


git reset --hard&& git fetch --all && git checkout tgg_gateway&& git branch -D tgg_debug && git checkout tgg_debug
git reset --hard&& git fetch --all && git checkout tgg_debug&& git branch -D tgg_gateway && git checkout tgg_gateway


nginx-debug编译
./configure --with-debug  --prefix=/usr/local/nginx-debug  --add-module=src/websocket  --with-cc-opt="-O0 -g"  --with-ld-opt="-g" 

vim objs/Makefile
# 找到 CFLAGS 行，确保包含：
CFLAGS = -pipe -O0 -g -W -Wall -Wpointer-arith -Wno-unused-parameter -Werror -g

# 找到 LINK 行，确保包含：
LINK = $(CC) -g