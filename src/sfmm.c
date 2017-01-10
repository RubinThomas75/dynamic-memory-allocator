#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include "sfmm.h"

#define PAGE_SIZE 4096
#define ROW_SIZE 8
#define MAX PAGE_SIZE*4

/**
 * All functions you make for the assignment must be implemented in this file.
 * Do not submit your assignment with a main function in this file.
 * If you submit with a main function in this file, you will get a zero.
 */

sf_free_header* freelist_head = NULL;
sf_free_header* currHead = NULL;

void* heapStart = NULL;
void* heapEnd = NULL;


void* sf_malloc(size_t size) {

	if(size == 0){
		printf("Cannot malloc 0\n");
		errno = ENOMEM;
		return NULL;
	} 
	if(size > MAX){
		printf("Cannot malloc more than 4GB\n");
		errno = ENOMEM;
		return NULL;
	}
	
	if(freelist_head == NULL){ //currently no freelist head

		size_t currentHeapSize = 0;
		heapStart = sf_sbrk(1);
		heapEnd = (char*)heapStart + PAGE_SIZE;
		currentHeapSize = PAGE_SIZE;

		while(currentHeapSize <= size){
			currentHeapSize += PAGE_SIZE;
			heapEnd = (double*)heapEnd + PAGE_SIZE;
			sf_sbrk(1); //Extends heap by one page size
		}


		freelist_head = (sf_free_header*)((double*) heapStart); //At start of current heappointer
		freelist_head->header.alloc = 0; //free
		freelist_head->header.block_size = (currentHeapSize - 16)>> 4; //CHECK (BLOCKSIZE>?)
		freelist_head->next = NULL;
		freelist_head->prev = NULL;

		double* footerPointer = (double*)freelist_head + currentHeapSize/8 - 2;//CHECK

		sf_footer* foot = (sf_footer*) footerPointer;
		foot->alloc = 0;
		foot->block_size = freelist_head->header.block_size;
	}

	currHead = freelist_head;


	//Create padding size
	size_t paddingSize = 0;
		for(int i = 0; i < 16; i++)
			if((size + paddingSize) % 16 == 0)
				paddingSize = i;

	//FIND A FREE BLOCK
	sf_free_header* lp = searchFreeBlock(currHead,size);
	int padReq = freeListHeadFixMALLOC(lp,size);

	//GET THAT BLOCK AND ALLOCATE IT:
	double* pointer = (double*) lp;

	//Set header
	sf_header* blockPointer = (sf_header*) lp;
	blockPointer->alloc = 1; //allocated
	blockPointer->block_size = (pad(16 + size)) >> 4; //size as a quadword
	blockPointer->padding_size = (pad(16 + size) - size);
	if(padReq > 0) {
		blockPointer->block_size = (((blockPointer->block_size) << 4) + padReq) >> 4;
		blockPointer->padding_size += padReq;
	}

	pointer += ((blockPointer->block_size << 4) / 8) - 1;
	//moves pointer to the end of the block

	//set footer
	sf_footer* footPointer = (sf_footer*) pointer;
	footPointer->alloc = 1;
	footPointer->block_size = blockPointer->block_size;

	return (((double*) lp) + 1);

}

void* searchFreeBlock(sf_free_header* ptr, size_t size){

	sf_free_header* tempFreeHead = freelist_head;

	if(freelist_head->next == NULL || (freelist_head->header.block_size << 4)> size){ //if there is no free list yet
		return tempFreeHead; //give free list head
	}

	tempFreeHead = ptr;

	while(tempFreeHead->next != NULL){
		tempFreeHead = tempFreeHead->next; //go through explicit free list
		if((tempFreeHead->header.block_size << 4)> size) break;	//if found with correct size
			return tempFreeHead; 

	}

	if(tempFreeHead->next == NULL){
		sf_free_header* temp = freelist_head;

		//NEW PAGE
		tempFreeHead = sf_sbrk(1);
		tempFreeHead = (sf_free_header*)((double *)tempFreeHead + 1 );
		tempFreeHead->header.alloc = 0;
		tempFreeHead->header.block_size = (pad(size) + 16) >> 4;

		//NEW HEAD, old head = next of new head
		freelist_head = tempFreeHead;
		tempFreeHead->next = temp;
		tempFreeHead->prev = NULL;
		temp->prev = freelist_head;

		sf_footer* newFoot = (sf_footer*) ((double*)tempFreeHead + (tempFreeHead->header.block_size<<4)/8 -1);
		newFoot->alloc = 0; 
		newFoot->block_size = tempFreeHead->header.block_size;

		tempFreeHead = freelist_head;
	}

	return tempFreeHead;
}

//PREVENT FRAGMENTATION method
int freeListHeadFixMALLOC(sf_free_header* ptr, size_t size){ // CHECK
	
	sf_free_header* holdNext = freelist_head->next;
	int padReq; 

	//Put padding necessary in padreq and return it

//CASE 1: YOU CAN ALLOCATE TO FREE HEAD AND THERE IS SPACE FOR ANOTHER FREE HEAD
// WITHIN THE OLD ONE, REFERENCES NEED TO BE CHANGED, NO PADDING NEEDED
	if(ptr == freelist_head && ((freelist_head->header.block_size<<4) - pad(size) - 32 >= 32)){
		padReq = 0;
		//Move pointer to next Block
		freelist_head = (sf_free_header*) ((char*) ptr + pad(size) + 16);
		//make a header and a footer for this new block
		freelist_head->header.alloc = 0;
		freelist_head->header.block_size = pad((ptr->header.block_size<< 4) - size - 16) >> 4;	
		freelist_head->prev = NULL;	
		freelist_head->next = holdNext;
		sf_footer* nextFoot = (sf_footer*) ((char*) freelist_head + (freelist_head->header.block_size<<4) - 8);
		nextFoot->alloc = 0;
		nextFoot->block_size = freelist_head->header.block_size;

		//CASE2: YOU CAN ALLOCATE TO FREEHEAD, BUT NO SPACE FOR ANOTHER FREE HEAD.
		//PADDING IS NECESSARY for frag
	} else if (ptr == freelist_head && ((freelist_head->header.block_size << 4)- pad(size) - 32 < 32)){
		padReq = (freelist_head->header.block_size << 4) - pad(size) - 32;

		sf_free_header* newNext = (freelist_head->next)->next;
		freelist_head = freelist_head->next;

		freelist_head->next = newNext;
		freelist_head->prev = NULL;	

		//CASE3: ALLOCATED TO A DIFFERENT BLOCK THAN FREE HEAD
		//BUT REFERENCES NEED TO BE CHANGED
	} else if (ptr != freelist_head  && ((ptr->header.block_size<<4) - pad(size) - 32 >= 32)){
		padReq = 0;

		sf_free_header* newFreeBlock = (sf_free_header*) ((char*) ptr + pad(size) + 16);
		newFreeBlock->header.alloc = 0;
		newFreeBlock->header.block_size = pad((ptr->header.block_size << 4) - size - 16) >> 4;
		sf_free_header* temp = freelist_head;
		freelist_head = newFreeBlock;
		freelist_head->next = temp;
		freelist_head->prev = NULL;
		temp->prev = freelist_head;

		//CASE4: YOU ALLOCATE TO A FREE BLOCK BUT THERE IS NO SPACE TO MAKE ANOTHER BLOCK THERE 
		// NEED TO AVOID FRAGMENTATION
	}else if(ptr != freelist_head  && ((ptr->header.block_size << 4) - pad(size) - 32 < 32)){
		padReq = ptr->header.block_size - pad(size) - 32;

		sf_free_header* newFreeBlock = ptr->next;
		sf_free_header* temp = freelist_head;
		freelist_head = newFreeBlock;
		freelist_head->next = temp;
		freelist_head->prev = NULL;
		temp->prev = freelist_head;
	} 

	currHead = ptr;
	return padReq;
}


size_t pad(size_t size){  // Can I make a macro of this ? Type indep? 
	if(size % 16 == 0)
    	return size;
    else
    	return (size + (16 - size % 16));
}


void sf_free(void *ptr){

	//ERROR CHECKING
	if(ptr == NULL){
		printf("Cannot free null pointer\n");
		//errno values for errors?
		return;
	}else if((size_t) ptr % 16 != 0){ //If pointer not aligned with blocks
		printf("Free pointer adress not aligned with memblock\n");
		return;
	}
	//Out of bounds check? FOR HEAP //CHECK! 

	if((double*)ptr < (double*)heapStart || (double*)ptr > (double*)heapEnd)
	{
		printf("%s\n", "Out of bounds" );
		exit(0);
	}
	

	sf_header* newHead = (sf_header*)((double*) ptr - 1);

	//Change header and footer to free (if !free)
	if(newHead->alloc == 1){
		newHead->alloc = 0;
		sf_footer* foot = (sf_footer*) ((double*)newHead + (newHead->block_size <<4)/8 - 1);
		foot->alloc = 0;
	} else{ 
		printf("Memory block is already free\n");
		return;
	}
	//Coalesce and fix references
     coalesceBlock(newHead); // CHECK for NEED CAST (sf_free_header)
	return;
}

sf_free_header* coalesceBlock(sf_header* ptr){

	//Pointer to new head of block after coalescing
	sf_free_header* toReturn = (sf_free_header*)ptr; //initial return;
	int hasCos = 0;
	int refFixed = 0;

	double* thisBlockPointer1 = (double*)ptr;  //Points at HEAD of this block
	double* prevBlockPointer = NULL;
	if(ptr != heapStart)
		 prevBlockPointer = ((double*)ptr) -1;  //Points at FOOT of previous block

	//Check if you can coalesce with the previous block //Check if its heapstart
	if((prevBlockPointer != NULL) && (((sf_footer*)prevBlockPointer)->alloc == 0)){
		size_t combinedSize = ((((sf_footer*) prevBlockPointer)->block_size << 4) + (((sf_header*) thisBlockPointer1)->block_size << 4)) >> 4;
		size_t saveSize = (((sf_footer*) prevBlockPointer)->block_size << 4);
		((sf_footer*) prevBlockPointer)->block_size = 0;
		((sf_header*) thisBlockPointer1)->block_size = 0;
		prevBlockPointer -= saveSize/8 - 1;
		sf_header* combinedHeader = (sf_header*) prevBlockPointer; //set the header to both
		combinedHeader->alloc = 0;
		combinedHeader->block_size= combinedSize; //Saves Next and Prev

		toReturn = (sf_free_header*) combinedHeader; 
		hasCos = 1;


		sf_footer* combinedFooter = (sf_footer*)(combinedHeader + (combinedSize << 4)/8 - 1);
		combinedFooter->alloc = 0;
		combinedFooter->block_size = combinedSize;
		//Now current Block is combined;
		//thisBlockPointer1 = toReturn;
		//CASE 1: BLOCK COALESCE WITH THE PREVIOUS BLOCK. GRAB
		// PREVIOUS PREV AND GO TO PREVIOUS NEXT
		//THEN MAKE CURR BLOCK HEADER WITH NULL PREV
		//AND NEXT AS CURR HEADER.
		if((sf_free_header*) combinedHeader != freelist_head){
			sf_free_header* tempNext = ((sf_free_header*) combinedHeader)->next;
			sf_free_header* tempPrev = ((sf_free_header*) combinedHeader)->prev;
			tempPrev->next = tempNext;
			tempNext->prev = tempPrev;

			sf_free_header* temp = freelist_head;

			freelist_head = ((sf_free_header*) combinedHeader);
			freelist_head->next = temp;
			freelist_head->prev = NULL;
			refFixed = 1;
			temp->prev = freelist_head;
		}

	} 

	if(hasCos == 0){
	double* thisBlockPointer2 = (double*)ptr + (ptr->block_size << 4)/8 - 1; //Points to FOOT of current block (DOESNT CHANGE IF COALESCE WITH PREF BLOCK)
	double* nextBlockPointer = (double*)ptr + (ptr->block_size << 4)/8; // Points to HEAD of Next Block
	if ((((double*)nextBlockPointer + 1)!= heapEnd) && (((sf_header*) nextBlockPointer)->alloc == 0)) {

		size_t combinedSize = ((((sf_footer*) nextBlockPointer)->block_size << 4) + (((sf_header*) thisBlockPointer2)->block_size << 4)) >> 4;
		//save reference to the next blocks next and prev
		sf_free_header* tempNext = ((sf_free_header*) nextBlockPointer)->next; 
		sf_free_header* tempPrev = ((sf_free_header*) nextBlockPointer) ->prev;

		((sf_footer*) thisBlockPointer2)->block_size = 0;
		((sf_header*) nextBlockPointer)->block_size = 0;
		thisBlockPointer2 -= ((((sf_header*) thisBlockPointer2)->block_size << 4)/8 - 1);
		sf_header* combinedHeader = (sf_header*) thisBlockPointer2;
		combinedHeader->alloc = 0;
		combinedHeader->block_size= combinedSize;

		if(hasCos == 0)
			toReturn = (sf_free_header*) combinedHeader;
		//else
			//return value is described in previous coalesce case

		nextBlockPointer += ((((sf_footer*) nextBlockPointer)->block_size << 4)/8 - 1);
		sf_footer* combinedFooter = (sf_footer*) nextBlockPointer;
		combinedFooter->alloc = 0;
		combinedFooter->block_size = combinedSize;

		//CASE 2: COALESCE WITH THE NEXT BLOCK
		//GRAB NEXT BLOCKS PREV MAKE IT GO TO NEXT NEXT
		//THEN SET CURR AS FREE LIST HEADER 
		if((sf_free_header*) combinedHeader != freelist_head){
			tempNext->prev = tempPrev;
			tempPrev->next = tempNext;

			sf_free_header* temp = freelist_head;
			freelist_head = ((sf_free_header*) combinedHeader);
			freelist_head->next = temp;
			freelist_head->prev = NULL;
			refFixed = 1;
			temp->prev = freelist_head;
		}

	}
	}
	//CASE 3 NO COALESCE,
	//MAKE THIS BLOCK THE HEAD;

	if(refFixed == 0){
		if(toReturn != freelist_head){
			sf_free_header* temp = freelist_head;
			freelist_head = toReturn;
			freelist_head->next = temp;
			freelist_head->prev = NULL;
			temp->prev = freelist_head;
		}		
	}

	return toReturn;
}


void *sf_realloc(void *ptr, size_t size){

	//VALIDATE ARGS POINTER
	if(ptr == NULL){
		printf("Poiter is null\n");
		return NULL;
	}else if((size_t) ptr % 16 == 0){
		printf("Pointer is not aligned\n");
		return NULL;
	}
		//BOUNDS CHECK??? 
	if((double*)ptr < (double*)heapStart || (double*)ptr > (double*)heapEnd)
	{
		printf("%s\n", "Out of bounds" );
		return NULL;
	}

    sf_header* oldBlockHeadPtr = (sf_header*) ((double*) ptr - 1);
    if(oldBlockHeadPtr->alloc != 1){
    	printf("This block is free\n");
    	return NULL;
    }

    //VALIDATE ARGS SIZE
    if(size == 0){
		printf("Cannot realloc to size 0. Use sf_free(void* ptr).\n");
		return NULL;
	}

	size_t oldSize = (oldBlockHeadPtr->block_size << 4);
	if(oldSize == size){
		return (double*) oldBlockHeadPtr + 1; // Just return pointer to block
	} else if (oldSize > size){
		//MAKING BLOCK SMALLER CASE

		//CASE 1: difference is < one block size;
		if(oldSize - size - 16 < 32){
			sf_free(ptr);
   		 	return sf_malloc(size);
   		 	//free it and malloc a new one... creates SPLINTER!!! 
   		 	//needs to be dealt with in coalesce case
		} 
		//CASE 2: difference > one block size

		//Go to the foot, going to cut this block.
		//this foot is now the foot of a free block
		sf_footer* oldBlockFootPtr = (sf_footer*) ((double*) oldBlockHeadPtr + oldBlockHeadPtr->block_size -1);
		oldBlockFootPtr->alloc = 0; //free
		oldBlockFootPtr->block_size = (oldSize-size-16) >> 4;		

		//Make header for new free block
		sf_header* newFreeBlockHead = (sf_header*) ((double*) oldBlockFootPtr - (oldBlockFootPtr->block_size << 4)/8 + 1);
		newFreeBlockHead->alloc = 0;
		newFreeBlockHead->block_size = oldBlockFootPtr->block_size;
		sf_free_header* nF = (sf_free_header*) newFreeBlockHead;
		//make it the head of the freelist
		sf_free_header* temp = freelist_head;
		freelist_head = nF;
		freelist_head->next = temp;
		freelist_head->prev = NULL;
		temp->prev = freelist_head;
		coalesceBlock((sf_header*)nF);

		//new header and footer
		oldBlockHeadPtr->block_size = pad(size) >> 4;
		oldBlockHeadPtr->padding_size = (pad(size) - size) >> 4;
		sf_footer* newfootPtr = (sf_footer*) ((double*)oldBlockHeadPtr + (oldBlockHeadPtr->block_size) - 1);
		newfootPtr->alloc = 1;
		newfootPtr->block_size = oldBlockHeadPtr->block_size;

		

		return(double*) oldBlockHeadPtr - 1;

	} else {
		//SIZE > OLDSIZE;
		sf_free(ptr);

		sf_free_header* find = searchFreeBlock(ptr,size);
		memcpy(ptr,find,(((sf_header*)ptr)->block_size<<4) - 16);
		return find;

	}
}



int sf_info(info* meminfo){
  return -1;
}

