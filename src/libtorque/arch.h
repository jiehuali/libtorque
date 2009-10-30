#ifndef LIBTORQUE_ARCH
#define LIBTORQUE_ARCH

#ifdef __cplusplus
extern "C" {
#endif

typedef struct libtorque_tlbtype {
} libtorque_tlbtype;

typedef struct libtorque_memt {
	unsigned totalsize,linesize,associativity,sharedways;
} libtorque_memt;

typedef struct libtorque_cput {
	unsigned elements,memories;
	int family,model,stepping,extendedsig;	// x86-specific; union?
	char *strdescription;
	libtorque_memt *memdescs;
	unsigned *apicids;			// FIXME not yet filled in
} libtorque_cput;

unsigned libtorque_cpu_typecount(void) __attribute__ ((visibility("default")));

const libtorque_cput *libtorque_cpu_getdesc(unsigned)
	__attribute__ ((visibility("default")));

// Remaining declarations are internal to libtorque via -fvisibility=hidden
int detect_architecture(void);
void free_architecture(void);
int pin_thread(int);
int unpin_thread(void);

#ifdef __cplusplus
}
#endif

#endif
