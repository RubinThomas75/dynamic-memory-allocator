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

void* sf_malloc(size_t size) {

}

void* sf2{{}};