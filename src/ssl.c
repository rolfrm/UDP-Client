#include <netinet/in.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <stdbool.h>
#include <iron/mem.h>
#include <iron/log.h>
#include <iron/utils.h>
#include "ssl.h"
#include <pthread.h>
//#define simulate_pk_loss {int rnd = rand() % 2; if(rnd == 0)return;}
#define simulate_pk_loss ;

//"HIGH:!aNULL:!MD5:!RC4"
const char * ssl_ciphers = "AES256-SHA256";
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
  int fd;
};

// locking stuff needed top make OpenSSL thread safe
#include <iron/utils.h>
#include <openssl/crypto.h>
#define MUTEX_TYPE       pthread_mutex_t
#define MUTEX_SETUP(x)   pthread_mutex_init(&(x), NULL)
#define MUTEX_CLEANUP(x) pthread_mutex_destroy(&(x))
#define MUTEX_LOCK(x)    pthread_mutex_lock(&(x))
#define MUTEX_UNLOCK(x)  pthread_mutex_unlock(&(x))
#define THREAD_ID        pthread_self()
 
 
/* This array will store all of the mutexes available to OpenSSL. */ 
static MUTEX_TYPE *mutex_buf = NULL;
 
static void locking_function(int mode, int n, const char *file, int line)
{
  UNUSED(file);
  UNUSED(line);
  if(mode & CRYPTO_LOCK){
    MUTEX_LOCK(mutex_buf[n]);
  }else{
    MUTEX_UNLOCK(mutex_buf[n]);
  }
}
 
static unsigned long id_function(void)
{
  return ((unsigned long)THREAD_ID);
}
 
int thread_setup(void)
{
  int i;
 
  mutex_buf = malloc(CRYPTO_num_locks() * sizeof(MUTEX_TYPE));
  if(!mutex_buf)
    return 0;
  for(i = 0;  i < CRYPTO_num_locks();  i++)
    MUTEX_SETUP(mutex_buf[i]);
  CRYPTO_set_id_callback(id_function);
  CRYPTO_set_locking_callback(locking_function);
  return 1;
}
 
int thread_cleanup(void)
{
  int i;
 
  if(!mutex_buf)
    return 0;
  CRYPTO_set_id_callback(NULL);
  CRYPTO_set_locking_callback(NULL);
  for(i = 0;  i < CRYPTO_num_locks();  i++)
    MUTEX_CLEANUP(mutex_buf[i]);
  free(mutex_buf);
  mutex_buf = NULL;
  return 1;
}

static void ssl_ensure_initialized(){
  static bool initialized = false;
  if(initialized) return;
  SSL_library_init();
  OpenSSL_add_ssl_algorithms();
  SSL_load_error_strings();
  thread_setup();
  if(initialized)
    ERROR("this shouldn ot happen");
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
  SSL_CTX * ctx = SSL_CTX_new(DTLSv1_2_server_method());
  { // setup server SSL CTX
    SSL_CTX_set_cipher_list(ctx, ssl_ciphers);
    SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);
    
    if (!SSL_CTX_use_certificate_file(ctx, "certs/client-cert.pem", SSL_FILETYPE_PEM)){
      loge("ERROR: no certificate found!\n");
      SSL_CTX_free(ctx);
      return NULL;
    }
    
    if (!SSL_CTX_use_PrivateKey_file(ctx, "certs/client-key.pem", SSL_FILETYPE_PEM)){
      loge("ERROR: no private key found!\n");
      SSL_CTX_free(ctx);
      return NULL;
    }
    
    if (!SSL_CTX_check_private_key (ctx)){
      loge("ERROR: invalid private key!\n");
      SSL_CTX_free(ctx);
      return NULL;
    }
    
    /* Client has to authenticate */
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_CLIENT_ONCE, dtls_verify_callback);
    
    SSL_CTX_set_cookie_generate_cb(ctx, generate_cookie);
    SSL_CTX_set_cookie_verify_cb(ctx, verify_cookie);
  }
  ssl_server * srv = alloc(sizeof(ssl_server));
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
  case SSL_ERROR_SYSCALL:{
    //logd("SSL_ERROR_SYSCALL error id: %i %i", ERR_get_error(), ret);
    // -fallthrough
    //doerr(SSL_ERROR_WANT_ASYNC);
    doerr(SSL_ERROR_WANT_CONNECT);
    doerr(SSL_ERROR_WANT_ACCEPT);
    doerr(SSL_ERROR_WANT_READ);
    doerr(SSL_ERROR_WANT_WRITE);
    doerr(SSL_ERROR_ZERO_RETURN);
  }
    break;
  case SSL_ERROR_NONE:
    break;
  }
  return 0;
}

ssl_server_con * ssl_server_accept(ssl_server_client * scli, int fd){
  BIO_set_fd(SSL_get_rbio(scli->ssl), fd, BIO_NOCLOSE);
  BIO_ctrl(SSL_get_rbio(scli->ssl), BIO_CTRL_DGRAM_SET_CONNECTED, 0, &scli->addr);
  int ret = 0;
  do{ret = SSL_accept(scli->ssl);}
  while(ret == 0);
  if(ret < 0)
     return NULL;
  ASSERT(ret > 0);
  struct timeval timeout;
  timeout.tv_sec = 5;
  timeout.tv_usec = 0;
  BIO_ctrl(SSL_get_rbio(scli->ssl), BIO_CTRL_DGRAM_SET_RECV_TIMEOUT, 0, &timeout);
  ssl_server_con * con = alloc(sizeof(ssl_server_con));
  con->ssl = scli->ssl;
  return con;
}

int ssl_server_read(ssl_server_con * con, void * buffer, size_t buffer_size){
  return SSL_read(con->ssl, buffer, buffer_size);
}

int ssl_server_peek(ssl_server_con * con, void * buffer, size_t buffer_size){
  return SSL_peek(con->ssl, buffer, buffer_size);
}

int ssl_server_pending(ssl_server_con * con){
  return SSL_pending(con->ssl);
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
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_RECV_TIMEOUT, 0, &timeout);
  }
  SSL * ssl = SSL_new(serv->ctx);
  SSL_set_bio(ssl, bio, bio);
  SSL_set_options(ssl, SSL_OP_COOKIE_EXCHANGE);

  struct sockaddr_storage client_addr = {0};
  int error = 0;

  while ((error = DTLSv1_listen(ssl, &client_addr)) <= 0){
    if(error == 0) continue;
    SSL_free(ssl);
    //logd("Connect error: %i\n", error);
    return NULL;
  }
  ssl_server_client * con = alloc(sizeof(ssl_server_client));
  con->ssl = ssl;
  con->addr = client_addr;
  con->bio = bio;
  return con;
  
}

ssl_client * ssl_start_client(int fd, struct sockaddr * remote_addr){
  ssl_ensure_initialized();
  SSL_CTX * ctx = SSL_CTX_new(DTLSv1_2_client_method());
  //SSL_CTX_set_info_callback(ctx, SSL_CTX_state_cb);
  SSL_CTX_set_cipher_list(ctx, ssl_ciphers);

  if (!SSL_CTX_use_certificate_file(ctx, "certs/client-cert.pem", SSL_FILETYPE_PEM)){
    printf("\nERROR: no certificate found!\n");
    SSL_CTX_free(ctx);
    return NULL;
  }
  
  
  if (!SSL_CTX_use_PrivateKey_file(ctx, "certs/client-key.pem", SSL_FILETYPE_PEM)){
    printf("\nERROR: no private key found!\n");
    SSL_CTX_free(ctx);
    return NULL;
  }
  
  if (!SSL_CTX_check_private_key (ctx)){
    printf("\nERROR: invalid private key!\n");
    SSL_CTX_free(ctx);
    return NULL;
  }
  SSL_CTX_set_verify_depth (ctx, 2);
  
  SSL * ssl = SSL_new(ctx);
  SSL_set_options(ssl, SSL_OP_COOKIE_EXCHANGE);
  // Create BIO, connect and set to already connected.
  BIO * bio = BIO_new_dgram(fd, BIO_NOCLOSE);
  
  BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_CONNECTED, 0, remote_addr);
  SSL_set_bio(ssl, bio, bio);
  {
    struct timeval timeout;
    timeout.tv_sec = 2;
    timeout.tv_usec = 0;
    BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_RECV_TIMEOUT, 0, &timeout);
  }
    
  int ret = SSL_connect(ssl);
  if(ret < 0){
    SSL_free(ssl);
    SSL_CTX_free(ctx);
    return NULL;
  }

  ssl_client * cli = alloc(sizeof(ssl_client));
  cli->ssl = ssl;
  cli->ctx = ctx;
  cli->fd = fd;
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
  BIO_ctrl(SSL_get_rbio(cli->ssl), BIO_CTRL_DGRAM_GET_RECV_TIMEOUT, 0, &timeout);
  return timeout.tv_usec + timeout.tv_sec * 1000000;
}

int ssl_server_get_timeout(ssl_server_con * cli){
  struct timeval timeout;
  BIO_ctrl(SSL_get_rbio(cli->ssl), BIO_CTRL_DGRAM_GET_RECV_TIMEOUT, 0, &timeout);
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

int ssl_client_pending(ssl_client * cli){
  return SSL_pending(cli->ssl);
}

int ssl_client_peek(ssl_client * cli, void * buffer, size_t length){
  socklen_t len = SSL_peek(cli->ssl, buffer, length);
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
  BIO_ssl_shutdown(SSL_get_rbio(cli->ssl));
  SSL_shutdown(cli->ssl);
  SSL_free(cli->ssl);
  SSL_CTX_free(cli->ctx);
}

struct sockaddr_storage ssl_server_client_addr(ssl_server_client * cli){
  return cli->addr;
}
