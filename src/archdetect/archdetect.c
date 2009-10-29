#include <stdio.h>
#include <stdlib.h>
#include <libtorque/arch.h>
#include <libtorque/libtorque.h>

static int
detail_processing_unit(const libtorque_cputype *pudesc){
	if(pudesc->elements <= 0){
		fprintf(stderr,"Error: element count of 0\n");
		return -1;
	}
	if(pudesc->memories <= 0){
		fprintf(stderr,"Error: memory count of 0\n");
		return -1;
	}
	printf("\tCount: %u\n",pudesc->elements);
	printf("\tMemories: %u\n",pudesc->memories);
	return 0;
}

static int
detail_processing_units(unsigned cpu_typecount){
	unsigned n;

	for(n = 0 ; n < cpu_typecount ; ++n){
		const libtorque_cputype *pudesc;

		if((pudesc = libtorque_cpu_getdesc(n)) == NULL){
			fprintf(stderr,"Couldn't look up CPU type %u\n",n);
			return EXIT_FAILURE;
		}
		printf("Processing unit type %u of %u:\n",n + 1,cpu_typecount);
		if(detail_processing_unit(pudesc)){
			return -1;
		}
	}
	return 0;
}

int main(void){
	unsigned cpu_typecount;

	if(libtorque_init()){
		fprintf(stderr,"Couldn't initialize libtorque\n");
		return EXIT_FAILURE;
	}
	if((cpu_typecount = libtorque_cpu_typecount()) <= 0){
		fprintf(stderr,"Got invalid CPU type count: %u\n",cpu_typecount);
		return EXIT_FAILURE;
	}
	if(detail_processing_units(cpu_typecount)){
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}