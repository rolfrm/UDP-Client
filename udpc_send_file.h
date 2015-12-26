const char * udpc_file_serve_service_name;
// read/write file depending on client request.
void udpc_file_serve(udpc_connection *  con, udpc_connection_stats * stats, char * dir);

// reads file
void udpc_file_client(udpc_connection * con, udpc_connection_stats * stats,
		      char * in_file_path, char * out_file_path);
// writes file
void udpc_file_client2(udpc_connection * con, udpc_connection_stats * stats,
		       char * in_file_path, char * out_file_path);
