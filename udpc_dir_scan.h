// requires 

typedef struct {
  unsigned char md5[16]; //16 = MD5_DIGEST_LENGTH
}udpc_md5;

typedef struct{
  char ** files;
  udpc_md5 * md5s;
  time_t * last_change;
  size_t cnt;
}dirscan;

const char * udpc_dirscan_service_name;

// md5
udpc_md5 udpc_file_md5(const char * path);
void udpc_print_md5(udpc_md5 md5);
bool udpc_md5_compare(udpc_md5 a, udpc_md5 b);

// dir
dirscan scan_directories(const char * basedir);
void dirscan_clean(dirscan * _dirscan);

// server/client
void udpc_dirscan_serve(udpc_connection * con, dirscan last_dirscan, size_t buffer_size, int delay_us, void * read);
// returns -1 on error.
int udpc_dirscan_client(udpc_connection * con, dirscan * dscan);
