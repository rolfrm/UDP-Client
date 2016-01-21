// requires udpc_seq.h
typedef struct {
  unsigned char md5[16]; //16 = MD5_DIGEST_LENGTH
}udpc_md5;

typedef u64 t_us;

typedef enum{
  UDPC_DIRSCAN_FILE,
  UDPC_DIRSCAN_DIR,
  UDPC_DIRSCAN_DELETED
}udpc_dirscan_type;

typedef struct{
  char ** files;
  udpc_md5 * md5s;
  t_us * last_change;
  size_t * size;
  udpc_dirscan_type * type;
  size_t cnt;
}dirscan;

const char * udpc_dirscan_service_name;

// md5
udpc_md5 udpc_file_md5(const char * path);
void udpc_print_md5(udpc_md5 md5);
void udpc_fprintf_md5(FILE * f, udpc_md5 md5);
bool udpc_md5_compare(udpc_md5 a, udpc_md5 b);

// dir
dirscan scan_directories(const char * basedir);
// Updates a previously calculated dirscan. Note that this is a destructive operation.
void udpc_dirscan_update(const char * basedir, dirscan * dir, bool include_directories);
void dirscan_clean(dirscan * _dirscan);
void dirscan_print(dirscan ds);
dirscan dirscan_from_buffer(void * buffer);
void * dirscan_to_buffer(dirscan _dirscan, size_t * size);
typedef enum{
  DIRSCAN_NEW = 2,
  DIRSCAN_GONE = 4,
  DIRSCAN_DIFF_MD5 = 8,
  DIRSCAN_GONE_PREV = 16
}dirscan_state;

typedef struct{
  dirscan_state * states;
  size_t * index1;
  size_t * index2;
  size_t cnt;
}dirscan_diff;

dirscan_diff udpc_dirscan_diff(dirscan d1, dirscan d2);
void udpc_dirscan_clear_diff(dirscan_diff * diff);


// server/client
void udpc_dirscan_serve(udpc_connection * con, udpc_connection_stats * stats, dirscan last_dirscan);
// returns -1 on error.
int udpc_dirscan_client(udpc_connection * con, udpc_connection_stats * stats, dirscan * dscan);

// Ensures that the directory for the file path fp exists.
void ensure_directory(const char * fp);

