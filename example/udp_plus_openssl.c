/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "uv.h"
#include "task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/ssl.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/rand.h>

#define CHECK_HANDLE(handle) \
  ASSERT((uv_udp_t*)(handle) == &server || (uv_udp_t*)(handle) == &client)

static uv_udp_t server;
static uv_udp_t client;

static int cl_send_cb_called;
static int cl_recv_cb_called;

static int sv_send_cb_called;
static int sv_recv_cb_called;

static int close_cb_called;


static uv_buf_t alloc_cb(uv_handle_t* handle, size_t suggested_size) {
  static char slab[65536];

  CHECK_HANDLE(handle);
  ASSERT(suggested_size <= sizeof slab);

  return uv_buf_init(slab, sizeof slab);
}


static void close_cb(uv_handle_t* handle) {
  CHECK_HANDLE(handle);
  close_cb_called++;
}


static void cl_recv_cb(uv_udp_t* handle,
                       ssize_t nread,
                       uv_buf_t buf,
                       struct sockaddr* addr,
                       unsigned flags) {
  CHECK_HANDLE(handle);
  ASSERT(flags == 0);

  if (nread < 0) {
    ASSERT(0 && "unexpected error");
  }

  if (nread == 0) {
    /* Returning unused buffer */
    /* Don't count towards cl_recv_cb_called */
    ASSERT(addr == NULL);
    return;
  }

  ASSERT(addr != NULL);
  ASSERT(nread == 4);
  ASSERT(!memcmp("PONG", buf.base, nread));

  cl_recv_cb_called++;

  uv_close((uv_handle_t*) handle, close_cb);
}


static void cl_send_cb(uv_udp_send_t* req, int status) {
  int r;

  ASSERT(req != NULL);
  ASSERT(status == 0);
  CHECK_HANDLE(req->handle);

  r = uv_udp_recv_start(req->handle, alloc_cb, cl_recv_cb);
  ASSERT(r == 0);

  cl_send_cb_called++;
}


static void sv_send_cb(uv_udp_send_t* req, int status) {
  ASSERT(req != NULL);
  ASSERT(status == 0);
  CHECK_HANDLE(req->handle);

  uv_close((uv_handle_t*) req->handle, close_cb);
  free(req);

  sv_send_cb_called++;
}


static void sv_recv_cb(uv_udp_t* handle,
                       ssize_t nread,
                       uv_buf_t buf,
                       struct sockaddr* addr,
                       unsigned flags) {
  uv_udp_send_t* req;
  int r;

  if (nread < 0) {
    ASSERT(0 && "unexpected error");
  }

  if (nread == 0) {
    /* Returning unused buffer */
    /* Don't count towards sv_recv_cb_called */
    ASSERT(addr == NULL);
    return;
  }

  CHECK_HANDLE(handle);
  ASSERT(flags == 0);

  ASSERT(addr != NULL);
  ASSERT(nread == 4);
  ASSERT(!memcmp("PING", buf.base, nread));

  /* FIXME? `uv_udp_recv_stop` does what it says: recv_cb is not called
    * anymore. That's problematic because the read buffer won't be returned
    * either... Not sure I like that but it's consistent with `uv_read_stop`.
    */
  r = uv_udp_recv_stop(handle);
  ASSERT(r == 0);

  req = malloc(sizeof *req);
  ASSERT(req != NULL);

  buf = uv_buf_init("PONG", 4);

  r = uv_udp_send(req,
                  handle,
                  &buf,
                  1,
                  *(struct sockaddr_in*)addr,
                  sv_send_cb);
  ASSERT(r == 0);

  sv_recv_cb_called++;
}

void server_main(int client_port, int server_port){
  OpenSSL_add_ssl_algorithms();
  SSL_load_error_strings();
  SSL_CTX * server_ctx = SSL_CTX_new(DTLSv1_server_method());
  { // setup server SSL CTX
    SSL_CTX_set_cipher_list(ctx, "ALL:NULL:eNULL:aNULL");
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
  
  struct sockaddr_in addr = uv_ip4_addr("127.0.0.1", server_port);
  uv_udp_t server;
  r = uv_udp_init(uv_default_loop(), &server);
  ASSERT(r == 0);

  r = uv_udp_bind(&server, addr, 0);
  ASSERT(r == 0);
  {
    struct timeval timeout;
    BIO *   bio = BIO_new_dgram(fd, BIO_NOCLOSE);
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_RECV_TIMEOUT, 0, &timeout);
    SSL * ssl = SSL_new(ctx);
    
    SSL_set_bio(ssl, bio, bio);
    SSL_set_options(ssl, SSL_OP_COOKIE_EXCHANGE);
    struct sockaddr_in client_addr;
    while (DTLSv1_listen(ssl, &client_addr) <= 0);
    char buf[100];
    size_t len = SSL_read(ssl, buf, sizeof(buf));
    logd("Server: Read data length: %i\n", len);
    char * buf2 = "PONG";
    SSL_write(ssl, buf2, strlen(buf2));
  }
}

void client_main(int client_port, int server_port){
  OpenSSL_add_ssl_algorithms();
  SSL_load_error_strings();


  // todo: Figure out if it has to be client/server or if client/client is ok.
  SSL_CTX * client_ctx =  SSL_CTX_new(DTLSv1_client_method());
  { // setup client SSL CTX
    SSL_CTX_set_cipher_list(client_ctx, "eNULL:!MD5");
    if (!SSL_CTX_use_certificate_file(client_ctx, "certs/client-cert.pem", SSL_FILETYPE_PEM))
      printf("\nERROR: no certificate found!");
    
    if (!SSL_CTX_use_PrivateKey_file(client_ctx, "certs/client-key.pem", SSL_FILETYPE_PEM))
      printf("\nERROR: no private key found!");
    
    if (!SSL_CTX_check_private_key (client_ctx))
      printf("\nERROR: invalid private key!");
    SSL_CTX_set_verify_depth (client_ctx, 2);
    SSL_CTX_set_read_ahead(client_ctx, 1);
  }

  uv_udp_t client;
  struct sockaddr_in addr;
  int r = 0;

  ASSERT(r == 0);

  addr = uv_ip4_addr("127.0.0.1", server_port);

  r = uv_udp_init(uv_default_loop(), &client);
  ASSERT(r == 0);

  ///* client sends "PING", expects "PONG" */
  SSL *ssl = SSL_new(ctx);
  BIO * bio = BIO_new_dgram(fd, BIO_CLOSE);
  BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_CONNECTED, 0, &addr.ss);
  SSL_set_bio(ssl, bio, bio);
  ASSERT(!(SSL_connect(ssl) < 0));
  timeout.tv_sec = 3;
  timeout.tv_usec = 0;
  BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_RECV_TIMEOUT, 0, &timeout);
  
  { // coms
    char * buf = "PING";
    size_t len = SSL_write(ssl, buf, strlen(buf));
    SSL_heartbeat(ssl);
    char readbuf[100];
    len = SSL_read(ssl, readbuf, sizeof(readbuf));
    logd("Read %i bytes\n",len);
  }
}


void main() {

  OpenSSL_add_ssl_algorithms();
  SSL_load_error_strings();


  // todo: Figure out if it has to be client/server or if client/client is ok.
  SSL_CTX * client_ctx =  SSL_CTX_new(DTLSv1_client_method());
  { // setup client SSL CTX
    SSL_CTX_set_cipher_list(client_ctx, "eNULL:!MD5");
    if (!SSL_CTX_use_certificate_file(client_ctx, "certs/client-cert.pem", SSL_FILETYPE_PEM))
      printf("\nERROR: no certificate found!");
  
    if (!SSL_CTX_use_PrivateKey_file(client_ctx, "certs/client-key.pem", SSL_FILETYPE_PEM))
      printf("\nERROR: no private key found!");
  
    if (!SSL_CTX_check_private_key (client_ctx))
      printf("\nERROR: invalid private key!");
    SSL_CTX_set_verify_depth (client_ctx, 2);
    SSL_CTX_set_read_ahead(client_ctx, 1);
  }

  SSL_CTX * server_ctx = SSL_CTX_new(DTLSv1_server_method());
  { // setup server SSL CTX
    SSL_CTX_set_cipher_list(ctx, "ALL:NULL:eNULL:aNULL");
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


  struct sockaddr_in addr;
  uv_udp_send_t req;
  uv_buf_t buf;
  int r;

  addr = uv_ip4_addr("0.0.0.0", TEST_PORT);

  r = uv_udp_init(uv_default_loop(), &server);
  ASSERT(r == 0);

  r = uv_udp_bind(&server, addr, 0);
  ASSERT(r == 0);
  {
    bio = BIO_new_dgram(fd, BIO_NOCLOSE);
  }
  //r = uv_udp_recv_start(&server, alloc_cb, sv_recv_cb);
  ASSERT(r == 0);

  addr = uv_ip4_addr("127.0.0.1", TEST_PORT);

  r = uv_udp_init(uv_default_loop(), &client);
  ASSERT(r == 0);

  
  ///* client sends "PING", expects "PONG" */
  //buf = uv_buf_init("PING", 4);
  {
    SSL *ssl = SSL_new(ctx);
    BIO *bio = BIO_new_dgram(fd, BIO_CLOSE);
    BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_CONNECTED, 0, &caddr.ss);
    SSL_set_bio(ssl, bio, bio);
    ASSERT(!(SSL_connect(ssl) < 0));
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_RECV_TIMEOUT, 0, &timeout);
  }
  
  { // coms
    char * buf = "PING";
    size_t len = SSL_write(ssl, buf, strlen(buf));
    SSL_heartbeat(ssl);
    char readbuf[100];
    len = SSL_read(ssl, readbuf, sizeof(readbuf));
    logd("Read %i bytes\n",len);
      
  }
    //r = uv_udp_send(&req, &client, &buf, 1, addr, cl_send_cb);
    ASSERT(r == 0);

  ASSERT(close_cb_called == 0);
  ASSERT(cl_send_cb_called == 0);
  ASSERT(cl_recv_cb_called == 0);
  ASSERT(sv_send_cb_called == 0);
  ASSERT(sv_recv_cb_called == 0);

  uv_run(uv_default_loop(), UV_RUN_DEFAULT);

  ASSERT(cl_send_cb_called == 1);
  ASSERT(cl_recv_cb_called == 1);
  ASSERT(sv_send_cb_called == 1);
  ASSERT(sv_recv_cb_called == 1);
  ASSERT(close_cb_called == 2);

  MAKE_VALGRIND_HAPPY();
  return 0;
}
