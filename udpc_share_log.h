// requires iron/types

// LOG WRITING
void share_log_set_file(const char * path);
void share_log_start_send_file(const char * file);
void share_log_end_send_file();
void share_log_start_receive_file(const char * file);
void share_log_end_receive_file();
void share_log_progress(i64 bytes_handled, i64 total_bytes);
void share_log_file_deleted(const char * file);


// LOG READING
struct _share_log_reader;
typedef struct _share_log_reader share_log_reader;
typedef enum {
  SHARE_LOG_PROGRESS = 0,
  SHARE_LOG_START_RECEIVE = 1,
  SHARE_LOG_START_SEND = 2,
  SHARE_LOG_END = 3,
  SHARE_LOG_DELETE
}share_log_item_type;

typedef struct{
  // Which type this log item is.
  share_log_item_type type;
  // When this log message was created.
  i64 timestamp;
  // log message content.
  union{
    // requires type = SHARE_LOG_START RECEIVE/SEND
    char * file_name;
    // requires type = SHARE_LOG_PROGRESS
    struct{
      i64 bytes_handled;
      i64 total_bytes;
    } progress;
  };
}share_log_item;

// Open the log reader
share_log_reader * share_log_open_reader(const char * path);
// Close the log reader and release the resources.
void share_log_close_reader(share_log_reader ** reader);
// Read a number of items.
int share_log_reader_read(share_log_reader * reader, share_log_item * out_items, int max_reads);
void share_log_clear_items(share_log_item * items, int cnt);

// pretty print
void share_log_item_print(share_log_item item);
