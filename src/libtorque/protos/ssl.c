#ifndef torque_WITHOUT_SSL
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <openssl/ssl.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
#include <libtorque/buffers.h>
#include <libtorque/schedule.h>
#include <libtorque/protos/ssl.h>
#include <libtorque/events/thread.h>

static unsigned numlocks;
static pthread_mutex_t *openssl_locks;

struct CRYPTO_dynlock_value {
	pthread_mutex_t mutex; // FIXME use a read-write lock
};

typedef struct ssl_cbstate {
	SSL_CTX *sslctx;
	SSL *ssl;
	void *cbstate;
	libtorquebrcb rxfxn;
	libtorquebwcb txfxn;
	torque_rxbuf rxb;
} ssl_cbstate;

struct ssl_cbstate *
create_ssl_cbstate(struct torque_ctx *ctx,SSL_CTX *sslctx,void *cbstate,
					libtorquebrcb rx,libtorquebwcb tx){
	ssl_cbstate *ret;

	if( (ret = malloc(sizeof(*ret))) ){
		if(initialize_rxbuffer(ctx,&ret->rxb) == 0){
			ret->sslctx = sslctx;
			ret->cbstate = cbstate;
			ret->rxfxn = rx;
			ret->txfxn = tx;
			ret->ssl = NULL;
			return ret;
		}
		free(ret);
	}
	return NULL;
}

void free_ssl_cbstate(ssl_cbstate *sc){
	if(sc){
		SSL_free(sc->ssl);
		free_rxbuffer(&sc->rxb);
		free(sc);
	}
}

int torque_stop_ssl(void){
	int ret = 0;
	unsigned z;

	RAND_cleanup();
	CRYPTO_set_locking_callback(NULL);
	CRYPTO_set_id_callback(NULL);
	for(z = 0 ; z < numlocks ; ++z){
		if(pthread_mutex_destroy(&openssl_locks[z])){
			ret = -1;
		}
	}
	numlocks = 0;
	free(openssl_locks);
	openssl_locks = NULL;
	return ret;
}

#if !defined(OPENSSL_API_COMPAT) || OPENSSL_API_COMPAT >= 0x10000000L
// See threads(3SSL)
static void
openssl_lock_callback(int mode,int n,const char *file __attribute__ ((unused)),
				int line __attribute__ ((unused))){
	if(mode & CRYPTO_LOCK){
		pthread_mutex_lock(&openssl_locks[n]);
	}else{
		pthread_mutex_unlock(&openssl_locks[n]);
	}
}

static unsigned long
openssl_id_callback(void){
	// FIXME pthread_self() doesn't return an integer type on (at least)
	// FreeBSD...argh :(
	return pthread_self_getnumeric();
}

static struct CRYPTO_dynlock_value *
openssl_dyncreate_callback(const char *file __attribute__ ((unused)),
				int line __attribute__ ((unused))){
	struct CRYPTO_dynlock_value *ret;

	if( (ret = malloc(sizeof(*ret))) ){
		if(pthread_mutex_init(&ret->mutex,NULL)){
			free(ret);
			return NULL;
		}
	}
	return ret;
}

static void
openssl_dynlock_callback(int mode,struct CRYPTO_dynlock_value *l,
				const char *file __attribute__ ((unused)),
				int line __attribute__ ((unused))){
	if(mode & CRYPTO_LOCK){
		pthread_mutex_lock(&l->mutex);
	}else{
		pthread_mutex_unlock(&l->mutex);
	}
}

static void
openssl_dyndestroy_callback(struct CRYPTO_dynlock_value *l,
				const char *file __attribute__ ((unused)),
				int line __attribute__ ((unused))){
	pthread_mutex_destroy(&l->mutex);
	free(l);
}
#endif

static int
openssl_verify_callback(int preverify_ok,X509_STORE_CTX *xctx __attribute__ ((unused))){
	if(preverify_ok){ // otherwise, OpenSSL's already blackballed the conn
		// FIXME check CRL etc
	}
	return preverify_ok;
}

// This is currently server-oriented. Provide client functionality also FIXME.
SSL_CTX *torque_ssl_ctx(const char *certfile,const char *keyfile,
			const char *cafile,unsigned cliver){
	unsigned char sessionid[64 / CHAR_BIT]; // FIXME really...? REALLY?!?
	SSL_CTX *ret;

	// Need to accept SSLv2, SSLv3, and TLSv1 ClientHellos if we want
	// multiple protocol support at all (we do, the latter two).
	if((ret = SSL_CTX_new(SSLv23_method())) == NULL){
		return NULL;
	}
	// But we do not want to actually serve insecure SSLv2.
	if(!(SSL_CTX_set_options(ret,SSL_OP_NO_SSLv2) & SSL_OP_NO_SSLv2)){
		SSL_CTX_free(ret);
		return NULL;
	}
	// Create a random SessionID (and thus verify RNG functionality).
	if(RAND_bytes(sessionid,sizeof(sessionid)) < 0){
		SSL_CTX_free(ret);
		return NULL;
	}
	// Enable SSL session resumption by setting a per-libtorque SessionID.
	if(SSL_CTX_set_session_id_context(ret,sessionid,sizeof(sessionid)) != 1){
		SSL_CTX_free(ret);
		return NULL;
	}
	// The CA's we trust. We must still ensure the certificate chain is
	// semantically valid, not just syntactically valid! This is done via
	// checking X509 properties and a CRL in openssl_verify_callback().
	if(cafile){
		if(SSL_CTX_load_verify_locations(ret,cafile,NULL) != 1){
			SSL_CTX_free(ret);
			return NULL;;
		}
	}
	if(cliver){
		SSL_CTX_set_verify(ret,SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
					openssl_verify_callback);
	}else{
		SSL_CTX_set_verify(ret,SSL_VERIFY_NONE,NULL);
	}
	if(!(keyfile || certfile)){
		return ret;
	}
	if(SSL_CTX_use_PrivateKey_file(ret,keyfile,SSL_FILETYPE_PEM) == 1){
		if(SSL_CTX_use_certificate_chain_file(ret,certfile) == 1){
			return ret;
		}
	}else if(SSL_CTX_use_PrivateKey_file(ret,keyfile,SSL_FILETYPE_ASN1) == 1){
		if(SSL_CTX_use_certificate_chain_file(ret,certfile) == 1){
			return ret;
		}
	}
	SSL_CTX_free(ret);
	return NULL;
}

int torque_init_ssl(void){
	int nlocks;
	unsigned z;

	if(SSL_library_init() != 1){
		return -1;
	}
	SSL_load_error_strings();
	// OpenSSL transparently seeds the entropy pool on systems supporting a
	// /dev/urandom device. Otherwise, one needs seed it using truly random
	// data from some source (EGD, preserved file, etc). We currently just
	// hope for a /dev/urandom, but at least verify the pool's OK FIXME.
	if(RAND_status() != 1){
		return -1;
	}
	if((nlocks = CRYPTO_num_locks()) < 0){
		RAND_cleanup();
		return -1;
	}else if(nlocks == 0){ // assume no need for threading
		return 0;
	}
	numlocks = nlocks;
	if((openssl_locks = malloc(sizeof(*openssl_locks) * numlocks)) == NULL){
		RAND_cleanup();
		return -1;
	}
	for(z = 0 ; z < numlocks ; ++z){
		if(pthread_mutex_init(&openssl_locks[z],NULL)){
			while(z--){
				pthread_mutex_destroy(&openssl_locks[z]);
			}
			free(openssl_locks);
			openssl_locks = NULL;
			numlocks = 0;
			RAND_cleanup();
			return -1;
		}
	}
	CRYPTO_set_locking_callback(openssl_lock_callback);
	CRYPTO_set_id_callback(openssl_id_callback);
	CRYPTO_set_dynlock_create_callback(openssl_dyncreate_callback);
	CRYPTO_set_dynlock_lock_callback(openssl_dynlock_callback);
	CRYPTO_set_dynlock_destroy_callback(openssl_dyndestroy_callback);
	return 0;
}

SSL *new_ssl_conn(SSL_CTX *ctx){
	return SSL_new(ctx);
}

static void ssl_rxfxn(int,void *);

int ssl_tx(int fd,ssl_cbstate *ssl,const void *buf,int len){
	int ret = 0;

	while(ret < len){
		int r;

		if((r = SSL_write(ssl->ssl,(const char *)buf + ret,len - ret)) >= 0){
			ret += r;
		}else{
			int err = SSL_get_error(ssl->ssl,r);

			if(err == SSL_ERROR_WANT_READ){
				torque_ctx *ctx = get_thread_ctx();

				set_evsource_rx(ctx->eventtables.fdarray,fd,ssl_rxfxn);
				set_evsource_tx(ctx->eventtables.fdarray,fd,NULL);
				if(restorefd(get_thread_evh(),fd,EVREAD)){
					return -1;
				}
			}else if(err == SSL_ERROR_WANT_WRITE){
				if(restorefd(get_thread_evh(),fd,EVREAD|EVWRITE)){ // just let it loop
					return -1;
				}
			}else{
				return -1;
			}
			break;
		}
	}
	return ret;
}

static void
ssl_txrxfxn(int fd,void *cbs){
	ssl_cbstate *sc = cbs;
	int r,err;

	while((r = rxbuffer_ssl(&sc->rxb,sc->ssl)) > 0){
		if(sc->rxfxn(fd,&sc->rxb,sc)){
			goto err;
		}
	}
	err = SSL_get_error(sc->ssl,r);
	if(err == SSL_ERROR_WANT_READ){
		torque_ctx *ctx = get_thread_ctx();

		set_evsource_rx(ctx->eventtables.fdarray,fd,ssl_rxfxn);
		set_evsource_tx(ctx->eventtables.fdarray,fd,NULL);
		if(restorefd(get_thread_evh(),fd,EVREAD)){
			goto err;
		}
	}else if(err == SSL_ERROR_WANT_WRITE){
		if(restorefd(get_thread_evh(),fd,EVREAD|EVWRITE)){ // just let it loop
			goto err;
		}
	}else{
		goto err;
	}
	return;

err:
	free_ssl_cbstate(sc);
	close(fd);
	return;
}

static void
ssl_rxfxn(int fd,void *cbstate){
	ssl_cbstate *sc = cbstate;
	int r,err;

	while((r = rxbuffer_ssl(&sc->rxb,sc->ssl)) >= 0){
		if(sc->rxfxn(fd,&sc->rxb,sc)){
			goto err;
		}
	}
	err = SSL_get_error(sc->ssl,r);
	if(err == SSL_ERROR_WANT_WRITE){
		torque_ctx *ctx = get_thread_ctx();

		set_evsource_rx(ctx->eventtables.fdarray,fd,NULL);
		set_evsource_tx(ctx->eventtables.fdarray,fd,ssl_txrxfxn);
		if(restorefd(get_thread_evh(),fd,EVWRITE|EVREAD)){
			goto err;
		}
	}else if(err == SSL_ERROR_WANT_READ){
		if(restorefd(get_thread_evh(),fd,EVREAD)){ // just let it loop
			goto err;
		}
	}else{
		goto err;
	}
	return;

err:
	free_ssl_cbstate(sc);
	close(fd);
	return;
}

static void
ssl_txfxn(int fd,void *cbs){
	ssl_cbstate *sc = cbs;

	if(sc->txfxn == NULL){
		free_ssl_cbstate(sc);
		close(fd);
	}else{
		if(sc->txfxn(fd,&sc->rxb,sc)){
			goto err;
		}
	}
	return;

err:
	free_ssl_cbstate(sc);
	close(fd);
	return;
}

static void accept_conttxfxn(int,void *);

static void
accept_contrxfxn(int fd,void *cbstate){
	torque_ctx *ctx = get_thread_ctx();
	evhandler *evh = get_thread_evh();
	ssl_cbstate *sc = cbstate;
	int ret;

	if((ret = SSL_accept(sc->ssl)) == 1){
		libtorquercb rx = sc->rxfxn ? ssl_rxfxn : NULL;
		libtorquewcb tx = sc->txfxn ? ssl_txfxn : NULL;

		if(initialize_rxbuffer(ctx,&sc->rxb)){
			goto err;
		}
		set_evsource_rx(ctx->eventtables.fdarray,fd,rx);
		set_evsource_tx(ctx->eventtables.fdarray,fd,tx);
		if(restorefd(evh,fd,(rx ? EVREAD : 0) | (tx ? EVWRITE : 0))){
			goto err;
		}
	}else{
		int err = SSL_get_error(sc->ssl,ret);

		if(err == SSL_ERROR_WANT_WRITE){
			set_evsource_rx(ctx->eventtables.fdarray,fd,NULL);
			set_evsource_tx(ctx->eventtables.fdarray,fd,accept_conttxfxn);
			if(restorefd(evh,fd,EVWRITE)){
				goto err;
			}
		}else if(err == SSL_ERROR_WANT_READ){
			if(restorefd(evh,fd,EVREAD)){ // just let it loop
				goto err;
			}
		}else{
			goto err;
		}
	}
	return;

err:
	free_ssl_cbstate(sc);
	close(fd);
}

static void
accept_conttxfxn(int fd,void *cbs){
	torque_ctx *ctx = get_thread_ctx();
	ssl_cbstate *sc = cbs;
	int ret;

	if((ret = SSL_accept(sc->ssl)) == 1){
		libtorquercb rx = sc->rxfxn ? ssl_rxfxn : NULL;
		libtorquewcb tx = sc->txfxn ? ssl_txfxn : NULL;

		if(initialize_rxbuffer(ctx,&sc->rxb)){
			goto err;
		}
		set_evsource_rx(ctx->eventtables.fdarray,fd,rx);
		set_evsource_tx(ctx->eventtables.fdarray,fd,tx);
	}else{
		int err = SSL_get_error(sc->ssl,ret);

		if(err == SSL_ERROR_WANT_READ){
			set_evsource_rx(ctx->eventtables.fdarray,fd,accept_contrxfxn);
			set_evsource_tx(ctx->eventtables.fdarray,fd,NULL);
		}else if(err == SSL_ERROR_WANT_WRITE){
			// just let it loop
		}else{
			goto err;
		}
	}
	return;

err:
	free_ssl_cbstate(sc);
	close(fd);
}

static inline int
ssl_accept_internal(int sd,const ssl_cbstate *sc){
	torque_ctx *ctx = get_thread_ctx();
	ssl_cbstate *csc;
	int ret;

	csc = create_ssl_cbstate(ctx,sc->sslctx,sc->cbstate,sc->rxfxn,sc->txfxn);
	if(csc == NULL){
		return -1;
	}
	if((csc->ssl = SSL_new(sc->sslctx)) == NULL){
		free_ssl_cbstate(csc);
		return -1;
	}
	if(SSL_set_fd(csc->ssl,sd) != 1){
		free_ssl_cbstate(csc);
		return -1;
	}
	if((ret = SSL_accept(csc->ssl)) == 1){
		libtorquercb rx = sc->rxfxn ? ssl_rxfxn : NULL;
		libtorquewcb tx = sc->txfxn ? ssl_txfxn : NULL;

		if(initialize_rxbuffer(ctx,&csc->rxb)){
			free_ssl_cbstate(csc);
			return -1;
		}
		if(torque_addfd_unbuffered(ctx,sd,rx,tx,csc)){
			free_ssl_cbstate(csc);
			return -1;
		}
	}else{
		int err;

		err = SSL_get_error(csc->ssl,ret);
		if(err == SSL_ERROR_WANT_WRITE){
			if(torque_addfd_unbuffered(ctx,sd,NULL,accept_conttxfxn,csc)){
				free_ssl_cbstate(csc);
				return -1;
			}
		}else if(err == SSL_ERROR_WANT_READ){
			if(torque_addfd_unbuffered(ctx,sd,accept_contrxfxn,NULL,csc)){
				free_ssl_cbstate(csc);
				return -1;
			}
		}else{
			free_ssl_cbstate(csc);
			return -1;
		}
	}
	return 0;
}

void ssl_accept_rxfxn(int fd,void *cbstate){
	struct sockaddr sina;
	socklen_t slen;
	int sd;

	do{
		int flags;

		slen = sizeof(sina);
		while((sd = accept(fd,&sina,&slen)) < 0){
			if(errno != EINTR){ // loop on EINTR
				if(restorefd(get_thread_evh(),fd,EVREAD)){
					// FIXME stat?;
				}
				return;
			}
		}
		if(((flags = fcntl(sd,F_GETFL)) < 0) || fcntl(sd,F_SETFL,flags | O_NONBLOCK)){
			close(sd);
		}else if(ssl_accept_internal(sd,cbstate)){
			close(sd);
		}
	}while(1);
}
#endif
