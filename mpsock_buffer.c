#include "mpsock_buffer.h"

/*
 * debug function
 */
void print_ringbuffer(mpsock_buffer *buffer)
{
	size_t end = buffer->buffer_end - buffer->buffer_start;
	size_t start = 0;
	size_t head = buffer->head - buffer->buffer_start;
	size_t tail = buffer->tail - buffer->buffer_start;
	LOG_DEBUG("%sbuffer_start=%zu, buffer_end=%zu, head=%zu, tail=%zu",FUN_EVENT,start,end,head,tail);
}

/*
 * verify if there is enough space to store size in the buffer
 */
int has_enough_memory(mpsock_buffer *buffer, size_t size)
{
	if(buffer->tail > buffer->head)
	{
		return ((buffer->buffer_end - buffer->tail) + (buffer->head - buffer->buffer_start) > size);
	}
	else if(buffer->tail < buffer->head)
	{
		return (buffer->head - buffer->tail > size);
	}
	else
	{
		return (buffer->buffer_end - buffer->buffer_start > size);
	}
}

/*
 * verify that we have enough memory allocated
 */
int has_enough_allocated(mpsock_buffer *buffer, size_t size)
{
	if(buffer->tail > buffer->head)
	{
		return ((buffer->tail - buffer->head) >= size);
	}
	else if(buffer->tail < buffer->head)
	{
		return (((buffer->buffer_end - buffer->head) + (buffer->tail - buffer->buffer_start)) >= size);
	}
	else
	{
		return (size == 0);
	}
}

/*
 * returns a pointer to memory in buffer with given size
 * return NULL if not enough space is available
 */
void *allocate_buffer_memory(mpsock_buffer *buffer, size_t size)
{
	LOG_DEBUG("%sallocate_buffer_memory(%zu)",FUN_EVENT,size);

	if(USE_ASSERTS) assert(size > 0);

	if(USE_ASSERTS) assert(buffer->head <= buffer->buffer_end && buffer->head >= buffer->buffer_start);
	if(USE_ASSERTS) assert(buffer->tail <= buffer->buffer_end && buffer->tail >= buffer->buffer_start);

	if(USE_ASSERTS) assert(has_enough_memory(buffer,size));
	//if(!has_enough_memory(buffer,size))
	//{
	//	// we do not have enough free space for this operation
	//	return NULL;
	//}

	// return value
	void *ret = buffer->tail;
	
	// advance tail by size
	if(buffer->tail+size <= buffer->buffer_end)
	{
		if(buffer->tail < buffer->head)
		{
			// no wrap around -> simply add
			buffer->tail += size;
			if(USE_ASSERTS) assert(buffer->tail < buffer->head);
		}
		else
		{
			// no wrap around -> simply add
			buffer->tail += size;
			if(USE_ASSERTS) assert(buffer->tail > buffer->head);
		}
	}
	else
	{
		// we wrap around the end
		buffer->tail = buffer->buffer_start + (size - (buffer->buffer_end - buffer->tail));
		if(USE_ASSERTS) assert(buffer->tail < buffer->head);
	}

	print_ringbuffer(buffer);

	if(USE_ASSERTS) assert(buffer->tail != buffer->head);
	if(USE_ASSERTS) assert(buffer->tail <= buffer->buffer_end && buffer->tail >= buffer->buffer_start);
	if(USE_ASSERTS) assert(buffer->head <= buffer->buffer_end && buffer->head >= buffer->buffer_start);

	return ret;
}

/*
 * free given size from head in given buffer
 */
void free_buffer_memory(mpsock_buffer *buffer, size_t size)
{
	LOG_DEBUG("%sfree_buffer_memory(%zu)",FUN_EVENT,size);

	if(USE_ASSERTS) assert(buffer->head <= buffer->buffer_end && buffer->head >= buffer->buffer_start);
	if(USE_ASSERTS) assert(buffer->tail <= buffer->buffer_end && buffer->tail >= buffer->buffer_start);

	if(USE_ASSERTS) assert(has_enough_allocated(buffer,size));

	if(buffer->buffer_end - buffer->head >= size)
	{
		if(buffer->tail < buffer->head)
		{
			// no wrap around -> simply add
			buffer->head += size;
			if(USE_ASSERTS) assert(buffer->head >= buffer->tail);
		}
		else
		{
			// no wrap around -> simply add
			buffer->head += size;
			if(USE_ASSERTS) assert(buffer->head <= buffer->tail);
		}
	}
	else
	{
		// we wrap around the end
		buffer->head = buffer->buffer_start + (size - (buffer->buffer_end - buffer->head));
		if(USE_ASSERTS) assert(buffer->head <= buffer->tail);
	}

	print_ringbuffer(buffer);

	if(USE_ASSERTS) assert(buffer->tail <= buffer->buffer_end && buffer->tail >= buffer->buffer_start);
	if(USE_ASSERTS) assert(buffer->head <= buffer->buffer_end && buffer->head >= buffer->buffer_start);
}

mpsock_buffer *create_buffer(size_t size)
{
	mpsock_buffer *buf = (mpsock_buffer*)malloc(sizeof(mpsock_buffer));
	buf->buffer_start = (void*)malloc(size);
	buf->buffer_end = buf->buffer_start + size;
	buf->head = buf->buffer_start;
	buf->tail = buf->head;
	buf->free_chunk_structures = NULL;

	return buf;
}

void store_in_buffer(mpsock_buffer *buffer, void *dst, const void *orig, size_t offset, size_t len)
{
	// determine real destination in buffer
	void *real_dst = dst+offset;
	if(real_dst > buffer->buffer_end)
	{
		// wrap around
		real_dst = buffer->buffer_start + (offset - (buffer->buffer_end - dst));
	}

	if(USE_ASSERTS) assert(real_dst <= buffer->buffer_end && real_dst >= buffer->buffer_start);

	// store data
	if(real_dst + len <= buffer->buffer_end)
	{
		// no wrap
		memcpy(real_dst,orig,len);
	}
	else
	{
		// we wrap
		memcpy(real_dst,orig,buffer->buffer_end - real_dst);
		memcpy(buffer->buffer_start, orig + (buffer->buffer_end - real_dst),len - (buffer->buffer_end - real_dst));
	}
}

void read_from_buffer(mpsock_buffer *buffer, void *dst, void *orig, size_t offset, size_t len)
{
	// determine real origin in buffer
	void *real_orig = orig+offset;
	if(real_orig > buffer->buffer_end)
	{
		// wrap around
		real_orig = buffer->buffer_start + (offset - (buffer->buffer_end - orig));
	}

	if(USE_ASSERTS) assert(real_orig <= buffer->buffer_end && real_orig >= buffer->buffer_start);

	// store data
	if(real_orig + len <= buffer->buffer_end)
	{
		// no wrap
		memcpy(dst,real_orig,len);
	}
	else
	{
		// we wrap
		memcpy(dst,real_orig,buffer->buffer_end - real_orig);
		memcpy(dst+(buffer->buffer_end - real_orig), buffer->buffer_start,len - (buffer->buffer_end - real_orig));
	}
}

mpsock_chunk *allocate_chunk_in_buffer(mpsock_buffer *buffer, size_t start_byte, size_t size)
{
	LOG_DEBUG("%sallocate_chunk",FUN_EVENT);

	mpsock_chunk *chunk;
	if(buffer->free_chunk_structures == NULL)
	{
		LOG_DEBUG("%sno reusable chunk found -> create new chunk",COND_EVENT);

		// create the chunk
		chunk = (mpsock_chunk*)malloc(sizeof(mpsock_chunk));
	}
	else
	{
		LOG_DEBUG("%sreuse old chunk",COND_EVENT);

		// get the chunk from the list
		chunk = buffer->free_chunk_structures;
		buffer->free_chunk_structures = buffer->free_chunk_structures->next;
	}

	// init values
	chunk->next = NULL;
	chunk->start_byte = start_byte;
	chunk->data_size = size;
	chunk->buffer_size = size;
	chunk->pos_save = 0;
	chunk->pos_read = 0;

	// allocate the buffer
	chunk->buffer = allocate_buffer_memory(buffer, size);

	return chunk;
}

void free_chunk_in_buffer(mpsock_buffer *buffer, mpsock_chunk *chunk)
{
	LOG_DEBUG("%sfree_chunk",FUN_EVENT);

	// free buffer memory
	free_buffer_memory(buffer, chunk->buffer_size);

	// reinit
	chunk->next = NULL;

	// append to free structure list
	mpsock_chunk *l_chunk = buffer->free_chunk_structures;
	if(l_chunk == NULL)
	{
		buffer->free_chunk_structures = chunk;
	}
	else
	{
		mpsock_chunk *last_chunk;
		while(l_chunk != NULL)
		{
			last_chunk = l_chunk;
			l_chunk = l_chunk->next;
		}

		last_chunk->next = chunk;
	}
}
