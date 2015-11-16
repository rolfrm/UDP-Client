typedef struct _udpc_connection udpc_connection;

// Client API
udpc_connection udpc_login(const char * service);
udpc_connection udpc_connect(udpc_connection client, const char * service);
void udpc_send(udpc_connection client, void * buffer, size_t length);
size_t udpc_receive(udpc_connection client, void * buffer, size_t max_size);

// Server API
void udpc_serve();
