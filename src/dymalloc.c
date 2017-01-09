/***
 **** Author: Rubin Thomas
*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include "sfmm.h"
#include "hw3.h"
 

sf_free_header* freelist_head = NULL;
sf_free_header* current_free_head = NULL;

void* heapStart = NULL;
void* heapEnd = NULL;

#define PAGE_SIZE 4096
#define ROW_SIZE 8
#define MAX PAGE_SIZE*4

void* sf_malloc(size_t size) {

	if(size == 0){
		printf("Cannot malloc 0\n");
		errno = ENOMEM;
		return NULL;
	}else if(size > MAX){
		printf("Cannot malloc more than 4GB\n");
		errno = ENOMEM;
		return NULL;
	}

	if(freelist_head == NULL){
		size_t heapsize = 0;  //setheapSize to 0;
		heapsize += PAGE_SIZE;

		heapStart = sf_sbrk(heapsize);
		heapEnd = sf_sbrk(0);

		printf("%x\n", heapStart);
		printf("%x\n", heapEnd);

		freelist_head = (sf_free_header*)((double*) heapStart + 1);
		freelist_head->next = NULL;
		freelist_head->prev = NULL;

		freelist_head->header.alloc = 0;
		freelist_head->header.block_size = (heapsize -16)>> 4;
		// Requested block size field doesnt matter for free blocks

		double* footerPointer = (double*) freelist_head + heapsize/8 - 2;

		sf_footer* foot = (sf_footer*) footerPointer;
		foot->alloc = 0;
		foot->block_size = freelist_head->header.block_size;

		current_free_head = freelist_head;
	}
