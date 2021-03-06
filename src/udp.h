struct sockaddr_storage udp_get_addr(const char * remote_address, int port);
int udp_connect(struct sockaddr_storage * local, struct sockaddr_storage * remote, bool socket_reuse);
int udp_open(struct sockaddr_storage * local);
void udp_close(int fd);
int udp_get_port(int fd);
