typedef struct _ssl_server ssl_server;
typedef struct _ssl_server_client ssl_server_client;
typedef struct _ssl_server_con ssl_server_con;
typedef struct _ssl_client ssl_client;


ssl_server * ssl_setup_server(int fd);
ssl_server_con * ssl_server_accept(ssl_server_client * scli, int fd);
size_t ssl_server_read(ssl_server_con * con, void * buffer, size_t buffer_size);
void ssl_server_write(ssl_server_con * con, const void * buffer, size_t buffer_size);
struct sockaddr_storage ssl_server_client_addr(ssl_server_client * cli);
void ssl_server_heartbeat(ssl_server_client * cli);
void ssl_server_close(ssl_server_client * cli);
void ssl_server_cleanup(ssl_server * server);
ssl_server_client * ssl_server_listen(ssl_server * serv);
ssl_client * ssl_start_client(int fd, struct sockaddr * remote_addr);
void ssl_client_write(ssl_client * cli, const void * buffer, size_t length);
size_t ssl_client_read(ssl_client * cli, void * buffer, size_t length);
void ssl_client_heartbeat(ssl_client * cli);
void ssl_client_close(ssl_client * cli);
