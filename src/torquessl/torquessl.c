#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <stdlib.h>
#include <pthread.h>
#include <libtorque/libtorque.h>

static int
ssl_conn_handler(int fd,void *v){
	if(fd < 0){
		return -1;
	}
	if(v == NULL){
		return -1;
	}
	return 0;
}

int main(void){
	struct libtorque_ctx *ctx = NULL;
	sigset_t termset;
	int sig,sd = -1;

	sigemptyset(&termset);
	sigaddset(&termset,SIGTERM);
	if(pthread_sigmask(SIG_SETMASK,&termset,NULL)){
		fprintf(stderr,"Couldn't set signal mask\n");
		return EXIT_FAILURE;
	}
	if((ctx = libtorque_init()) == NULL){
		fprintf(stderr,"Couldn't initialize libtorque\n");
		goto err;
	}
	if(libtorque_addfd(ctx,sd,ssl_conn_handler,NULL,NULL)){
		fprintf(stderr,"Couldn't add SSL sd %d\n",sd);
		goto err;
	}
	if(sigwait(&termset,&sig)){
		fprintf(stderr,"Couldn't wait on signals\n");
		goto err;
	}
	printf("Got signal %d (%s), closing down...\n",sig,strsignal(sig));
	if(libtorque_stop(ctx)){
		fprintf(stderr,"Couldn't shutdown libtorque\n");
		return EXIT_FAILURE;
	}
	printf("Successfully cleaned up.\n");
	return EXIT_SUCCESS;

err:
	if(libtorque_stop(ctx)){
		fprintf(stderr,"Couldn't shutdown libtorque\n");
		return EXIT_FAILURE;
	}
	return EXIT_FAILURE;
}
