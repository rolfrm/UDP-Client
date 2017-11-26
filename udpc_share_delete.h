// This is purely for reliably deleting a file on the server side.
// on the local side its easy to correct when the dir has changed
// but some kind of protocol is needed to tell the server to delete a file.
extern const char * udpc_delete_service_name;

// Serve the possibility of deleting a file
bool udpc_delete_serve(udpc_connection * con, char * basedir);

// Client code to tell the server to delete a file.
void udpc_delete_client(udpc_connection * con, char * file_to_delete);
