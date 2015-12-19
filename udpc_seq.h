// UDPC Sequence
typedef struct{
  udpc_connection * con;
  u64 seq_id;
  u64 seq_cnt;
  u64 seq_other_id ;
  u64 seq_other_cnt;
}udpc_seq;

udpc_seq udpc_setup_seq(udpc_connection * con);
udpc_seq udpc_setup_seq_peer(udpc_connection * con);
int udpc_seq_read(udpc_seq * con, void * buffer, size_t max_size, u64 * seq_number);
void udpc_seq_write(udpc_seq * con, const void * buffer, size_t size);
