### 资源服务器
  ssh root@47.129.198.53
### 1、安装Docker：
  在Ubuntu主机上首先需要安装Docker。如果您还没有安装，可以通过以下命令安装：

	sudo apt-get update  
	sudo apt-get install docker.io  
	sudo systemctl start docker  
	sudo systemctl enable docker  

### 启动并安装两个Ubuntu容器：
 使用Docker运行两个Ubuntu容器：

	sudo docker run -itd -p 6379:3306 --name mysql-master ubuntu:22.04  
	sudo docker run -itd -p 6380:3306 --name mysql-slaver ubuntu:22.04 
	-itd 类选项表示交互式（-i）、附加到标准输入（-t）以打开TTY、以守护进程模式运行（-d）。
	--name 为每个容器指定name。



### 进入容器并安装MySQL：
 进入第一个容器并安装MySQL主服务器： 

sudo docker exec -it mysql-master /bin/bash  
apt-get update  
apt-get install mysql-server  
配置MySQL服务器（根据需要修改）：

apt install vim -y

修改主配置文件
/etc/mysql/mysql.conf.d/mysqld.cnf
主
[mysqld]
server-id = 1
log_bin = mysql-bin
binlog_do_db = your_database_name  # 替换为需要同步的数据库名称，主从服务器都要手动创建这个db才行
require_secure_transport = OFF  # 暂时关掉ssl认证，否则从服务器会认证失败，后续有需要可改为安全的ssl认证

从
[mysqld]
server-id = 2
// 设置服务id

service mysql restart
  若重启失败，执行：
  service mysql stop && usermod -d /var/lib/mysql/ mysql && service mysql start


## 修改mysql的密码
通过/etc/mysql/debian.cnf中找到的账号密码
执行命令
mysql -udebian-sys-maint -p
然后输入配置文件中显示的密码，然后执行
use mysql;
update user set authentication_string='' where user='root';--将字段置为空 
ALTER user 'root'@'localhost' IDENTIFIED BY 'CYwPZx4ReW';--修改密码为CYwPZx4ReW




### 修改root账号密码
service mysql stop
mysqld_safe --skip-grant-tables &
mysql -uroot -p
flush privileges; -- 不执行这句可能会修改密码失败
// 账号密码要符合密码等级要求，这里没有执行mysql_secure_installation去设置了，所以在密码末尾加了  1.  连个字符以适配
ALTER USER 'root'@'localhost' IDENTIFIED WITH mysql_native_password BY 'CYwPZx4ReW';
SELECT user, host, authentication_string FROM mysql.user;  -- 查看是否修改成功
exit退出，然后退出安全启动模式killall mysqld，重启mysql
再执行mysql -uroot -p的时候就必须要输入正确的密码了


## 修改密码等级

SHOW VARIABLES LIKE 'validate_password%'; -- 查看有效密码的各个限制
若未显示，执行命令安装模块
INSTALL PLUGIN validate_password SONAME 'validate_password.so';
SET GLOBAL validate_password.policy = 'MEDIUM'; -- 中等校验
SET GLOBAL validate_password.mixed_case_count = 1;  -- 至少一个大写和一个小写字母
SET GLOBAL validate_password.number_count = 1;       -- 至少一个数字
SET GLOBAL validate_password.special_char_count = 0; -- 至少一个特殊字符
有的环境是
SET GLOBAL validate_password_number_count = 0;
SET GLOBAL validate_password_special_char_count = 0;



### 创建复制账号，主从服务器都要创建
CREATE USER 'teamgaga'@'%' IDENTIFIED BY 'CYwPZx4ReW';
GRANT REPLICATION SLAVE ON *.* TO 'teamgaga'@'%';
FLUSH PRIVILEGES;

### 从服务器需要
	在主服务器中执行SHOW MASTER STATUS;获取file(master_log_file)和position的值
	+------------------+----------+--------------+------------------+-------------------+
	| File             | Position | Binlog_Do_DB | Binlog_Ignore_DB | Executed_Gtid_Set |
	+------------------+----------+--------------+------------------+-------------------+
	| mysql-bin.000001 |      157 | test_db      |                  |                   |
	+------------------+----------+--------------+------------------+-------------------+

CHANGE MASTER TO
    MASTER_HOST='172.17.0.2',
    MASTER_USER='teamgaga',
    MASTER_PASSWORD='CYwPZx4ReW',
    MASTER_LOG_FILE='mysql-bin.000001',
    MASTER_LOG_POS=157;
然后启动同步进程
START REPLICA;
SHOW SLAVE STATUS;-- 查看Slave_IO_Running Slave_SQL_Running 两个字段是否为yes，是就可以正常使用了

测试复制：
-- 在主服务器上
CREATE DATABASE test_db;
USE test_db;
CREATE TABLE test_table (id INT PRIMARY KEY, name VARCHAR(50));
INSERT INTO test_table (id, name) VALUES (1, 'test_name');
然后，在从服务器上查询该数据库和表，确认数据是否已成功复制：

-- 在从服务器上
USE test_db;
SELECT * FROM test_table;


修改root账号的host为%
SELECT user, host FROM mysql.user WHERE user = 'root';
UPDATE mysql.user SET host='%' WHERE user='root' AND host='localhost';