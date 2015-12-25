// UDPC Sequence
typedef struct{
  udpc_connection * con;
  u64 seq_id;
  u64 seq_cnt;
  u64 seq_other_id ;
  u64 seq_other_cnt;
  u64 last_msg_time;
}udpc_seq;

typedef struct{
  // peak rtt in us. Measured at max load.
  int rtt_peak_us;
  // mean rtt in us. Measured at max load.
  int rtt_mean_us;
  // The number of packets that can be sent before connection is flodded.
  int max_pkgs_per_sec;
  // The optimal MTU size. Max is 16KB, normal is 1.5KB. Depends on if connection supports jumbo frames.
  int opt_mtu_size;
}udpc_connection_stats;

udpc_seq udpc_setup_seq(udpc_connection * con);
udpc_seq udpc_setup_seq_peer(udpc_connection * con);
int udpc_seq_read(udpc_seq * con, void * buffer, size_t max_size, u64 * seq_number);
void udpc_seq_write(udpc_seq * con, const void * buffer, size_t size);

// A conversation.
typedef struct{
  // unique conversation id.
  u64 seqid;
  // the connection used.
  udpc_connection * con;
  // time last mesage was received.
  u64 last_msg_time;
}udpc_conv;

void udpc_conv_load(udpc_conv * conv, udpc_connection * con, u64 seqid);
int udpc_conv_write(udpc_conv * conv, void * data, size_t data_site);
int udpc_conv_read(udpc_conv * conv, void * buffer, size_t buffer_size);

typedef struct{
  size_t total_size;
  size_t chunk_size;
}transmission_data;
udpc_connection_stats get_stats();
//int status = udpc_receive_transmission(con, service_id, handle_chunk);
int udpc_receive_transmission(udpc_connection * con, udpc_connection_stats * stats, u64 service_id,
			      int (* handle_chunk)(const transmission_data * tid, const void * chunk,
						   size_t chunk_id, size_t chunk_size, void * userdata),
			      void * userdata);

int udpc_send_transmission(udpc_connection * con, udpc_connection_stats * stats, u64 service_id,
			   size_t total_size, size_t chunk_size,
			   int (* send_chunk)(const transmission_data * tid, const void * chunk,
					      size_t chunk_id, size_t chunk_size, void * userdata),
			   void * userdata);
