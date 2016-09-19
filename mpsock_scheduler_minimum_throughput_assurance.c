#include "mpsock_scheduler_minimum_throughput_assurance.h"
#include "mpsock_pool.h"
#include "mpsock_connection.h"

// TODO: optimization necessary!!! -> see TODOs inside code
int optimize_final_chunk_request(mpsock_scheduler *scheduler)
{
	if(MINIMUM_THROUGHPUT_ASSURANCE == 0) return FALSE;

	mpsock_pool *pool = scheduler->connection->pool;
	
	/* 1st Requirement: No more Requests */
	if(pool->next_start_byte < pool->current_file_size || HASH_COUNT(pool->partial_chunk_table) > 0)
	{
		// there is still more requests to make
		return FALSE;
	}

	// can happen in case of Transfer-Encoding: chunked
	//if(pool->best_connection == NULL) return FALSE;

	// small optimization: when not more than one connection, we can save computation
	if(HASH_COUNT(pool->connection_table) > 1)
	{
		mpsock_connection *connection = scheduler->connection;

		double bytes_left = (double)(connection->current_chunk->data_size - connection->current_chunk->pos_save);
		double bandwidth = (double)(scheduler->bandwidth); 
		double time_left = bytes_left/bandwidth;

		mpsock_connection *iter_connection;

		for(iter_connection = pool->connection_table; iter_connection != NULL; iter_connection = iter_connection->hh.next)
		{
			//if(iter_connection != connection && iter_connection->current_chunk != NULL)
			if(iter_connection != connection && !iter_connection->scheduler->bad_connection)
			{
				double iter_bytes_left = 0;

				if(iter_connection->current_chunk != NULL)
				{
					iter_bytes_left = (double)(iter_connection->current_chunk->data_size - iter_connection->current_chunk->pos_save);
				}
				
				double iter_bandwidth = (double)(iter_connection->scheduler->bandwidth);
				double iter_time_left = iter_bytes_left/iter_bandwidth;

				/* Check whether this connection might be faster */
				// TODO: check if && or ||
				if((bytes_left/iter_bandwidth + iter_connection->scheduler->rtt + ASSURANCE_PENALTY < bytes_left/scheduler->bandwidth) /*|| (iter_connection->interface->if_id < connection->interface->if_id)*/)
				{
					/* Check if connection ready before us (and soon...) */
					if(iter_time_left < TIME_LEFT_UNTIL_SHUTDOWN && iter_time_left < time_left)
					{
						// TODO: verify
						//if(bandwidth > 0)
						//{
							LOG_INFO("%ssd#%d interface %s with bandwidth = %zu kb/s will gain over current = %zu kb/s",RESULT_EVENT,scheduler->connection->sd,iter_connection->interface->name,(size_t)(iter_bandwidth/1024),(size_t)(bandwidth/1024));
							return TRUE;
						//}
					}
				}
	
				/* 2nd Requirement: another connection will finish faster than us */
				/* 3rd Requirement: the other connection has a sufficiently better bandwidth than us (or better interface) */
				// TODO: verify
				/*
				if((time_left > iter_time_left + iter_connection->scheduler->rtt && bandwidth < BANDWIDTH_DIFF*iter_bandwidth && bandwidth > 0 && iter_bandwidth > 0) || (iter_connection->interface->if_id < connection->interface->if_id))
				{
					// another connection should finish this chunk
					// check if now is a good time to stop the current request
					// 4th Requirement: the connection finishes its request soon
					if(iter_time_left < TIME_LEFT_UNTIL_SHUTDOWN)
					{
						// 5th Requirement: is there a gain to be expected when changing connection
						size_t remaining_bytes = scheduler->connection->current_chunk->data_size - scheduler->connection->current_chunk->pos_save;
						// TODO: verify
						if(remaining_bytes/iter_bandwidth + iter_connection->scheduler->rtt < remaining_bytes/scheduler->bandwidth)
						//if(remaining_bytes/iter_bandwidth < remaining_bytes/scheduler->bandwidth)
						{
							LOG_INFO("%ssd#%d bandwidth = %zu kb/s will gain over current = %zu kb/s",RESULT_EVENT,scheduler->connection->sd,(size_t)(iter_bandwidth/1024),(size_t)(bandwidth/1024));

							// now is a good time - wrap it up
							return TRUE;
						}
					}
				}
				*/
			}
		}
	}

	return FALSE;
}
