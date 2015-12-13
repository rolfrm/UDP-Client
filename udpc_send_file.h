const char * udpc_file_serve_service_name;
void udpc_file_serve(udpc_connection * c2, void * ptr, char * dir);
void udpc_file_client(udpc_connection * con, int delay, int bufsize, char * in_file_path, char * out_file_path);
// sends the file to the server
void udpc_file_client2(udpc_connection * con, int delay, int bufsize, char * in_file_path, char * out_file_path);
