void udpc_speed_client(udpc_connection * con, int delay, int bufsize, int count, int * out_missed, int * out_missed_seqs, double * out_mean_rtt, double * out_peak_rtt);
// first buffer is optional
void udpc_speed_serve(udpc_connection * c2, void * first_buffer);
const char * udpc_speed_test_service_name;
