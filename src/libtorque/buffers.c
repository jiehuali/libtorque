#include <errno.h>
#include <unistd.h>
#include <libtorque/buffers.h>
#include <libtorque/libtorque.h>
#include <libtorque/events/thread.h>

static inline int
rxback(libtorque_rxbuf *rxb,int fd,void *cbstate){
	if(rxb->bufoff - rxb->bufate){
		return rxb->rx(fd,rxb,cbstate);
	}
	return 0;
}

static inline int
txback(libtorque_rxbuf *rxb,int fd,void *cbstate){
	if(rxb->bufoff - rxb->bufate){
		return rxb->tx(fd,rxb,cbstate);
	}
	return 0;
}

static int
growrxbuf(libtorque_rxbuf *rxb){
	typeof(*rxb->buffer) *tmp;
	size_t news;

	news = rxb->buftot * 2; // FIXME hrmmm
	if((tmp = mod_pages(rxb->buffer,rxb->buftot,news)) == NULL){
		return -1;
	}
	rxb->buffer = tmp;
	rxb->buftot = news;
	return 0;
}

void buffered_txfxn(int fd,void *cbstate){
	libtorque_rxbufcb *cbctx = cbstate;
	libtorque_rxbuf *rxb = &cbctx->rxbuf;
	int cb;

	// FIXME very likely incomplete
	if((cb = txback(rxb,fd,cbstate)) < 0){
		goto err;
	}
	if(restorefd(get_thread_evh(),fd,EPOLLIN)){
		goto err;
	}
	return;

err:
	close(fd);
}

void buffered_rxfxn(int fd,void *cbstate){
	libtorque_rxbufcb *cbctx = cbstate;
	libtorque_rxbuf *rxb = &cbctx->rxbuf;
	int r;

	for( ; ; ){
		if(rxb->buftot - rxb->bufoff == 0){
			int cb;

			if((cb = rxback(rxb,fd,cbstate)) < 0){
				break;
			}
			if(rxb->buftot - rxb->bufoff == 0){
				if(growrxbuf(rxb)){
					break;
				}
			}
		}
		if((r = read(fd,rxb->buffer + rxb->bufoff,rxb->buftot - rxb->bufoff)) > 0){
			rxb->bufoff += r;
		}else if(r == 0){
			int cb;

			// must close, *unless* TX indicated FIXME
			if((cb = rxback(rxb,fd,cbstate)) <= 0){
				break;
			}
			if(restorefd(get_thread_evh(),fd,EPOLLOUT)){
				break;
			}
			return;
		}else if(errno == EAGAIN || errno == EWOULDBLOCK){
			int cb;

			if((cb = rxback(rxb,fd,cbstate)) < 0){
				break;
			}
			if(restorefd(get_thread_evh(),fd,EPOLLIN | (cb ? EPOLLOUT : 0))){
				break;
			}
			return;
		}else if(errno != EINTR){
			break;
		}
	}
	close(fd);
}
