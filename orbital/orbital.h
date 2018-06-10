
struct _talk_dispatch;
typedef struct _talk_dispatch talk_dispatch;

struct _conversation;
typedef struct _conversation conversation;


struct _conversation{
  // server: even id's, client non-even id's.
  int id;
  bool finished;
  void * user_data;
  talk_dispatch * talk;
  
  void (* update)(conversation * self);
  void (* process)(conversation * self, void * data, int length);
};

// struct for managing multiple ongoing conversations.
struct _talk_dispatch{
  udpc_connection * connection;
  bool is_server;

  // move to hidden.

  // list that contains both client and server conversations.
  size_t conversation_count;
  conversation ** conversations;
  size_t active_conversation_count;

  size_t read_buffer_size;
  void * read_buffer;

  size_t send_buffer_size;
  void * send_buffer;

  // us
  u64 latency;
  
  // timestamp us.
  u64 last_update;

  u64 * transfer_time_window;
  u64 * transfer_sum_window;
  u64 transfer_capacity;
  u64 transfer_count;

  // index points to the newest element in transfer windows.
  u64 transfer_index;
  u64 transfer_sum;

  void (* new_conversation)(conversation * conv, void * buffer, int length);
  iron_mutex process_mutex;

  f64 target_rate;
  f64 update_interval;
  bool is_processing;
  bool is_updating;
  bool connection_closed;
  
};

talk_dispatch * talk_dispatch_create(udpc_connection * con, bool is_server);
void talk_dispatch_start_conversation(talk_dispatch * talk, conversation * conv);

void talk_dispatch_process(talk_dispatch * talk, int timeoutms);
void talk_dispatch_update(talk_dispatch * talk);
void talk_dispatch_delete(talk_dispatch ** talk);
void talk_dispatch_send(talk_dispatch * talk, conversation * conv, void * message, int message_length);
void conv_send(conversation * self, void * message, int message_length);
conversation * talk_create_conversation(talk_dispatch * talk);

struct _reader;
typedef struct _reader reader;

struct _reader {
  size_t (*read)(reader * rd, void * dst, size_t size);
  void (*seek)(reader * rd, size_t position);
  void (*close)(reader * rd);
  size_t position;
  size_t size;

  void * data;
};

reader * mem_reader_create(void * data, size_t size, bool delete_on_close);
reader * file_reader_create(const char * path);
size_t reader_read(reader * rd, void * buffer, size_t length);
void reader_seek(reader * rd, size_t position);
void reader_close(reader ** rd);

struct _writer;
typedef struct _writer writer;

struct _writer {
  void (*write)(writer * rd, void * src, size_t size);
  void (*seek)(writer * rd, size_t position);
  void (*close)(writer * rd);
  size_t position;  // not to be changed by user
  void * data;
};

writer * mem_writer_create(void ** location, size_t * size_location);
writer * file_writer_create(const char * file);

void writer_write(writer * wt, void * src, size_t size);
void writer_seek(writer * wt, size_t position);
void writer_close(writer ** wt);

void safesend_create(conversation * conv, reader * reader);
void safereceive_create(conversation * conv, writer * writer);

typedef enum{
  DATA_LOG_NEW_FILE = 1,
  DATA_LOG_NEW_DIR = 2,
  DATA_LOG_NAME = 3,
  DATA_LOG_DATA = 4,
  DATA_LOG_NULL = 5,
  DATA_LOG_DELETED = 6,
  // DATALOG_DEDUPLICATE = 7 // implement de-duplication by pointing back to a previous datalog entry.
  DATA_LOG_FILE_START = 8,
  DATA_LOG_FILE_END = 9
  
  
}data_log_item_type;
 
typedef  struct  __attribute__((__packed__)) {
  u64 file_id;
  data_log_item_type type;
}data_log_item_header;

typedef u64 data_log_timestamp;

typedef struct __attribute__((__packed__)){
  data_log_item_header header;
  u64 size;
  u64 hash;
}data_log_new_file;

typedef struct __attribute__((__packed__)){
  data_log_item_header header;

}data_log_new_dir;

typedef struct __attribute__((__packed__)){
  data_log_item_header header;
  const char * name;
}data_log_name;

typedef struct  __attribute__((__packed__)){
  data_log_item_header header;
  u64 offset;
  u64 size;
  void * data;
}data_log_data;

typedef struct __attribute__((__packed__)){
  data_log_item_header header;
}data_log_deleted;

typedef struct __attribute__((__packed__)){
  data_log_item_header header;
}data_log_null;

extern data_log_null null_item;

void data_log_generate_items(const char * directory, void (* f)(const data_log_item_header * item, void * userdata), void * userdata);

void datalog_generate_from_file(const char * file, void (* f)(const data_log_item_header * item, void * userdata), void * userdata);


typedef struct{
  const char * root;
  const char * commits_file;
  const char * datalog_file;
  void * internal;
}datalog;

typedef struct __attribute__((__packed__)) {
  u64 hash;
  u32 length;
}commit_item;

void datalog_initialize(datalog * dlog, const char * root_dir, const char * datalog_file, const char * commits_file);
void datalog_update(datalog * dlog);
void datalog_destroy(datalog * dlog);
u64 datalog_get_commit_count(datalog * dlog);

typedef struct {
  datalog * dlog;
  data_log_item_header * head;
  size_t offset;
  size_t commit_index;
  void * internal;
}datalog_iterator;

void datalog_iterator_init(datalog * dlog, datalog_iterator * it);
const data_log_item_header * datalog_iterator_next(datalog_iterator * it);
const data_log_item_header * datalog_iterator_next0(FILE * f, void ** buffer, size_t * buffer_size);
const data_log_item_header * datalog_iterator_next2(datalog_iterator * it, commit_item * out_commit);
void datalog_iterator_destroy(datalog_iterator * it);
void datalog_apply_item(datalog * dlog, const data_log_item_header * item, bool register_only, bool write_commit, bool check_delete);

typedef struct{
  datalog * dlog;
  bool reverse;
  void * internal;

}datalog_commit_iterator;

void datalog_commit_iterator_init(datalog_commit_iterator * it, datalog * dlog, bool reverse);
void datalog_commit_iterator_destroy(datalog_commit_iterator * it);
bool datalog_commit_iterator_next(datalog_commit_iterator * it, commit_item * out_item);

commit_item datalog_get_commit(datalog * dlog, u64 index);
// Starts iterating from a specific commit item. Iteration started this way can only be forward.
// the first commit retrieved by next will be the commit specified in 'item'.
void datalog_iterator_init_from(datalog_iterator * it, datalog * dlog, commit_item item);
void datalog_cpy_to_file(datalog * dlog, const data_log_item_header * item, FILE * file);
void datalog_rewind_to(datalog * dlog, datalog_iterator * it2);
commit_item datalog_get_prev_commit(datalog * dlog);

// utils.
u64 orbital_file_hash(const char * file);
u64 orbital_file_hash2(FILE * f);
bool orbital_file_exists(const char * path);

// debug
void datalog_assert_is_at_end(datalog * dlog);
void datalog_print_commits(datalog * dlog, bool reverse);

extern data_stream dlog_verbose;
