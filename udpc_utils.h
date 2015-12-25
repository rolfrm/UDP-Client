void udpc_pack(const void * data, size_t data_len, void ** buffer, size_t * buffer_size);
void udpc_pack_int(int value, void ** buffer, size_t * buffer_size);
void udpc_pack_string(const void * str, void ** buffer, size_t * buffer_size);
void udpc_pack_size_t(size_t value, void ** buffer, size_t * buffer_size);
void udpc_pack_u8(u8 value, void ** buffer, size_t * buffer_size);
void udpc_unpack(void * dst, size_t size, void ** buffer);
int udpc_unpack_int(void ** buffer);
size_t udpc_unpack_size_t(void ** buffer);
char * udpc_unpack_string(void ** buffer);
u64 get_rand_u64();

