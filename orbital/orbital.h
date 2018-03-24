
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
  
};

talk_dispatch * talk_dispatch_create(udpc_connection * con, bool is_server);
void talk_dispatch_start_conversation(talk_dispatch * talk, conversation * conv);

void talk_dispatch_process(talk_dispatch * talk, int timeoutms);
void talk_dispatch_update(talk_dispatch * talk);
void talk_dispatch_delete(talk_dispatch ** talk);
void talk_dispatch_send(talk_dispatch * talk, conversation * conv, void * message, int message_length);
void conv_send(conversation * self, void * message, int message_length);
conversation * talk_create_conversation(talk_dispatch * talk);
