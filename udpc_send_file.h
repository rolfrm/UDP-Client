const char * udpc_file_serve_service_name;
void udpc_file_serve(udpc_connection * c2, void * ptr);
void udpc_file_client(udpc_connection * con, int delay, int bufsize, char * in_file_path, char * out_file_path);
