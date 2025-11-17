// 应用层协议类型
enum L4_TYPE
{
	L4_TYPE_HTTP,
	L4_TYPE_WEBSOCK	
};
// 需要对fd进行的操作类型
enum FD_OPT
{
	FD_READ = 1,
	FD_WRITE = 2,
	FD_CLOSE = 4
};

// 客户端需要保留的信息
typedef struct st_cli_info {
	int fd;			// socket fd 						master 填充
	char* cli_id;	// user id 							process 填充
	int need_keep;	// 是否为长连接                     	process填充
	int l4_type;	// 应用层协议类型，http/websocket		process填充
} tgg_cli_info;

std::vector<tgg_cli_info*> vec_cli;// 存储fd对应的客户端信息，fd就是下标，要能进程间共享

// 交互数据的结构，入队列时填充
typedef struct st_opt_data {
	int fd;			// socket fd
	int fd_opt;		// 对fd的操作类型(读写关闭)
	void* data;		// 携带的数据
} tgg_opt_data;