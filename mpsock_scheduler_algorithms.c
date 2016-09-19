#include "mpsock_scheduler_algorithms.h"
#include "mpsock_socket.h"
#include "mpsock_pool.h"
#include "mpsock_connection.h"

size_t dynamic_alpha_algorithm(mpsock_scheduler *scheduler)
{
	/* ================== alpha estimate ==================== */
	size_t chunk_size = 0;
	size_t bandwidth_sample = 0;

	double diff = (double)(scheduler->last_read.tv_sec - scheduler->send_start.tv_sec);
	diff += (double)(scheduler->last_read.tv_usec - scheduler->send_start.tv_usec)/1000000;

	LOG_DEBUG("%ssd#%d difference: %f",RESULT_EVENT,scheduler->connection->sd,diff);

	if(USE_ASSERTS) assert(diff >= 0);

	if(diff > 0)
	{
		bandwidth_sample = scheduler->total_number_bytes/diff;

		if(USE_ASSERTS) assert(bandwidth_sample > 0);

		scheduler->last_bandwidth = bandwidth_sample;
	}

	if(scheduler->bandwidth > 0 && scheduler->last_bandwidth > 0)
	{
		//if((1+OPTIMAL_RANGE_DIFF)*scheduler->last_bandwidth < scheduler->bandwidth || scheduler->is_decreased)
		if((1+OPTIMAL_RANGE_DIFF)*scheduler->last_bandwidth < bandwidth_sample || scheduler->is_decreased)
		{
			// if chunk_size is already too big, then it does not make sense to raise alpha
			if(scheduler->alpha * scheduler->rtt * scheduler->bandwidth < MAXIMUM_CHUNK_SIZE)
			{
				// bandwidth rose a lot -> raise alpha
				//scheduler->alpha *= ALPHA_RAISE_BIG;
				scheduler->alpha += ALPHA_LINEAR_INC;
				LOG_DEBUG("%ssd#%d raise alpha to %f",COND_EVENT,scheduler->connection->sd,scheduler->alpha);
			}

			scheduler->is_decreased = FALSE;
		}
		else
		{
			if(!scheduler->is_decreased)
			{
				scheduler->alpha *= ALPHA_DECREASE;
				scheduler->is_decreased = TRUE;
				LOG_DEBUG("%ssd#%d decrease alpha to %f",COND_EVENT,scheduler->connection->sd,scheduler->alpha);
			}
		}
		/*
		else if(scheduler->last_bandwidth < scheduler->bandwidth)
		{
			// TODO: verify
			// if chunk_size is already too big, then it does not make sense to raise alpha
			if(scheduler->alpha * scheduler->rtt * scheduler->bandwidth < MAXIMUM_CHUNK_SIZE)
			{
				// bandwidth rose a lot -> raise alpha
				scheduler->alpha *= ALPHA_RAISE_SMALL;
				LOG_INFO("%ssd#%d small raise alpha to %f",COND_EVENT,scheduler->connection->sd,scheduler->alpha);
			}
		}
		else if((1-OPTIMAL_RANGE_DIFF)*scheduler->last_bandwidth > scheduler->bandwidth)
		{
			// bandwidth smaller now -> fall back to previous alpha
			scheduler->alpha *= (1/ALPHA_RAISE_BIG);
			LOG_INFO("%ssd#%d lowered alpha to %f",COND_EVENT,scheduler->connection->sd,scheduler->alpha);
		}
		*/

		// TODO: make alpha dynamic
		//scheduler->alpha = INITIAL_ALPHA;
		LOG_DEBUG("%ssd#%d current alpha = %f",RESULT_EVENT,scheduler->connection->sd,scheduler->alpha);
	}
	/* ====================================================== */

	// set current bandwidth as last used
	//scheduler->last_bandwidth = scheduler->bandwidth;

	chunk_size = scheduler->alpha * scheduler->rtt * scheduler->bandwidth;
	//size_t chunk_size = scheduler->bandwidth;

	if(chunk_size == 0)
	{
		// TODO: maybe better
		LOG_DEBUG("%snot enough reads to estimate bandwidth --> just doubling current chunk_size",COND_EVENT);
		//chunk_size = scheduler->connection->chunk_size * 2;
		chunk_size = scheduler->connection->pool->mpsocket->initial_chunk_size;
	}

	return chunk_size;
}

size_t time_chunk_algorithm(struct mpsock_scheduler *scheduler)
{
	size_t chunk_size = 0;

	// initial case
	if(scheduler->bandwidth == 0 || scheduler->rtt == 0) return scheduler->connection->pool->mpsocket->initial_chunk_size;

	if(USE_ASSERTS) assert(scheduler->bandwidth > 0);
	if(USE_ASSERTS) assert(scheduler->rtt > 0);

	/* ============= Calculate Time Slice ============ */
	double time_span = 0;
	double t_max = scheduler->connection->pool->mpsocket->alpha_max*scheduler->rtt;
	double t_min = ALPHA_MIN*scheduler->rtt;
	size_t remaining_bytes = scheduler->connection->pool->current_file_size - scheduler->connection->pool->next_start_byte;

	if(USE_ASSERTS) assert(remaining_bytes >= 0);

	// determine beta
	size_t beta = -1;
	mpsock_connection *current_con;
	mpsock_connection *beta_connection = NULL;

	lock_for_scheduling(scheduler->connection->pool);
	lock_for_next_chunk(scheduler->connection->pool);

	for(current_con = scheduler->connection->pool->connection_table; current_con != NULL; current_con = current_con->hh.next)
	{
		if(current_con == scheduler->connection || current_con->scheduler->bandwidth == 0 || current_con->current_chunk == NULL || !current_con->is_used) continue;

		size_t bytes_left = current_con->current_chunk->data_size - current_con->current_chunk->pos_save;
		double delta_t = ((double)(bytes_left)) / ((double)(current_con->scheduler->bandwidth));

		if(USE_ASSERTS) assert(delta_t >= 0);

		if(beta < 0 || beta > delta_t)
		{
			beta_connection = current_con;
			beta = delta_t;
		}
	}

	if(beta < 0) 
	{
		beta_connection = NULL;
		beta = 0;
	}

	size_t beta_bandwidth = 0;
	double beta_t_max = 0;
	if(beta_connection != NULL)
	{
		beta_bandwidth = beta_connection->scheduler->bandwidth;
		beta_t_max = beta_connection->scheduler->rtt * scheduler->connection->pool->mpsocket->alpha_max;

		if(USE_MIN_T_MAX)
		{
			if(t_max > beta_t_max && beta_t_max > 0)
			{
				t_max = beta_t_max;
			}
			else if(t_max > 0)
			{
				beta_t_max = t_max;
			}
		}
	}

	if(USE_ASSERTS) assert(beta_connection != scheduler->connection);
	if(USE_ASSERTS) assert(beta >= 0);

	unlock_for_next_chunk(scheduler->connection->pool);
	unlock_for_scheduling(scheduler->connection->pool);

	/* =================================== 4-CASE-ALGORITHM START =========================================== */
	/* 1. T_max,i * R_i >= s - x*/
	/*
	if(t_max * scheduler->bandwidth >= remaining_bytes)
	{
		if(beta * scheduler->bandwidth >= remaining_bytes || (scheduler->connection->pool->finishing && !FLAG_MAX_ALGO))
		{
			// download entire remaining file
			//time_span = ((double)(remaining_bytes)) / scheduler->bandwidth;

			// to ensure complete download and avoid rounding errors
			chunk_size = remaining_bytes;
			LOG_DEBUG("%ssd#%d 1.1",RESULT_EVENT,scheduler->connection->sd);
		}
		else if(scheduler->connection->pool->finishing && FLAG_MAX_ALGO)
		{
			time_span = ((double)(remaining_bytes + beta*(double)(beta_bandwidth)))/((double)(scheduler->bandwidth + beta_bandwidth));
			if(time_span < t_min)
			{
				time_span = t_min;
			}

			LOG_DEBUG("%ssd#%d 1.3",RESULT_EVENT,scheduler->connection->sd);
		}
		else
		{
			time_span = ((double)(remaining_bytes + beta*(double)(beta_bandwidth)))/((double)(scheduler->bandwidth + beta_bandwidth));
			scheduler->connection->pool->finishing = TRUE;
			LOG_DEBUG("%ssd#%d 1.2: %f == %zu",RESULT_EVENT,scheduler->connection->sd,time_span*scheduler->bandwidth + (time_span - beta)*(double)(beta_bandwidth),remaining_bytes);
		}
	}
	*/
	/* T_max,i * R_i < s - x*/
	/*
	else
	{
		time_span = ((double)(remaining_bytes + beta*(double)(beta_bandwidth)))/((double)(scheduler->bandwidth + beta_bandwidth));
		
		if(time_span > t_max || (time_span-beta) > beta_t_max)
		{
			time_span = t_max;
			LOG_DEBUG("%ssd#%d 2.2",RESULT_EVENT,scheduler->connection->sd);
		}
		else if(FLAG_MAX_ALGO)
		{
			if(time_span < t_min)
			{
				time_span = t_min;
			}

			scheduler->connection->pool->finishing = TRUE;
			LOG_DEBUG("%ssd#%d 2.3",RESULT_EVENT,scheduler->connection->sd);
		}
		else
		{
			scheduler->connection->pool->finishing = TRUE;
			LOG_DEBUG("%ssd#%d 2.1: %f == %zu",RESULT_EVENT,scheduler->connection->sd,time_span*scheduler->bandwidth + (time_span - beta)*(double)(beta_bandwidth),remaining_bytes);
		}
	}
	*/
	/* =================================== 4-CASE-ALGORITHM END =========================================== */

	/* =================================== 3-CASE-ALGORITHM START =========================================== */
	if(scheduler->connection->pool->mpsocket->scheduler_version == 0)
	{
		/* case 1 */
		if(t_max * scheduler->bandwidth >= remaining_bytes && (beta * scheduler->bandwidth >= remaining_bytes || scheduler->connection->pool->finishing))
		{
			// download entire remaining file
			//time_span = ((double)(remaining_bytes)) / scheduler->bandwidth;
	
			// to ensure complete download and avoid rounding errors
			chunk_size = remaining_bytes;
			LOG_INFO("%ssd#%d Scheduler Case 1",RESULT_EVENT,scheduler->connection->sd);
		}
		/* T_max,i * R_i < s - x*/
		else
		{
			time_span = ((double)(remaining_bytes + beta*(double)(beta_bandwidth)))/((double)(scheduler->bandwidth + beta_bandwidth));
			
			/* case 3 */
			if(time_span > t_max || (time_span-beta) > beta_t_max)
			{
				time_span = t_max;
				LOG_INFO("%ssd#%d Scheduler Case 3",RESULT_EVENT,scheduler->connection->sd);
			}
			/* case 2 */
			else
			{
				if(time_span < t_min && FLAG_MAX_ALGO)
				{
					time_span = t_min;
				}
	
				scheduler->connection->pool->finishing = TRUE;
				LOG_INFO("%ssd#%d Scheduler Case 2",RESULT_EVENT,scheduler->connection->sd);
			}

			///* case 2 */
			//if(time_span < t_max)
			//{
			//	if(time_span < t_min && FLAG_MAX_ALGO)
			//	{
			//		time_span = t_min;
			//	}
			//
			//	scheduler->connection->pool->finishing = TRUE;
			//	LOG_INFO("%ssd#%d Scheduler Case 2",RESULT_EVENT,scheduler->connection->sd);
			//	
			//}
			///* case 3 */
			//else
			//{
			//	time_span = t_max;
			//	LOG_INFO("%ssd#%d Scheduler Case 3",RESULT_EVENT,scheduler->connection->sd);
			//}
		}
	}
	else if(scheduler->connection->pool->mpsocket->scheduler_version == 1)
	{
		/* case 1 */
		if(t_max * scheduler->bandwidth >= remaining_bytes && (beta * scheduler->bandwidth >= remaining_bytes || scheduler->connection->pool->finishing))
		{
			// download entire remaining file
			//time_span = ((double)(remaining_bytes)) / scheduler->bandwidth;
	
			// to ensure complete download and avoid rounding errors
			chunk_size = remaining_bytes;
			LOG_INFO("%ssd#%d Scheduler Case 1",RESULT_EVENT,scheduler->connection->sd);
		}
		/* T_max,i * R_i < s - x*/
		else
		{
			time_span = ((double)(remaining_bytes + beta*(double)(beta_bandwidth)))/((double)(scheduler->bandwidth + beta_bandwidth));
			
			///* case 3 */
			//if(time_span > t_max || (time_span-beta) > beta_t_max)
			//{
			//	time_span = t_max;
			//	LOG_INFO("%ssd#%d Scheduler Case 3",RESULT_EVENT,scheduler->connection->sd);
			//}
			///* case 2 */
			//else
			//{
			//	if(time_span < t_min && FLAG_MAX_ALGO)
			//	{
			//		time_span = t_min;
			//	}
			//
			//	scheduler->connection->pool->finishing = TRUE;
			//	LOG_INFO("%ssd#%d Scheduler Case 2",RESULT_EVENT,scheduler->connection->sd);
			//}

			/* case 2 */
			if(time_span < t_max)
			{
				if(time_span < t_min && FLAG_MAX_ALGO)
				{
					time_span = t_min;
				}
			
				scheduler->connection->pool->finishing = TRUE;
				LOG_INFO("%ssd#%d Scheduler Case 2",RESULT_EVENT,scheduler->connection->sd);
				
			}
			/* case 3 */
			else
			{
				time_span = t_max;
				LOG_INFO("%ssd#%d Scheduler Case 3",RESULT_EVENT,scheduler->connection->sd);
			}
		}
	}
	else if(scheduler->connection->pool->mpsocket->scheduler_version == 2)
	{
		/* case 1 */
		if(t_max * scheduler->bandwidth >= remaining_bytes && (beta * scheduler->bandwidth >= remaining_bytes || (scheduler->connection->pool->finishing && scheduler->bandwidth > 0.9*beta_bandwidth)))
		{
			// download entire remaining file
			//time_span = ((double)(remaining_bytes)) / scheduler->bandwidth;
	
			// to ensure complete download and avoid rounding errors
			chunk_size = remaining_bytes;
			LOG_INFO("%ssd#%d Scheduler Case 1",RESULT_EVENT,scheduler->connection->sd);
		}
		/* T_max,i * R_i < s - x*/
		else
		{
			time_span = ((double)(remaining_bytes + beta*(double)(beta_bandwidth)))/((double)(scheduler->bandwidth + beta_bandwidth));
			
			///* case 3 */
			//if(time_span > t_max || (time_span-beta) > beta_t_max)
			//{
			//	time_span = t_max;
			//	LOG_INFO("%ssd#%d Scheduler Case 3",RESULT_EVENT,scheduler->connection->sd);
			//}
			///* case 2 */
			//else
			//{
			//	if(time_span < t_min && FLAG_MAX_ALGO)
			//	{
			//		time_span = t_min;
			//	}
			//
			//	scheduler->connection->pool->finishing = TRUE;
			//	LOG_INFO("%ssd#%d Scheduler Case 2",RESULT_EVENT,scheduler->connection->sd);
			//}

			/* case 2 */
			if(time_span < t_max)
			{
				if(time_span < t_min && FLAG_MAX_ALGO)
				{
					time_span = t_min;
				}
			
				scheduler->connection->pool->finishing = TRUE;
				LOG_INFO("%ssd#%d Scheduler Case 2",RESULT_EVENT,scheduler->connection->sd);
				
			}
			/* case 3 */
			else
			{
				time_span = t_max;
				LOG_INFO("%ssd#%d Scheduler Case 3",RESULT_EVENT,scheduler->connection->sd);
			}
		}
	}
	else
	{
		perror("undefined scheduler version");
		exit(1);
	}
	/* =================================== 3-CASE-ALGORITHM END =========================================== */

	// adjust to t_min
	//if(time_span < t_min) time_span = t_min;

	/* =============================================== */

	// convert to byte size
	if(chunk_size == 0)
	{
		chunk_size = time_span * scheduler->bandwidth;
	}

	if(chunk_size == 0) 
	{
		chunk_size = scheduler->connection->pool->mpsocket->initial_chunk_size;
	}
	else
	{
		
	}

	return chunk_size;
}
