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

udpc_md5 udpc_file_md5(const char * path);
dirscan scan_directories(const char * basedir);

const char * udpc_dirscan_service_name;
void udpc_print_md5(udpc_md5 md5);
void dirscan_clean(dirscan * _dirscan);

void udpc_dirscan_serve(udpc_connection * con, dirscan last_dirscan, size_t buffer_size, int delay_us, void * read);
void udpc_dirscan_client(udpc_connection * con);
