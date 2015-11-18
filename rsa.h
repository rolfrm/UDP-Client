typedef struct{


}udpc_rsa_pubkey;

// Encrypts the data in buffer and writes it back in-place.
void udpc_unsafe_rsa_encrypt(udpc_rsa_pubkey pubkey, void ** buffer, size_t * len);
void udpc_rsa_get_local_pubkey(udpc_rsa_pubkey pubkey);
udpc_rsa_pubkey udpc_read_pubkey(void * pubkey_data);
