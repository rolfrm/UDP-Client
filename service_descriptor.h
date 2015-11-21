typedef struct{
  char * username;
  char * service;
  char * host;
} service_descriptor;

service_descriptor udpc_get_service_descriptor(const char * service_string);
void udpc_delete_service_descriptor(service_descriptor desc);
void udpc_print_service_descriptor(service_descriptor item);

