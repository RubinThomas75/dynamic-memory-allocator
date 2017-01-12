/***
 **** Author: Rubin Thomas
*/

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <string.h>
#include "sfmm.h"
 

sf_free_header* freelist_head = NULL;
sf_free_header* current_free_head = NULL;

void* heapStart = NULL;
void* heapEnd = NULL;

#define PAGE_SIZE 4096
#define ROW_SIZE 8
#define MAX PAGE_SIZE*4


void* sf_malloc(size_t size) {

	if(!validateSize(size))
		return NULL;

	//If this is the first call to malloc, essentially make the whole page free a huge free block.
	//May need to bug check this for when your first call > one page.
	if(freelist_head == NULL)
		current_free_head = createFreeListHead(size);
	

	sf_free_header* listPointer = findNextFit(current_free_head,size);

	//Allocate to that block
	void* nextPointer = allocateBlock(listPointer,size);

    return nextPointer;

}

void* sf_realloc(void *ptr, size_t size) {

	if(size == 0){
		fprintf(stderr,"Cannot realloc to size 0. Call sf_free instead.\n");
		return NULL;
	}

	if(ptr == NULL){
		fprintf(stderr,"Cannot realloc null\n");
		return NULL;
	}

	if((size_t) ptr % 16 == 0){
		fprintf(stderr,"Cannot realloc, pointer is not aligned\n");
		return NULL;
	}

	if((size_t) ptr > (size_t) heapEnd || (size_t) ptr < (size_t) heapStart){
		fprintf(stderr,"Address not in heap range\n");
		return NULL;
	}

    sf_header* headPtr = (sf_header*) ((double*) ptr - 1);

    if(headPtr->alloc != 1){
    	fprintf(stderr,"Cannot realloc a freed block\n");
    	return NULL;
    }

	size_t oldSize = (headPtr->block_size << 4);

	if(oldSize == size){

		fprintf(stderr,"Realloc size is the same as current size.\n");
		return (double*) headPtr + 1;

	} else if (oldSize > size){

		if(oldSize - size - 16 < 32){
			//SPLINTER WILL BE CREATED
			sf_free(ptr);
   		 	return sf_malloc(size);
		} 

		//Clear original footer
		sf_footer* origFoot = (sf_footer*) ((double*) headPtr + headPtr->block_size -1);
		origFoot->alloc = 0;
		origFoot->block_size = (oldSize - size - 16) >> 4;		

		//Create new header
		sf_header* newHead = (sf_header*) ((double*) origFoot - (origFoot->block_size << 4)/8 + 1);
		newHead->alloc = 0;
		newHead->block_size = origFoot->block_size;

		//Change references
		sf_free_header* newFree = (sf_free_header*) newHead;




		sf_free_header* temp = freelist_head;
		freelist_head = newFree;
		freelist_head->next = temp;
		freelist_head->prev = NULL;
		temp->prev = freelist_head;


		//Change original header
		headPtr->block_size = quadWord(size) >> 4;

		//Set new footer
		sf_footer* footPtr = (sf_footer*) ((double*)headPtr + (headPtr->block_size));
		footPtr->alloc = 1;
		footPtr->block_size = headPtr->block_size;

		//Coalesce newly created free block if possible
		coalesce(newHead);

		return(double*) headPtr - 1;

	} else {

		sf_free(ptr);
		sf_free_header* nextFit = findNextFit(ptr,size);
		memcpy(ptr,nextFit,(((sf_header*)ptr)->block_size<<4) - 16);
		return nextFit;

	}
}

void* sf_calloc(size_t nmemb, size_t size) {

	if(size == 0){
		fprintf(stderr, "Cannot malloc 0\n");
		return NULL;
	}

	long max = 1;
	max = 4 * (max << 30);
	if(size > max){
		fprintf(stderr, "Cannot malloc more than 4GB\n");
		errno = ENOMEM;
		return NULL;
	}
		
	if(freelist_head == NULL){

		size_t heapsize = 0;
		while(heapsize <= size){
			heapsize += PAGE_SIZE;
		}

		heapStart = sf_sbrk(heapsize);
		heapEnd = sf_sbrk(0);
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

	//Find a free block
	sf_free_header* listPointer = findNextFit(current_free_head,size);

	//Allocate to that block
	void* nextPointer = allocateBlock(listPointer,size);

    return nextPointer;

}

void sf_free(void *ptr) {

	if(!validatePTR(ptr))
		return;

	//Block address -8 to get header ptr
	sf_header* headPtr = (sf_header*)((double*) ptr - 1);

	if(!isFree(headPtr)){
		headPtr->alloc = 0;
		sf_footer* foot = (sf_footer*) ((double*)headPtr + (headPtr->block_size <<4)/8 - 1);
		foot->alloc = 0;
	} else{ 
		fprintf(stderr,"Memory block is already free\n");
		return;
	}

	//Coalesce blocks if possible
	sf_free_header* newHead = coalesce(headPtr);

	//Change references, think about the cases.
	int result = freeFixFreeHead(newHead);

	return;
}


//*************************//*************************//*******************//***
//******************************************************************************
//*******************************HELPER FUNCTIONS ******************************
//******************************************************************************
//************************//**************************//********************//**


bool validateSize(size_t size){

	if(size == 0){
		printf("Cannot malloc 0\n");
		errno = ENOMEM;
		return false;
	}else if(size > MAX){
		printf("Cannot malloc more than 4GB\n");
		errno = ENOMEM;
		return false;
	}

	 return true;

}

bool validatePTR(void* ptr){

	if(ptr == NULL){
		fprintf(stderr,"Cannot call sf_free with NULL\n");
		return false;
	} else if((size_t) ptr > (size_t) heapEnd || (size_t) ptr < (size_t) heapStart){
		fprintf(stderr,"Address not in heap range\n");
		return false;
	} else if((size_t) ptr % 16 != 0){
		fprintf(stderr, "Free address not aligned\n");
		return false;
	}

	return true;
}

bool isFree(sf_header* ptr){
   return ptr->alloc == 0;
}

size_t quadWord(size_t size){
	
	if(size % 16 == 0)
    	return size;
    else
    	return size + (16 - size % 16);
}

void* createFreeListHead(size_t size){
		size_t heapsize = 0;  //setheapSize to 0;
		heapStart = sf_sbrk(0);

		while(heapsize < size+16){
			 sf_sbrk(1);
			heapsize += PAGE_SIZE;
		}

		heapEnd = sf_sbrk(0);

		freelist_head = (sf_free_header*)((double*) heapStart); 

		//Currently theres no freelist so next is null.
		freelist_head->next = NULL;
		freelist_head->prev = NULL;

		freelist_head->header.alloc = 0;
		//doesnt relal
		freelist_head->header.block_size = (heapsize) >> 4 ;

		double* footerPointer = (double*)freelist_head + heapsize/8 - 1;
		sf_footer* foot = (sf_footer*) footerPointer;
		foot->alloc = 0;
		foot->block_size = freelist_head->header.block_size;

		return freelist_head;
}

void* findNextFit(sf_free_header* ptr, size_t size){

	sf_free_header* freehead = freelist_head;

	//If the head fits, give the head.
	if(freelist_head->next == NULL && ((freelist_head->header.block_size) << 4) >= size){
		return freehead;
	}


	while(freehead->next != NULL){
		freehead = freehead->next;
		if((freehead->header.block_size << 4) >= (size+16)) 
			return freehead;	//Bug check this
	}

	if(freehead->next == NULL){
		sf_free_header* temp = freelist_head;

		void* newStart = sf_sbrk(0);
		sf_free_header* newBlockFreeHeader;
		int blockSize = 0;

		while(blockSize < (size+16)){
			heapEnd = sf_sbrk(1);
			sf_free_header* newBlock = (sf_free_header*)newStart;

			newBlock->header.block_size = PAGE_SIZE;

		    newBlockFreeHeader = coalesce(newBlock);              //See if it can be merged with the previous block
			blockSize += newBlockFreeHeader->header.block_size;
		}

		freehead = newBlockFreeHeader;



	}

	return freehead;
}

void* allocateBlock(sf_free_header* ptr, size_t size){

	//Change the Freelist head before you allocate the block
	int result = allocFixFreeHead(ptr,size);

	// Now allocate the block
	double* pointer = (double*) ptr;

	//allocate header
	sf_header* blockPointer = (sf_header*) ptr;
	blockPointer->alloc = 1;
	blockPointer->block_size = (16 + quadWord(size)) >> 4;

	if(result > 0) {
		blockPointer->block_size = (((blockPointer->block_size) << 4) + result) >> 4;
	}

	pointer += ((blockPointer->block_size << 4) / 8) - 1;

	//allocate footer
	sf_footer* footPointer = (sf_footer*) pointer;
	footPointer->alloc = 1;
	footPointer->block_size = blockPointer->block_size;

	double* returnVal = ((double*) ptr) + 1;

	return returnVal;

}

void* callocateBlock(sf_free_header* ptr, size_t size){

	//Change the Freelist head before you allocate the block
	sf_free_header* tempNext = freelist_head->next;

	freelist_head = (sf_free_header*) ((char*) ptr + size + 16);
	freelist_head->header.alloc = 0;
	freelist_head->header.block_size = quadWord((ptr->header.block_size<< 4) - size - 16) >> 4;	
	freelist_head->next = tempNext;
	freelist_head->prev = NULL;	

	sf_footer* nextFoot = (sf_footer*) ((char*) freelist_head + (freelist_head->header.block_size<<4) - 8);
	nextFoot->alloc = 0x0;
	nextFoot->block_size = freelist_head->header.block_size;

	// Now allocate the block
	double* pointer = (double*) ptr;

	//allocate header
	sf_header* blockPointer = (sf_header*) ptr;
	blockPointer->alloc = 1;
	blockPointer->block_size = quadWord(16 + size) >> 4;

	//Initialize the payload to 0
	double* tempPointer = pointer;
	pointer += ((blockPointer->block_size << 4) / 8) - 1;

	while(tempPointer < pointer){
		*tempPointer = 0;
		tempPointer++;
	}	

	pointer += ((blockPointer->block_size << 4) / 8) - 1;

	//allocate footer
	sf_footer* footPointer = (sf_footer*) pointer;
	footPointer->alloc = 1;
	footPointer->block_size = blockPointer->block_size;

	return (double*) ptr + 1;

}

int allocFixFreeHead(sf_free_header* ptr,size_t size){

	// Before you allocate a block, you may have to change
	// the free list head. This is done differently depending
	// on the the head should be moving.
	sf_free_header* tempNext = freelist_head->next;

	int result; //The extra padding amount necessary to prevent fragmentation

	//Normal case: Freelist head was allocated to and you move it block_size over
	if(ptr == freelist_head && ((freelist_head->header.block_size<<4) - quadWord(size) - 32 >= 32)){

		freelist_head = (sf_free_header*) ((char*) ptr + quadWord(size) + 16);

		freelist_head->header.alloc = 0;
		freelist_head->header.block_size = quadWord((ptr->header.block_size<< 4) - quadWord(size) - 16) >> 4;	

		freelist_head->next = tempNext;
		freelist_head->prev = NULL;	

		sf_footer* nextFoot = (sf_footer*) ((char*) freelist_head + (freelist_head->header.block_size<<4) - 8);
		nextFoot->alloc = 0;
		nextFoot->block_size = freelist_head->header.block_size;

		result = 0;

	} else if (ptr == freelist_head && ((freelist_head->header.block_size << 4)- quadWord(size) - 32 < 32)){
		//Case 2: Freelist head was allocated to, but next block is too small so we split
		result = (freelist_head->header.block_size << 4) - quadWord(size) - 32;

		sf_free_header* newNext = (freelist_head->next)->next;
		freelist_head = freelist_head->next;

		//Same case whether LIFO or ADDRESS	
		freelist_head->next = newNext;
		freelist_head->prev = NULL;	

	} else if (ptr != freelist_head  && ((ptr->header.block_size<<4) - quadWord(size) - 32 >= 32)){
		//Case 3: Freelist head was not allocated to, we change the next and prev pointers

		sf_free_header* newFreeBlock = (sf_free_header*) ((char*) ptr + quadWord(size) + 16);
		newFreeBlock->header.alloc = 0;
		newFreeBlock->header.block_size = quadWord((ptr->header.block_size << 4) - size - 16) >> 4;


		sf_free_header* temp = freelist_head;
		freelist_head = newFreeBlock;
		freelist_head->next = temp;
		freelist_head->prev = NULL;
		temp->prev = freelist_head;

		result = 0;

	}else if(ptr != freelist_head  && ((ptr->header.block_size << 4) - quadWord(size) - 32 < 32)){
		//Case 4: Case 2, but it wasn't the freelist head
		result = ptr->header.block_size - quadWord(size) - 32;

		sf_free_header* newNext = (ptr->next)->next;
		sf_free_header* newFreeBlock = ptr->next;


		sf_free_header* temp = freelist_head;
		freelist_head = newFreeBlock;
		freelist_head->next = temp;
		freelist_head->prev = NULL;
		temp->prev = freelist_head;


	} else {
		result = 5;

		printf("Case Unknown: Aborting\n");
		exit(0);
	}

	current_free_head = ptr;

	return result;

}

size_t locateNextAlloc(sf_free_header* ptr){

	sf_free_header* temp = ptr;
	size_t count = 0;

	while(temp->header.alloc != 1){
		temp = (sf_free_header*)((double*) temp + 1);
		count++;
	}

	return count;

}

sf_free_header* coalesce(sf_header* headPtr){

	//Pointer to new head of block after coalescing
	sf_free_header* toReturn = (sf_free_header*)headPtr;

	double* currBlockHead = (double*) headPtr; // POINTS TO CURRENT BLOCKHEADER
	double* currBlockFoot = (double*) headPtr + (headPtr->block_size << 4)/8 - 1; // POINTS TO CURRENT BLOCK FOOTER
	double* nextBlockHead = (double*) headPtr + (headPtr->block_size << 4)/8; // POINTS TO NEXT BLOCK HEADER
	double* prevBlockFoot; 
	
	if((void*)headPtr != heapStart)
	    prevBlockFoot = ((double*) headPtr) -1;//POINTS TO PREVIOUS FOOTER
	else
		prevBlockFoot = NULL;

	//If we are not already at the heap start, and the previous block is also free,
	//We can coalesce here
	if((prevBlockFoot != NULL) && (((sf_footer*) prevBlockFoot)->alloc == 0)){
		//Get block sizes
		size_t blockSize1 = ((sf_footer*) prevBlockFoot)->block_size << 4;
		size_t blockSize2 = ((sf_header*) currBlockHead)->block_size << 4;
		size_t newSize = (blockSize2 + blockSize1) >> 4;

		//Clear the header and footer of current block, and footer of previous block
		((sf_footer*) prevBlockFoot)->alloc = 0;
		((sf_footer*) prevBlockFoot)->block_size = 0;
		((sf_header*) currBlockHead)->alloc = 0;
		((sf_header*) currBlockHead)->block_size = 0;
		((sf_footer*) currBlockFoot)->block_size = 0;
		((sf_footer*) currBlockFoot)->alloc = 0;

		//Set new header for coalesced block
		prevBlockFoot = prevBlockFoot - blockSize1/8;
		sf_header* newHeader = (sf_header*) prevBlockFoot;
		newHeader->alloc = 0;
		newHeader->block_size= newSize;

		//set return pointer
		toReturn = (sf_free_header*) newHeader;

		//Set new Footer for coalesced block
		currBlockHead = currBlockHead + (blockSize2/8 - 1);
		sf_footer* newFooter = (sf_footer*) currBlockHead;
		newFooter->alloc = 0;
		newFooter->block_size = newSize;

		//If the previous block was the freelist head, we good, keep going
		//If it wasnt...
		if((sf_free_header*) newHeader != freelist_head){

			sf_free_header* temp = freelist_head;
			freelist_head = ((sf_free_header*) newHeader);

			freelist_head->next = temp;
			freelist_head->prev = NULL;

			temp->prev = freelist_head;

		}
	//If we arent at the end of Memory and if the next block isnt allocated already, try and coalesce with that block.
	//Need to fix this so both coalescing is possible.
	} else if ((((double*)nextBlockHead)!= heapEnd) && (((sf_header*) nextBlockHead)->alloc == 0)) {

		//Get Block sizes
		size_t blockSize1 = ((sf_footer*) currBlockFoot)->block_size << 4;
		size_t blockSize2 = ((sf_header*) nextBlockHead)->block_size << 4;
		size_t newSize = (blockSize2 + blockSize1) >> 4;
		//Store references
		sf_free_header* temp = ((sf_free_header*) nextBlockHead)->next; 

		//Clear the header and footer
		((sf_footer*) currBlockFoot)->alloc = 0;
		((sf_footer*) currBlockFoot)->block_size = 0;
		((sf_header*) nextBlockHead)->alloc = 0;
		((sf_header*) nextBlockHead)->block_size = 0;
		//Set new header
		currBlockFoot -= (blockSize1/8 - 1);
		sf_header* newHeader = (sf_header*) currBlockFoot;
		newHeader->alloc = 0;
		newHeader->block_size= newSize;

		toReturn = (sf_free_header*) newHeader;

		//Set new Footer
		sf_footer* newFooter = (sf_footer*) (nextBlockHead + blockSize2/8 - 1);
		newFooter->alloc = 0;
		newFooter->block_size = newSize;

		//Set references
		if((sf_free_header*) newHeader != freelist_head){

			if((sf_free_header*)nextBlockHead == freelist_head){
				void* temp2 = freelist_head->next;
				freelist_head = newHeader;
				freelist_head->next = temp2;
			} else {
				temp = freelist_head;
				freelist_head = ((sf_free_header*) newHeader);
				freelist_head->next = temp;
				freelist_head->prev = NULL;
				temp->prev = freelist_head;
			}
		}


	}
	return toReturn;
}

int freeFixFreeHead(sf_free_header* ptr){

	sf_free_header* next = ptr->next;

	if(ptr != freelist_head){
		sf_free_header* temp = freelist_head;
		freelist_head = ptr;
		freelist_head->next = temp;
		freelist_head->prev = NULL;
		temp->prev = freelist_head;
	}


}