/**
 * Multipath Multi-source HTTP (mHTTP)
 *
 * Developers: Juhoon Kim (kimjuhoon@gmail.com), Karl Fischer
 *
 */

#ifndef __MPSOCK_BUFFER_H__
#define __MPSOCK_BUFFER_H__

#include "libmpsocket.h"
#include "mpsock_pool.h"

/*
 * ringbuffer for chunk memory.
 * This is works, as long as we have sequential requests with wget
 * Later in Browser with parallel requests, we need another structure
 */
typedef struct mpsock_buffer
{
	/* buffer pointers */
	void *buffer_start;
	void *buffer_end;

	/* first element in buffer */
	void *head;

	/* last element in buffer */
	void *tail;

	/* free chunk structure list */
	struct mpsock_chunk *free_chunk_structures;
} mpsock_buffer;

/*
 * method that creates a ringbuffer of given size
 */
mpsock_buffer *create_buffer(size_t size);

/*
 * store given data into given buffer with given offset
 */
void store_in_buffer(mpsock_buffer *buffer, void *dst, const void *orig, size_t offset, size_t len);

/*
 * reads specified data from buffer with offset to given destination
 */
void read_from_buffer(mpsock_buffer *buffer, void *dst, void *orig, size_t offset, size_t len);

/*
 * returns a chunk structure with given size allocated buffer memory
 */
struct mpsock_chunk *allocate_chunk_in_buffer(mpsock_buffer *buffer, size_t start_byte, size_t size);

/*
 * frees the given chunk
 */
void free_chunk_in_buffer(mpsock_buffer *buffer, struct mpsock_chunk *chunk);

#endif
