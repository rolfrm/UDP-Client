#include <netinet/in.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <stdbool.h>
#include <iron/mem.h>
#include <iron/log.h>
#include <iron/utils.h>
#include "ssl.h"

//#define simulate_pk_loss int rnd = rand() % 2; if(rnd == 0)return;
#define simulate_pk_loss ;

struct _ssl_server{
  SSL_CTX * ctx;
  int fd;
};

struct _ssl_server_client{
  SSL * ssl;
  struct sockaddr_storage addr;
  BIO * bio;
};

struct _ssl_server_con{
  SSL * ssl;
};

struct _ssl_client{
  SSL * ssl;
  SSL_CTX * ctx;
};

static void ssl_ensure_initialized(){
  static bool initialized = false;
  if(initialized) return;
  SSL_library_init();
  OpenSSL_add_ssl_algorithms();
  SSL_load_error_strings();
  initialized = true;
}

#define BUFFER_SIZE          (1<<16)
#define COOKIE_SECRET_LENGTH 16
unsigned char cookie_secret[COOKIE_SECRET_LENGTH];
int cookie_initialized=0;

static int generate_cookie(SSL *ssl, unsigned char *cookie, unsigned int *cookie_len)
{
  unsigned char *buffer, result[EVP_MAX_MD_SIZE];
  unsigned int length = 0, resultlength;
  union {
    struct sockaddr_storage ss;
    struct sockaddr_in6 s6;
    struct sockaddr_in s4;
  } peer;

  /* Initialize a random secret */
  if (!cookie_initialized)
    {
      if (!RAND_bytes(cookie_secret, COOKIE_SECRET_LENGTH))
	{
	  printf("error setting random cookie secret\n");
	  return 0;
	}
      cookie_initialized = 1;
    }

  /* Read peer information */
  (void) BIO_dgram_get_peer(SSL_get_rbio(ssl), &peer);

  /* Create buffer with peer's address and port */
  length = 0;
  switch (peer.ss.ss_family) {
  case AF_INET:
    length += sizeof(struct in_addr);
    break;
  case AF_INET6:
    length += sizeof(struct in6_addr);
    break;
  default:
    OPENSSL_assert(0);
    break;
  }
  length += sizeof(in_port_t);
  buffer = (unsigned char*) OPENSSL_malloc(length);

  if (buffer == NULL)
    {
      printf("out of memory\n");
      return 0;
    }

  switch (peer.ss.ss_family) {
  case AF_INET:
    memcpy(buffer,
	   &peer.s4.sin_port,
	   sizeof(in_port_t));
    memcpy(buffer + sizeof(peer.s4.sin_port),
	   &peer.s4.sin_addr,
	   sizeof(struct in_addr));
    break;
  case AF_INET6:
    memcpy(buffer,
	   &peer.s6.sin6_port,
	   sizeof(in_port_t));
    memcpy(buffer + sizeof(in_port_t),
	   &peer.s6.sin6_addr,
	   sizeof(struct in6_addr));
    break;
  default:
    OPENSSL_assert(0);
    break;
  }

  /* Calculate HMAC of buffer using the secret */
  HMAC(EVP_sha1(), (const void*) cookie_secret, COOKIE_SECRET_LENGTH,
       (const unsigned char*) buffer, length, result, &resultlength);
  OPENSSL_free(buffer);

  memcpy(cookie, result, resultlength);
  *cookie_len = resultlength;

  return 1;
}

static int verify_cookie(SSL *ssl, unsigned char *cookie, unsigned int cookie_len)
{
  unsigned char *buffer, result[EVP_MAX_MD_SIZE];
  unsigned int length = 0, resultlength;
  union {
    struct sockaddr_storage ss;
    struct sockaddr_in6 s6;
    struct sockaddr_in s4;
  } peer;

  /* If secret isn't initialized yet, the cookie can't be valid */
  if (!cookie_initialized)
    return 0;

  /* Read peer information */
  (void) BIO_dgram_get_peer(SSL_get_rbio(ssl), &peer);

  /* Create buffer with peer's address and port */
  length = 0;
  switch (peer.ss.ss_family) {
  case AF_INET:
    length += sizeof(struct in_addr);
    break;
  case AF_INET6:
    length += sizeof(struct in6_addr);
    break;
  default:
    OPENSSL_assert(0);
    break;
  }
  length += sizeof(in_port_t);
  buffer = (unsigned char*) OPENSSL_malloc(length);

  if (buffer == NULL)
    {
      printf("out of memory\n");
      return 0;
    }

  switch (peer.ss.ss_family) {
  case AF_INET:
    memcpy(buffer,
	   &peer.s4.sin_port,
	   sizeof(in_port_t));
    memcpy(buffer + sizeof(in_port_t),
	   &peer.s4.sin_addr,
	   sizeof(struct in_addr));
    break;
  case AF_INET6:
    memcpy(buffer,
	   &peer.s6.sin6_port,
	   sizeof(in_port_t));
    memcpy(buffer + sizeof(in_port_t),
	   &peer.s6.sin6_addr,
	   sizeof(struct in6_addr));
    break;
  default:
    OPENSSL_assert(0);
    break;
  }

  /* Calculate HMAC of buffer using the secret */
  HMAC(EVP_sha1(), (const void*) cookie_secret, COOKIE_SECRET_LENGTH,
       (const unsigned char*) buffer, length, result, &resultlength);
  OPENSSL_free(buffer);

  if (cookie_len == resultlength && memcmp(result, cookie, resultlength) == 0)
    return 1;

  return 0;
}

static int dtls_verify_callback (int ok, X509_STORE_CTX *ctx) {
  UNUSED(ok);
  UNUSED(ctx);
  /* This function should ask the user
   * if he trusts the received certificate.
   * Here we always trust.
   */
  return 1;
}

ssl_server * ssl_setup_server(int fd){
  ssl_ensure_initialized();
  SSL_CTX * ctx = SSL_CTX_new(DTLSv1_server_method());
  { // setup server SSL CTX
    SSL_CTX_set_cipher_list(ctx, "HIGH:!aNULL:!MD5:!RC4");
    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);
    
    if (!SSL_CTX_use_certificate_file(ctx, "certs/server-cert.pem", SSL_FILETYPE_PEM))
      printf("\nERROR: no certificate found!");
    
    if (!SSL_CTX_use_PrivateKey_file(ctx, "certs/server-key.pem", SSL_FILETYPE_PEM))
      printf("\nERROR: no private key found!");
    
    if (!SSL_CTX_check_private_key (ctx))
      printf("\nERROR: invalid private key!");
    
    /* Client has to authenticate */
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE, dtls_verify_callback);
    
    SSL_CTX_set_read_ahead(ctx, 1);
    SSL_CTX_set_cookie_generate_cb(ctx, generate_cookie);
    SSL_CTX_set_cookie_verify_cb(ctx, verify_cookie);
  }
  ssl_server * srv = alloc0(sizeof(ssl_server));
  srv->ctx = ctx;
  srv->fd = fd;
  return srv;
}

void ssl_server_cleanup(ssl_server * server){
  SSL_CTX_free(server->ctx);
  free(server);
}

static int handle_ssl_error2(SSL * ssl, int ret, int * accepted_errors,
			     int * retcode, int accepted_error_cnt){
  int err = SSL_get_error(ssl, ret);
  for(int i = 0; i < accepted_error_cnt; i++)
    if(accepted_errors[i] == err)
      return retcode[i];

#define doerr(kind)case kind: ERROR(#kind); break;
  switch(err){
    doerr(SSL_ERROR_SSL);
  case(SSL_ERROR_SYSCALL):
    ERROR("SSL_ERROR_SYSCALL error id: %i %i", ERR_get_error(), ret); 
    //doerr(SSL_ERROR_WANT_ASYNC);
    doerr(SSL_ERROR_WANT_CONNECT);
    doerr(SSL_ERROR_WANT_ACCEPT);
    doerr(SSL_ERROR_WANT_READ);
    doerr(SSL_ERROR_WANT_WRITE);
    doerr(SSL_ERROR_ZERO_RETURN);
  case SSL_ERROR_NONE:
    break;
  }
  return 0;
}

static void handle_ssl_error(SSL * ssl, int ret){
  handle_ssl_error2(ssl, ret, NULL, NULL, 0);
}

ssl_server_con * ssl_server_accept(ssl_server_client * scli, int fd){
  BIO_set_fd(SSL_get_rbio(scli->ssl), fd, BIO_NOCLOSE);
  BIO_ctrl(SSL_get_rbio(scli->ssl), BIO_CTRL_DGRAM_SET_CONNECTED, 0, &scli->addr);
  int ret = 0;
  do{ret = SSL_accept(scli->ssl);}
  while(ret == 0);
  if(ret < 0){
    handle_ssl_error(scli->ssl, ret);
    return NULL;
  }
  ASSERT(ret > 0);
  struct timeval timeout;
  timeout.tv_sec = 5;
  timeout.tv_usec = 0;
  BIO_ctrl(SSL_get_rbio(scli->ssl), BIO_CTRL_DGRAM_SET_RECV_TIMEOUT, 0, &timeout);
  ssl_server_con * con = alloc0(sizeof(ssl_server_con));
  con->ssl = scli->ssl;
  return con;
}

int ssl_server_read(ssl_server_con * con, void * buffer, size_t buffer_size){
  return SSL_read(con->ssl, buffer, buffer_size);
}



void ssl_server_write(ssl_server_con * con, const void * buffer, size_t buffer_size){
  simulate_pk_loss;
  SSL_write(con->ssl, buffer, buffer_size);
}

void ssl_server_heartbeat(ssl_server_client * cli){
  SSL_heartbeat(cli->ssl);
}

void ssl_server_close(ssl_server_client * cli){
  SSL_shutdown(cli->ssl);
  SSL_free(cli->ssl);
}

ssl_server_client * ssl_server_listen(ssl_server * serv){

  BIO * bio = BIO_new_dgram(serv->fd, BIO_NOCLOSE);
  {
    struct timeval timeout;
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_RECV_TIMEOUT, 0, &timeout);
  }
  SSL * ssl = SSL_new(serv->ctx);
  SSL_set_bio(ssl, bio, bio);
  SSL_set_options(ssl, SSL_OP_COOKIE_EXCHANGE);

  struct sockaddr_storage client_addr;
  while (DTLSv1_listen(ssl, &client_addr) <= 0){
    SSL_free(ssl);
    return NULL;
  }
  ssl_server_client * con = alloc0(sizeof(ssl_server_client));
  con->ssl = ssl;
  con->addr = client_addr;
  con->bio = bio;
  return con;
  
}

ssl_client * ssl_start_client(int fd, struct sockaddr * remote_addr){
  ssl_ensure_initialized();
  SSL_CTX * ctx = SSL_CTX_new(DTLSv1_client_method());
  SSL_CTX_set_cipher_list(ctx, "HIGH:!aNULL:!MD5:!RC4");
  
  if (!SSL_CTX_use_certificate_file(ctx, "certs/client-cert.pem", SSL_FILETYPE_PEM))
    printf("\nERROR: no certificate found!");
  
  if (!SSL_CTX_use_PrivateKey_file(ctx, "certs/client-key.pem", SSL_FILETYPE_PEM))
    printf("\nERROR: no private key found!");
  
  if (!SSL_CTX_check_private_key (ctx))
    printf("\nERROR: invalid private key!");
  
  SSL_CTX_set_verify_depth (ctx, 2);
  SSL_CTX_set_read_ahead(ctx, 1);
  
  SSL * ssl = SSL_new(ctx);
  
  // Create BIO, connect and set to already connected.
  BIO * bio = BIO_new_dgram(fd, BIO_CLOSE);
  
  BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_CONNECTED, 0, remote_addr);
  SSL_set_bio(ssl, bio, bio);
  int ret = SSL_connect(ssl);
  if(ret < 0){
    handle_ssl_error(ssl, ret);
  }
  
  {
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_RECV_TIMEOUT, 0, &timeout);
  }

  ssl_client * cli = alloc0(sizeof(ssl_client));
  cli->ssl = ssl;
  cli->ctx = ctx;
  return cli;
}

void ssl_set_timeout(ssl_client * cli, int timeout_us){
  struct timeval timeout;
  {
    timeout.tv_sec = timeout_us / 1000000;
    timeout.tv_usec = timeout_us % 1000000;
  }
  BIO_ctrl(SSL_get_rbio(cli->ssl), BIO_CTRL_DGRAM_SET_RECV_TIMEOUT, 0, &timeout);
}

void ssl_server_set_timeout(ssl_server_con*  con, int timeout_us){
  struct timeval timeout;
  {
    timeout.tv_sec = timeout_us / 1000000;
    timeout.tv_usec = timeout_us % 1000000;
  }
  BIO_ctrl(SSL_get_rbio(con->ssl), BIO_CTRL_DGRAM_SET_RECV_TIMEOUT, 0, &timeout);
}

int ssl_get_timeout(ssl_client * cli){
  struct timeval timeout;
  BIO_ctrl(SSL_get_rbio(cli->ssl), BIO_CTRL_DGRAM_SET_RECV_TIMEOUT, 0, &timeout);
  return timeout.tv_usec + timeout.tv_sec * 1000000;
}

void ssl_client_write(ssl_client * cli, const void * buffer, size_t length){
  simulate_pk_loss;
  socklen_t len = SSL_write(cli->ssl, buffer, length);
  ASSERT(SSL_get_error(cli->ssl, len) == SSL_ERROR_NONE);
}

int ssl_client_read(ssl_client * cli, void * buffer, size_t length){
  socklen_t len = SSL_read(cli->ssl, buffer, length);
  int accepted[] = {SSL_ERROR_WANT_READ, SSL_ERROR_ZERO_RETURN};
  int code[] = {-1, -2};
  int ecode = handle_ssl_error2(cli->ssl, len, accepted, code, array_count(accepted));
  if(ecode < 0)
    return ecode;
  return len;
}

void ssl_client_heartbeat(ssl_client * cli){
  SSL_heartbeat(cli->ssl);
}

void ssl_client_close(ssl_client * cli){
  SSL_shutdown(cli->ssl);
  SSL_free(cli->ssl);
}

struct sockaddr_storage ssl_server_client_addr(ssl_server_client * cli){
  return cli->addr;
}
