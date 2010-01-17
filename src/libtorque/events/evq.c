#include <unistd.h>
#include <libtorque/events/evq.h>
#include <libtorque/events/thread.h>

int destroy_evqueue(evqueue *evq){
	int ret = 0;

	if(pthread_mutex_lock(&evq->lock)){
		return -1;
	}
	if(--evq->refcount == 0){
		ret |= close(evq->efd);
		evq->efd = -1;
	}
	ret |= pthread_mutex_unlock(&evq->lock);
	return ret;
}

int ref_evqueue(evqueue *e){
	int ret = 0;

	if(pthread_mutex_lock(&e->lock)){
		return -1;
	}
	++e->refcount;
	ret |= pthread_mutex_unlock(&e->lock);
	return ret;
}

#ifdef LIBTORQUE_LINUX
// fd is the common signalfd
static inline int
add_commonfds_to_evhandler(int fd,evqueue *evq){
	struct epoll_ctl_data ecd;
	struct epoll_event ee;
	struct kevent k;

	if(fd < 0){
		return -1;
	}
	memset(&ee.data,0,sizeof(ee.data));
	ee.data.fd = fd;
	// We automatically wait for EPOLLERR/EPOLLHUP; according to
	// epoll_ctl(2), "it is not necessary to add set [these] in ->events"
	// We do NOT use edge-triggered polling for this internal handler, for
	// obscure technical reasons (a thread which bails can't easily empty
	// the pending signal queue, at least not without reading everything
	// else, putting it on an event queue, and then swapping the exit back
	// in from some other queue). So no EPOLLET.
	ee.events = EPOLLIN;
	k.events = &ee;
	k.ctldata = &ecd;
	ecd.op = EPOLL_CTL_ADD;
	if(add_evector_kevents(evq,&k,1)){
		return -1;
	}
	return 0;
}
#endif

static inline int
add_evqueue_baseevents(const libtorque_ctx *ctx,evqueue *e){
#ifdef LIBTORQUE_FREEBSD
	EVECTOR_AUTOS(8,ev);

		for(z = 1 ; z < eh->evsources->sigarraysize ; ++z){
			struct kevent k;

			if(!sigismember(sigs,z)){
				continue;
			}
			EV_SET(&k,z,EVFILT_SIGNAL,EV_ADD | EV_CLEAR,0,0,NULL);
			if(add_evector_kevents(ev,&k,1)){
				if(flush_evector_changes(eh,ev)){
					return -1;
				}
			}
		}
		if(flush_evector_changes(eh,ev)){
			return -1;
		}
	}
	if(add_signal_to_evhandler(e,&s,rxcommonsignal,NULL)){
		return -1;
	}
	return 0;
#elif defined(LIBTORQUE_LINUX_SIGNALFD)
	return add_commonfds_to_evhandler(ctx->eventtables.common_signalfd,e);
#elif defined(LIBTORQUE_LINUX)
	if(!ctx || !e){ // FIXME just to silence -Wunused warnings, blargh
		return -1;
	}
	return 0;
#endif
}

int init_evqueue(libtorque_ctx *ctx,evqueue *e){
	if(pthread_mutex_init(&e->lock,NULL)){
		return -1;
	}
	if((e->efd = create_efd()) < 0){
		pthread_mutex_destroy(&e->lock);
		return -1;
	}
	if(add_evqueue_baseevents(ctx,e)){
		close(e->efd);
		pthread_mutex_destroy(&e->lock);
		return -1;
	}
	e->refcount = 1;
	return 0;
}
