typedef struct _udpc_connection udpc_connection;
typedef struct _udpc_server udpc_server;
typedef struct _udpc_service udpc_service;
extern int udpc_server_port;
// Client API
// Creates and logs into the service described by , storing the IP/port on the server.
// From now on the port should be kept open by sending empty
// UDP packets to the server.
udpc_service * udpc_login(const char * service_url);

// Logs out of the service.
void udpc_logout(udpc_service * con);

// Sends hardbeat message to server.
void udpc_heartbeat(udpc_service * con);

// Connects a client to a service.
udpc_connection * udpc_connect(const char * service_url);

// Accepts a udpc connection.
udpc_connection * udpc_listen(udpc_service * con);

// Sends data across the connection.
void udpc_write(udpc_connection * client, const void * buffer, size_t length);

// Receives data from the connection.
int udpc_read(udpc_connection * client, void * buffer, size_t max_size);

// Reads data from the connection without removing it from the buffer.
int udpc_peek(udpc_connection * client, void * buffer, size_t max_size);

// Returns the number of bytes in the next package in the buffer.
int udpc_pending(udpc_connection * client);

void udpc_set_timeout(udpc_connection * client, int us);
int udpc_get_timeout(udpc_connection * client);


// Closes the connection.
void udpc_close(udpc_connection * con);

// Pushes an error on top of the udpc error stack.
void udpc_push_error(const char * error);

// Pops an error from the error stack. It has to be freed by the caller.
char * udpc_pop_error();

// Server APx
void udpc_start_server(const char * local_address);
