/*
 * Simple, 32-bit and 64-bit clean allocator based on implicit free
 * lists, first-fit placement, and boundary tag coalescing, as described
 * in the CS:APP3e text. Blocks must be aligned to doubleword (8 byte)
 * boundaries. Minimum block size is 16 bytes.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include "mm.h"
#include "memlib.h"

#define FOR(i, n) for(int i=0; i<n; i++)
#define FOR1(i, n) for(int i=1; i<=n; i++)
#define DIFF(A, B) if ((A) != (B))
#define SAME(A, B) if ((A) == (B))
#define NOTNULL(A) if ((A) != NULL)
#define ISNULL(A) if ((A) == NULL)

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your information in the following struct.
 ********************************************************/
team_t team = {
        /* Your student ID */
        "20191559",
        /* Your full name*/
        "SangWon Kang",
        /* Your email address */
        "me@kevink1113.com",
};

/* $begin mallocmacros */
/* Basic constants and macros */
#define WSIZE       4       /* Word and header/footer size (bytes) */ //line:vm:mm:beginconst
#define DSIZE       8       /* Double word size (bytes) */
#define CHUNKSIZE  (1<<12)  /* Extend heap by this amount (bytes) */  //line:vm:mm:endconst
#define ALIGNMENT 8         /* single word (4) or double word (8) alignment */
#define PLACE_THRESHOLD ALIGNMENT << 3 /* Threshold for place function: modify to find changes */

#define MAX(x, y) ((x) > (y)? (x) : (y))
#define MIN(x, y) ((x) < (y)? (x) : (y)) // Added

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc)) //line:vm:mm:pack

/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)(p))            //line:vm:mm:get
#define PUT(p, val)  (*(unsigned int *)(p) = (val))    //line:vm:mm:put

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)                   //line:vm:mm:getsize
#define GET_ALLOC(p) (GET(p) & 0x1)                    //line:vm:mm:getalloc

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)       ((char *)(bp) - WSIZE)                      //line:vm:mm:hdrp
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) //line:vm:mm:ftrp

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) //line:vm:mm:nextblkp
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) //line:vm:mm:prevblkp

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define NEXT_BLK(ptr) (*(char **)(ptr))
#define PREV_BLK(ptr) (*(char **)(ptr + WSIZE))
#define BLOCK_SIZE(ptr) (GET_SIZE(HDRP(ptr)))
#define SET(p, ptr) (*(uintptr_t *)(p) = (uintptr_t)(ptr))
/* $end mallocmacros */

/* Global variables */
static void *heap_listp = NULL; // Empty list


/* Function prototypes for internal helper routines */
static char *init_heap_space(void);

static void create_heap(char *heap_s);

static void *extend_heap(size_t words);

static char *extend_heap_if_needed(char *bp, size_t asize, int *left);

static char *fit_block(size_t asize);

static void *place(void *bp, size_t asize); // Modified return type

static void *coalesce(void *bp);

static void insertion(size_t size, char *ptr);

static void deletion(char *ptr);


/* packing, putting header */
#define SET_HDR(bp, size, alloc) PUT(HDRP(bp), PACK(size, alloc))

/* packing, putting footer */
#define SET_FTR(bp, size, alloc) PUT(FTRP(bp), PACK(size, alloc))

/* packing, putting header and footer altogether */
#define SET_HDR_FTR(bp, size, alloc)  do {  \
    SET_HDR(bp, size, alloc);               \
    SET_FTR(bp, size, alloc);               \
} while(0)

/* splitting block, handle free list */
#define SPLIT_BLK(bp, size, free) do {      \
    SET_HDR_FTR(bp, size, 1);               \
    SET_HDR_FTR(NEXT_BLKP(bp), free, 0);    \
    insertion(free, NEXT_BLKP(bp));         \
} while(0)

#define NEXT_NOT_ALLOC(ptr) (!GET_ALLOC(HDRP(NEXT_BLKP(ptr))) || !BLOCK_SIZE(NEXT_BLKP(ptr)))

/**
 * @brief Initialize list pointer, heap space
 * @return space pointer or -1 on error
 */
static char *init_heap_space(void) {
    heap_listp = NULL;
    char *heap_s = mem_sbrk(WSIZE << 2);
    SAME(heap_s, (void *) -1) return (void *) -1;
    return heap_s;
}

/**
 * @brief Create heap structure with prologue and epilogue
 * @param heap_s heap space pointer
 */
static void create_heap(char *heap_s) {
    PUT(heap_s, 0);                             // Alignment
    PUT(WSIZE + heap_s, PACK(DSIZE, 1));        // Prologue header
    PUT((WSIZE << 1) + heap_s, PACK(DSIZE, 1)); // Prologue footer
    PUT(3 * WSIZE + heap_s, PACK(0, 1));        // Epilogue header
}

/**
 * @brief Extend the heap with a new free block
 * @param words size of the new free block
 * @return pointer of the new free block
 */
static void *extend_heap(size_t words) {
    void *bp;
    SAME(bp = mem_sbrk(ALIGN(words)), ((void *) -1)) return NULL;

    SET_HDR_FTR(bp, ALIGN(words), 0);           // Set new block header, footer (mark free)
    SET_HDR(NEXT_BLKP(bp), 0, 1);               // Set epilogue header (mark end)
    insertion(ALIGN(words), bp);      // insert

    return coalesce(bp);                        // coalesce and return
}

/**
 * @brief Extend heap if needed, for a given block pointer and size
 * @param bp Block pointer
 * @param asize size of block
 * @param left size left in current block, if applicable
 * @return Pointer to block / NULL
 */
static char *extend_heap_if_needed(char *bp, size_t asize, int *left) {
    ISNULL(bp) bp = extend_heap(MAX(asize, CHUNKSIZE));
    else if (left && *left < 0) {
        int extendsize = MAX(CHUNKSIZE, -(*left));
        ISNULL (extend_heap(extendsize)) return NULL;
        *left += extendsize;
    }
    return bp;
}

int mm_init(void) {
    char *heap_s = init_heap_space();
    DIFF(heap_s, (void *) -1) {
        create_heap(heap_s);
        DIFF(extend_heap(1 << 6), 0) return 0;
    }
    return -1;
}

/**
 * @brief Get an adequate block for the size
 * @param size Size of block
 * @return Pointer to adequate block or NULL
 */
static char *fit_block(size_t asize) {
    char *bp = heap_listp;
    while (bp && BLOCK_SIZE(bp) < asize) bp = NEXT_BLK(bp);

    return bp;
}

/**
 * @brief Allocate block of size bytes
 * @param size Size of block
 * @return Pointer to block or NULL
 */
void *mm_malloc(size_t size) {
    if (size == 0) return NULL;

    size_t asize = MAX(ALIGN(size + DSIZE), DSIZE << 1);
    char *bp = fit_block(asize);
    bp = extend_heap_if_needed(bp, asize, NULL);

    ISNULL(bp) return NULL;

    return place(bp, asize);
}


/**
 * @brief Free memory block pointed by ptr
 * @param bp Pointer of memory block
 */
void mm_free(void *bp) {
    size_t size = BLOCK_SIZE(bp);
    SET_HDR_FTR(bp, size, 0);
    insertion(size, bp);
    coalesce(bp);
}

/**
 * @brief Reallocate memory block pointed by ptr
 * @param ptr Pointer of memory block
 * @param size Size of memory block
 * @return Reallocated block pointer or NULL
 */
void *mm_realloc(void *ptr, size_t size) {
    if (!size) return NULL;

    size_t asize = (size <= DSIZE) ? (DSIZE << 1) + (1 << 7) : ALIGN(size + DSIZE) + (1 << 7);

    if (BLOCK_SIZE(ptr) >= asize) return ptr;

    if (NEXT_NOT_ALLOC(ptr)) {
        int left = BLOCK_SIZE(ptr) + BLOCK_SIZE(NEXT_BLKP(ptr)) - asize;
        ptr = extend_heap_if_needed(ptr, asize, &left);

        ISNULL (ptr) return NULL;  // If extend_heap_if_needed failed

        deletion(NEXT_BLKP(ptr));
        SET_HDR_FTR(ptr, asize + left, 1);
        return ptr;
    }

    size_t old_size = BLOCK_SIZE(ptr);
    size_t copy_size = MIN(old_size, size);

    // Temporarily save the data
    char *temp_data = mm_malloc(copy_size); // can change to malloc
    memcpy(temp_data, ptr, copy_size);

    // Free the old block
    mm_free(ptr);

    // Allocate new block
    void *newptr = mm_malloc(size);
    NOTNULL (newptr) {
        // Copy the data to the new block
        memcpy(newptr, temp_data, copy_size);
    }

    // Free the temp buffer
    mm_free(temp_data);

    return newptr;
}


/**
 * @brief Place block of asize bytes at start of free block bp
 * and split if remainder would be at least minimum block size
 * @param bp pointer of block
 * @param asize size of block
 * @return pointer of placed block
 */
static void *place(void *bp, size_t asize) {
    deletion(bp);

    size_t csize = BLOCK_SIZE(bp);
    size_t free_left = csize - asize;
    size_t min_block_size = DSIZE << 1;

    if (min_block_size >= free_left) {
        asize = csize; // can't be split, allocate entire block
    } else if (asize < PLACE_THRESHOLD) {
        SPLIT_BLK(bp, asize, free_left);
    } else {
        // move to next block
        SET_HDR_FTR(bp, free_left, 0);
        insertion(free_left, bp);
        bp = NEXT_BLKP(bp);
    }

    SET_HDR_FTR(bp, asize, 1);
    return bp;
}


/**
 * @brief Find free blocks, and insert (Explicit list implementation)
 * @param size size of block
 * @param ptr pointer of block
 */
static void insertion(size_t size, char *ptr) {
    char *cur_ptr = heap_listp;
    char *prev_ptr = NULL;

    /* Find place to insert */
    while (cur_ptr && BLOCK_SIZE(cur_ptr) < size) {
        prev_ptr = cur_ptr;
        cur_ptr = NEXT_BLK(cur_ptr);
    }
    /* NOT start of list */
    NOTNULL (prev_ptr) {
        SET(ptr + WSIZE, prev_ptr);
        SET(prev_ptr, ptr);
        NOTNULL (cur_ptr) {
            SET(ptr, cur_ptr);
            SET(cur_ptr + WSIZE, ptr);
        } else SET(ptr, NULL);
    }
        /* start of list */
    else {
        heap_listp = ptr;
        SET(ptr + WSIZE, NULL);
        NOTNULL (cur_ptr) {      // NOT the only element in list
            SET(ptr, cur_ptr);
            SET(cur_ptr + WSIZE, ptr);
        } else SET(ptr, NULL); // only element in list
    }
}


/**
 * @brief Find blocks by case, and delete (Explicit list implementation)
 * @param ptr pointer to delete
 */
static void deletion(char *ptr) {
    NOTNULL (PREV_BLK(ptr)) { // Not first node
        SET(PREV_BLK(ptr), NEXT_BLK(ptr));
        if (NEXT_BLK(ptr)) SET(NEXT_BLK(ptr) + WSIZE, PREV_BLK(ptr)); // Not last node
    } else { // first node
        heap_listp = NEXT_BLK(ptr);
        if (heap_listp) SET(heap_listp + WSIZE, NULL); // only node
    }
}

/**
 * @brief Coalesce free blocks
 * @param bp pointer of block
 * @return pointer of coalesced block
 */
static void *coalesce(void *bp) {
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); // prev block allocated?
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); // next block allocated?
    size_t size = BLOCK_SIZE(bp);                       // size of coalescing block

    if (prev_alloc && next_alloc) {         /* Case 1 */
        return bp;
    } else if (prev_alloc && !next_alloc) { /* Case 2 */
        deletion(bp);
        deletion(NEXT_BLKP(bp));
        size += BLOCK_SIZE(NEXT_BLKP(bp));
        SET_HDR(bp, size, 0);
        SET_FTR(bp, size, 0);
    } else if (!prev_alloc && next_alloc) { /* Case 3 */
        deletion(bp);
        deletion(PREV_BLKP(bp));
        size += BLOCK_SIZE(PREV_BLKP(bp));
        SET_HDR(PREV_BLKP(bp), size, 0);
        SET_FTR(bp, size, 0);
        bp = PREV_BLKP(bp);
    } else {                                /* Case 4 */
        deletion(bp);
        deletion(PREV_BLKP(bp));
        deletion(NEXT_BLKP(bp));
        size += BLOCK_SIZE(PREV_BLKP(bp)) +
                BLOCK_SIZE(NEXT_BLKP(bp));
        SET_HDR(PREV_BLKP(bp), size, 0);
        SET_FTR(NEXT_BLKP(bp), size, 0);
        bp = PREV_BLKP(bp);
    }

    insertion(size, bp);

    return bp;
}