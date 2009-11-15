#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <libtorque/events/sysdep.h>
#include <libtorque/events/sources.h>

evsource *create_evsources(unsigned n){
	evsource *evs;

	if( (evs = malloc(sizeof(*evs) * n)) ){
		memset(evs,0,sizeof(*evs) * n);
	}
	return evs;
}

// We need no locking here, because the only time someone should call
// setup_evsource is when they've been handed the file descriptor from the OS,
// and not handed it off to anything else which would register it. If it was
// already being used, it must have been removed from the event queue (by
// guarantees of the epoll/kqueue mechanisms), and thus no events exist for it.
void setup_evsource(evsource *evs,int n,libtorquecb rfxn,libtorquecb tfxn,void *v){
	evs[n].rxfxn = rfxn;
	evs[n].txfxn = tfxn;
	evs[n].cbstate = v;
}

int destroy_evsources(evsource *evs,unsigned n __attribute__ ((unused))){
	int ret = 0;

	if(evs){
		free(evs);
	}
	return ret;
}
