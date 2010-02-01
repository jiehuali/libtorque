#ifndef LIBTORQUE_INTERNAL
#define LIBTORQUE_INTERNAL

// Typedefs internal to libtorque. This file should not be installed, nor
// #include'd by applications (save those built as part of the libtorque
// distribution). Anything seen by userspace must be prefixed with libtorque_.
// These ought only be opaque types, so the actual typedefs are free of this
// requirement (although I've usually used the same identifier).

#include <stdio.h>
#include <stdint.h>
#include <libtorque/schedule.h>
#include <libtorque/libtorque.h>
#include <libtorque/protos/dns.h>
#include <libtorque/events/sysdep.h>

struct evsource;
struct evhandler;

// See the comment in hardware/topology.h. For simply walking the topology, the
// following rules apply:
//  - the schedulable set of cpus for a group is the union of schedulable cpus
//     of its subgroups.
//  - every group has at least one schedulable cpu.
//  - groupids are relevant only within a set of siblings.
// Leaf nodes will have cpudesc set, suitable for lookup into ctx->cpudescs
//  (note that a leaf node cannot encompass heterogeneous setups).
typedef struct libtorque_topt {
	cpu_set_t schedulable;
	unsigned groupid;
	unsigned cpudesc;		// only meaningful when sub == NULL
	struct libtorque_topt *next,*sub;
} libtorque_topt;

// A node is defined as an area where all memory has the same speed as seen
// from some arbitrary set of CPUs (ignoring caches).
typedef struct libtorque_nodet {
	uintmax_t size;			// total node size
	size_t *psizevals;		// number of page sizes
	unsigned psizes;		// architecturally-supported pagesizes
	unsigned count;			// how many of these nodes do we have
	unsigned nodeid;		// libNUMA node id
} libtorque_nodet;

typedef enum {
	MEMTYPE_UNKNOWN,
	MEMTYPE_CODE,
	MEMTYPE_DATA,
	MEMTYPE_UNIFIED
} libtorque_memtypet;

typedef enum {
	PROCESSOR_X86_OEM = 0,
	PROCESSOR_X86_OVERDRIVE = 1,
	PROCESSOR_X86_DUAL = 2,
	PROCESSOR_X86_UNKNOWN
} libtorque_x86typet;

typedef struct libtorque_tlbt {
	unsigned entries,pagesize,associativity,sharedways;
	libtorque_memtypet tlbtype;
	unsigned level;
} libtorque_tlbt;

typedef struct libtorque_memt {
	uintmax_t totalsize;
	unsigned linesize,associativity,sharedways;
	libtorque_memtypet memtype;
	unsigned level;
} libtorque_memt;

typedef struct libtorque_cput {
	unsigned elements;		// Usable processors of this type
	unsigned memories;		// Number of memories for this type
	unsigned family,model,stepping;	// x86-specific; perhaps use a union?
					// family and model include the
					// extended family and model bits
	libtorque_x86typet x86type;	// stupid bullshit
	unsigned tlbs;			// Number of TLBs (per-mem?)
	unsigned threadspercore;	// Number of ways our core is shared
	unsigned coresperpackage;	// Number of cores sharing our die
	libtorque_tlbt *tlbdescs;	// TLB descriptors, NULL if tlbs == 0
	char *strdescription;		// Vender-specific string description
	libtorque_memt *memdescs;	// Memory descriptors, never NULL
} libtorque_cput;

typedef struct evtables {
	struct evsource *fdarray,*sigarray;
	unsigned sigarraysize,fdarraysize;
#ifdef LIBTORQUE_LINUX_SIGNALFD
	int common_signalfd;
#endif
} evtables;

// evqueues are shared among some number (possibly 1) of threads. currently,
// all threads share a single evqueue, but this will almost certainly change..
typedef struct evqueue {
	int efd;			// epoll() or kqueue() file descriptor
	dns_state dnsctx;		// DNS resolution state
} evqueue;

// Whenever a field is added to this structure, make sure it's
//  a) initialized in create_torque_ctx(), and
//  b) free()d (and reset) in the appropriate cleanup
// A context describes the system state as it was when torque_init() was called,
// and as it was then restricted (ie, only those processing elements in our
// cpuset, and only those NUMA nodes which we can reach).
typedef struct torque_ctx {
	evqueue evq;			// shared evq
	unsigned nodecount;		// number of NUMA nodes
	unsigned cpu_typecount;		// number of processing element types
	libtorque_cput *cpudescs;	// dynarray of cpu_typecount elements
	libtorque_nodet *manodes;	// dynarray of NUMA node descriptors
	libtorque_topt *sched_zone;	// interconnection DAG (see topology.h)
	evtables eventtables;		// callback state tables
	struct evhandler *ev;		// evhandler of list leader FIXME purge
} torque_ctx;

#endif
