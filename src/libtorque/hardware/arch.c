#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <libtorque/internal.h>
#include <libtorque/hardware/arch.h>
#include <libtorque/hardware/memory.h>
#include <libtorque/hardware/x86cpuid.h>
#include <libtorque/hardware/topology.h>

// Returns the slot we just added to the end, or NULL on failure. Pointers
// will be shallow-copied; dynamically allocate them, and do not free them
// explicitly (they'll be handled by free_cpudetails()).
static inline libtorque_cput *
add_cputype(unsigned *cputc,libtorque_cput **types,
		const libtorque_cput *acpu){
	size_t s = (*cputc + 1) * sizeof(**types);
	typeof(**types) *tmp;

	if((tmp = realloc(*types,s)) == NULL){
		return NULL;
	}
	*types = tmp;
	(*types)[*cputc] = *acpu;
	return *types + (*cputc)++;
}

static void
free_cpudetails(libtorque_cput *details){
	free(details->tlbdescs);
	details->tlbdescs = NULL;
	free(details->memdescs);
	details->memdescs = NULL;
	free(details->strdescription);
	details->strdescription = NULL;
	details->stepping = details->model = details->family = 0;
	details->tlbs = details->elements = details->memories = 0;
	details->coresperpackage = details->threadspercore = 0;
	details->x86type = PROCESSOR_X86_UNKNOWN;
}

// Methods to discover processor and cache details include:
//  - running CPUID (must be run on each processor, x86 only)
//  - querying cpuid(4) devices (linux only, must be root, x86 only)
//  - CPUCTL ioctl(2)s (freebsd only, with cpuctl device, x86 only)
//  - /proc/cpuinfo (linux only)
//  - /sys/devices/{system/cpu/*,/virtual/cpuid/*} (linux only)
static int
detect_cpudetails(unsigned id,libtorque_cput *details,
			unsigned *thread,unsigned *core,unsigned *pkg){
	if(pin_thread(id)){
		return -1;
	}
	if(x86cpuid(details,thread,core,pkg)){
		free_cpudetails(details);
		return -1;
	}
	return 0;
}

static int
compare_cpudetails(const libtorque_cput * restrict a,
			const libtorque_cput * restrict b){
	if(a->family != b->family || a->model != b->model ||
		a->stepping != b->stepping || a->x86type != b->x86type){
		return -1;
	}
	if(a->memories != b->memories || a->tlbs != b->tlbs){
		return -1;
	}
	if(a->threadspercore != b->threadspercore || a->coresperpackage != b->coresperpackage){
		return -1;
	}
	if(strcmp(a->strdescription,b->strdescription)){
		return -1;
	}
	if(memcmp(a->tlbdescs,b->tlbdescs,a->tlbs * sizeof(*a->tlbdescs))){
		return -1;
	}
	if(memcmp(a->memdescs,b->memdescs,a->memories * sizeof(*a->memdescs))){
		return -1;
	}
	return 0;
}

static libtorque_cput *
match_cputype(unsigned cputc,libtorque_cput *types,
		const libtorque_cput *acpu){
	unsigned n;

	for(n = 0 ; n < cputc ; ++n){
		if(compare_cpudetails(types + n,acpu) == 0){
			return types + n;
		}
	}
	return NULL;
}

// Might leave the calling thread pinned to a particular processor; restore the
// CPU mask if necessary after a call.
static int
detect_cputypes(libtorque_ctx *ctx,unsigned *cputc,libtorque_cput **types){
	unsigned totalpe,z,cpu;
	cpu_set_t mask;

	*cputc = 0;
	*types = NULL;
	if((totalpe = detect_cpucount(ctx,&mask)) <= 0){
		goto err;
	}
	for(z = 0, cpu = 0 ; z < totalpe ; ++z){
		libtorque_cput cpudetails;
		unsigned thread,core,pkg;
		typeof(*types) cputype;

		while(cpu < CPU_SETSIZE && !CPU_ISSET(cpu,&mask)){
			++cpu;
		}
		if(cpu == CPU_SETSIZE){
			goto err;
		}
		if(detect_cpudetails(cpu,&cpudetails,&thread,&core,&pkg)){
			goto err;
		}
		if( (cputype = match_cputype(*cputc,*types,&cpudetails)) ){
			++cputype->elements;
			free_cpudetails(&cpudetails);
		}else{
			cpudetails.elements = 1;
			if((cputype = add_cputype(cputc,types,&cpudetails)) == NULL){
				free_cpudetails(&cpudetails);
				goto err;
			}
		}
		if(associate_affinityid(ctx,cpu,(unsigned)(cputype - *types)) < 0){
			goto err;
		}
		if(topologize(ctx,cpu,thread,core,pkg)){
			goto err;
		}
		++cpu;
	}
	if(unpin_thread(ctx)){
		goto err;
	}
	return 0;

err:
	unpin_thread(ctx);
	while((*cputc)--){
		free_cpudetails((*types) + ctx->cpu_typecount);
	}
	*cputc = 0;
	free(*types);
	*types = NULL;
	return -1;
}

int detect_architecture(libtorque_ctx *ctx){
	if(detect_memories(ctx)){
		goto err;
	}
	if(detect_cputypes(ctx,&ctx->cpu_typecount,&ctx->cpudescs)){
		goto err;
	}
	return 0;

err:
	free_architecture(ctx);
	return -1;
}

void free_architecture(libtorque_ctx *ctx){
	reset_topology(ctx);
	while(ctx->cpu_typecount--){
		free_cpudetails(&ctx->cpudescs[ctx->cpu_typecount]);
	}
	ctx->cpu_typecount = 0;
	free(ctx->cpudescs);
	ctx->cpudescs = NULL;
	free_memories(ctx);
}

unsigned libtorque_cpu_typecount(const libtorque_ctx *ctx){
	return ctx->cpu_typecount;
}

// Takes a description ID
const libtorque_cput *libtorque_cpu_getdesc(const libtorque_ctx *ctx,unsigned n){
	if(n >= ctx->cpu_typecount){
		return NULL;
	}
	return &ctx->cpudescs[n];
}
