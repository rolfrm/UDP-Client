typedef struct _udpc_connection udpc_connection;

// Client API
// Creates a user on the host, returns an invalid connection if the user already exists.
udpc_connection udpc_create_user(const char * service);

// Adds a public key to the user, making it possible to access the same user using different keys.
void udpc_add_pubkey(udpc_connection client, const char * pubkey);

// Remvoes a public key. How?
void udpc_remove_pubkey(udpc_connection client);

// Logs into the service, storing the IP/port on the server.
udpc_connection * udpc_login(const char * service);

// Logs out of the service
void udpc_logout(udpc_connection * con);

// Connects a client to a service.
udpc_connection * udpc_connect(const char * service);

// Accepts a udpc connection.
udpc_connection * udpc_listen(udpc_connection * con);

// Sends data across the connection.
void udpc_send(udpc_connection * client, void * buffer, size_t length);

// Receives data from the connection.
size_t udpc_receive(udpc_connection * client, void * buffer, size_t max_size);

// Server API
void udpc_start_server(char * local_address);
