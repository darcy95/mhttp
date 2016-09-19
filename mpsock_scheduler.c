#include "mpsock_scheduler.h"
#include "mpsock_pool.h"
#include "mpsock_socket.h"
#include "http_parser.h"
#include "mpsock_connection.h"
#include "mpsock_dns.h"
#include "mpsock_interface.h"
#include "mpsock_scheduler_minimum_throughput_assurance.h"
#include "mpsock_scheduler_algorithms.h"
#include "mpsock_def.h"

mpsock_scheduler *create_scheduler(struct mpsock_connection *connection)
{
	mpsock_scheduler *scheduler = (mpsock_scheduler*)malloc(sizeof(mpsock_scheduler));
	scheduler->connection = connection;
	scheduler->send_start.tv_sec=0;
	scheduler->send_start.tv_usec=0;
	scheduler->last_read.tv_sec=0;
	scheduler->last_read.tv_usec=0;
	scheduler->first_read.tv_sec=0;
	scheduler->first_read.tv_usec=0;
	scheduler->first_recv.tv_sec=0;
	scheduler->first_recv.tv_usec=0;
	scheduler->tmp_time.tv_sec=0;
	scheduler->tmp_time.tv_usec=0;
	scheduler->last_byte_read.tv_sec=0;
	scheduler->last_byte_read.tv_usec=0;
	scheduler->total_number_bytes = 0;
	scheduler->rtt = 0;
	scheduler->bandwidth = 0;
	scheduler->last_bandwidth = 0;
	scheduler->num_samples = 0;
	scheduler->num_reads = 0;
	scheduler->num_sends = 0;
	scheduler->alpha = connection->pool->mpsocket->initial_alpha;
	scheduler->is_decreased = FALSE;
	scheduler->bad_connection = FALSE;
	return scheduler;
}

void reinit_scheduler(mpsock_scheduler *scheduler)
{
	scheduler->send_start.tv_sec=0;
	scheduler->send_start.tv_usec=0;
	scheduler->last_read.tv_sec=0;
	scheduler->last_read.tv_usec=0;
	scheduler->first_read.tv_sec=0;
	scheduler->first_read.tv_usec=0;
	scheduler->first_recv.tv_sec=0;
	scheduler->first_recv.tv_usec=0;
	scheduler->tmp_time.tv_sec=0;
	scheduler->tmp_time.tv_usec=0;
	scheduler->last_byte_read.tv_sec=0;
	scheduler->last_byte_read.tv_usec=0;
	scheduler->num_reads = 0;
	scheduler->num_sends = 0;
	scheduler->total_number_bytes = 0;

	// TODO: this should only be reinitialized if the connection serves another host (in which case this whole struct would be freed anyways)
	
	//scheduler->num_samples = 0;
	//scheduler->rtt = 0;
	//scheduler->bandwidth = 0;
	//scheduler->last_bandwidth = 0;
	//scheduler->alpha = connection->pool->mpsocket->initial_alpha;
	//scheduler->bad_connection = FALSE;

	return;
}

int performs_bad(mpsock_scheduler *scheduler)
{
	// TODO: implement
	if(scheduler->bad_connection) return BAD_PERF_CLOSE;
	//if(scheduler->connection->chunk_size == 0) return BAD_PERF_CLOSE;
	//if((rand()%1000) < 10) return TRUE;

	// check whether we did not receive data in a 'long' time
	gettimeofday(&(scheduler->tmp_time),NULL);
	double tmp_diff = 0;
	// TODO: verify with send_start
	if(scheduler->last_read.tv_sec < scheduler->send_start.tv_sec)
	{
		tmp_diff = scheduler->tmp_time.tv_sec - scheduler->send_start.tv_sec;
		tmp_diff += (double)(scheduler->tmp_time.tv_usec - scheduler->send_start.tv_usec)/1000000;
	}
	else
	{
		// TODO: use different ts than last_read!!!
		tmp_diff = scheduler->tmp_time.tv_sec - scheduler->last_byte_read.tv_sec;
		tmp_diff += (double)(scheduler->tmp_time.tv_usec - scheduler->last_byte_read.tv_usec)/1000000;
	}

	if(tmp_diff > MAX_DATA_WAIT)
	{
		struct timeval tt_ts;
		gettimeofday(&tt_ts,NULL);
		double tt_diff = tt_ts.tv_sec - global_start_ts.tv_sec;
		tt_diff += (double)(tt_ts.tv_usec - global_start_ts.tv_usec)/1000000;

		LOG_INFO("%ssd#%d at %fs data receive timeout",RESULT_EVENT,scheduler->connection->sd,tt_diff);
		return BAD_PERF_CLOSE;
	}

	// check whether this is the last chunk in request and if there is a better connection to request it from
	if(!scheduler->connection->waiting)
	{
		if(optimize_final_chunk_request(scheduler))
		{
			struct timeval tt_ts;
			gettimeofday(&tt_ts,NULL);
			double tt_diff = tt_ts.tv_sec - global_start_ts.tv_sec;
			tt_diff += (double)(tt_ts.tv_usec - global_start_ts.tv_usec)/1000000;

			LOG_INFO("%ssd#%d at %fs request last chunk over different connection",RESULT_EVENT,scheduler->connection->sd,tt_diff);
			return BAD_PERF_KEEP;
		}
	}
	
	return FALSE;
}

int partial_req_over_new_con(mpsock_scheduler *scheduler)
{
	// TODO: implement
	mpsock_pool *pool = scheduler->connection->pool;

	// check whether last chunk is already requested
	if(pool->next_start_byte >= pool->current_file_size && HASH_COUNT(pool->partial_chunk_table) <= 1) return FALSE;

	// still a lot of bytes to request?
	if(pool->next_start_byte < pool->current_file_size*0.85 || HASH_COUNT(pool->partial_chunk_table) > 1) return TRUE;

	return FALSE;
}

int reached_maximum(mpsock_scheduler *scheduler)
{
	if(scheduler->num_sends == scheduler->connection->pool->mpsocket->max_req_con && scheduler->connection->pool->mpsocket->max_req_con > 0)
	{
		LOG_INFO("%sconnection sd#%d needs to be closed",RESULT_EVENT,scheduler->connection->sd);
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

int needs_new_connection(mpsock_scheduler *scheduler)
{
	LOG_DEBUG("%snum_sends = %zu",FUN_EVENT,scheduler->num_sends);
	if(scheduler->num_sends == scheduler->connection->pool->mpsocket->max_req_con-1 && scheduler->connection->pool->next_start_byte < scheduler->connection->pool->current_file_size && scheduler->connection->pool->mpsocket->max_req_con > 0)
	{
		LOG_INFO("%ssd#%d creates new connection",COND_EVENT,scheduler->connection->sd);
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

void scheduler_send_event(mpsock_scheduler *scheduler)
{
	gettimeofday(&(scheduler->send_start),NULL);
	LOG_DEBUG("%ssd=%d send_timestamp=%zu.%zu",RESULT_EVENT,scheduler->connection->sd,scheduler->send_start.tv_sec,scheduler->send_start.tv_usec);
	memset(&(scheduler->first_recv),0,sizeof(struct timeval));
	memset(&(scheduler->first_read),0,sizeof(struct timeval));
	scheduler->total_number_bytes = 0;
	scheduler->num_reads = 0;
	scheduler->num_sends++;
	lock_for_calc(scheduler->connection->pool);
	scheduler->connection->pool->num_sends++;
	scheduler->connection->pool->mpsocket->num_sends++;
	unlock_for_calc(scheduler->connection->pool);
	LOG_DEBUG("%stotal_requests=%zu",RESULT_EVENT,scheduler->connection->pool->mpsocket->num_sends);
	return;
}

void scheduler_read_event(mpsock_scheduler *scheduler, int bytes)
{
	if(bytes == 0) return;
	
	scheduler->num_reads++;
	scheduler->total_number_bytes += bytes;
	scheduler->connection->bytes_received += bytes;

	if(!((scheduler->first_recv.tv_sec == 0 && scheduler->first_recv.tv_usec == 0) || (scheduler->num_reads % (scheduler->connection->pool->mpsocket->processing_skips+1) == 0)))
	{
		// save processing power
		return;
	}

	if(USE_ASSERTS) assert(scheduler->send_start.tv_sec != 0);
	gettimeofday(&(scheduler->last_read),NULL);
	if(bytes > 0)
	{
		gettimeofday(&(scheduler->last_byte_read),NULL);
	}
	
	/* ================== RTT Estimate =================*/
	if(scheduler->first_recv.tv_sec == 0 && scheduler->first_recv.tv_usec == 0)
	{
		//if(USE_ASSERTS) assert(scheduler->total_number_bytes == 0);
		if(USE_ASSERTS) assert(scheduler->num_reads == 1);
		scheduler->first_recv.tv_sec = scheduler->last_read.tv_sec;
		scheduler->first_recv.tv_usec = scheduler->last_read.tv_usec;

		if(USE_ASSERTS) assert(scheduler->rtt >= 0);

		double rtt_sample = scheduler->first_recv.tv_sec - scheduler->send_start.tv_sec;
		rtt_sample += (double)(scheduler->first_recv.tv_usec - scheduler->send_start.tv_usec)/1000000;

		if(USE_ASSERTS) assert(rtt_sample > 0);

		LOG_DEBUG("%srtt sample = %fs",RESULT_EVENT,rtt_sample);

		if(scheduler->rtt > 0)
		{
			// calculate new RTT
			// NewRTT = alpha*OldRTT + (1-alpha)*NewSample
			scheduler->rtt = DIST_FACTOR*scheduler->rtt + (1-DIST_FACTOR)*rtt_sample;
		}
		else
		{
			// very first estimate
			LOG_DEBUG("%ssd#%d first RTT",COND_EVENT,scheduler->connection->sd);
			scheduler->rtt = rtt_sample;
		}

		if(scheduler->connection->pool->mpsocket->log_metrics)
		{
			double sync_diff = scheduler->last_read.tv_sec - global_start_ts.tv_sec;
			sync_diff += (double)(scheduler->last_read.tv_usec - global_start_ts.tv_usec)/1000000;
			//LOG_METRICS("rtt %d %f %f\n",scheduler->connection->sd,sync_diff,scheduler->rtt);
			printf("rtt sd#%d %f %f\n",scheduler->connection->sd,sync_diff,scheduler->rtt);
		}

		if(USE_ASSERTS) assert(scheduler->rtt > 0);
		
		LOG_DEBUG("%ssd#%d RTT = %f",RESULT_EVENT,scheduler->connection->sd,scheduler->rtt);
	}
	/* ========================================================= */

	if(scheduler->first_read.tv_sec == 0 && scheduler->first_read.tv_usec == 0)
	{
		scheduler->first_read.tv_sec = scheduler->last_read.tv_sec;
		scheduler->first_read.tv_usec = scheduler->last_read.tv_usec;
	}

	/* ==================== Bandwidth Estimate ======================== */
	if(scheduler->num_reads > READ_SKIPS && (scheduler->num_reads % (scheduler->connection->pool->mpsocket->processing_skips+1) == 0))
	{
		/*
		if(scheduler->first_read.tv_sec == 0 && scheduler->first_read.tv_usec == 0)
		{
			if(USE_ASSERTS) assert(scheduler->num_reads == READ_SKIPS + 1);

			scheduler->first_read.tv_sec = scheduler->last_read.tv_sec;
			scheduler->first_read.tv_usec = scheduler->last_read.tv_usec;
			
			return;
		}
		*/

		// TODO: measure first from send_start
		double diff = 0;
		if(scheduler->bandwidth == 0)
		{
			diff = (double)(scheduler->last_read.tv_sec - scheduler->send_start.tv_sec);
			diff += (double)(scheduler->last_read.tv_usec - scheduler->send_start.tv_usec)/1000000;
		}
		else
		{
			diff = (double)(scheduler->last_read.tv_sec - scheduler->first_read.tv_sec);
			diff += (double)(scheduler->last_read.tv_usec - scheduler->first_read.tv_usec)/1000000;
		}
	
		LOG_DEBUG("%ssd#%d difference: %f",RESULT_EVENT,scheduler->connection->sd,diff);
	
		if(USE_ASSERTS) assert(diff >= 0);

		if(diff > 0)
		{
			//scheduler->total_number_bytes += bytes;

			size_t bandwidth_sample = scheduler->total_number_bytes/diff;
			scheduler->num_samples++;

			if(USE_ASSERTS) assert(bandwidth_sample > 0);

			if(scheduler->bandwidth > 0)
			{
				// calculate new Bandwidth
				// NewBandwidth = alpha*OldBandwidth + (1-alpha)*NewSample
				if(USE_HARMONIC_MEAN)
				{
					double num_samples = scheduler->num_samples;
					scheduler->bandwidth = num_samples/(((num_samples-1)/scheduler->bandwidth) + (1/(double)(bandwidth_sample)));
				}
				else
				{
					scheduler->bandwidth = DIST_FACTOR*scheduler->bandwidth + (1-DIST_FACTOR)*bandwidth_sample;
				}
			}
			else
			{
				// very first sample
				scheduler->bandwidth = bandwidth_sample;
			}

			if(scheduler->connection->pool->mpsocket->log_metrics)
			{
				double sync_diff = scheduler->last_read.tv_sec - global_start_ts.tv_sec;
				sync_diff += (double)(scheduler->last_read.tv_usec - global_start_ts.tv_usec)/1000000;
				//LOG_METRICS("B %d %f %zu\n",scheduler->connection->sd,sync_diff,scheduler->bandwidth/1024);
				printf("Bandwidth sd#%d %f %zu\n",scheduler->connection->sd,sync_diff,scheduler->bandwidth/1024);
			}
		}
	}
	/* ==================================================== */

	LOG_DEBUG("%ssd=%d last_read_timestamp=%zu.%zu",RESULT_EVENT,scheduler->connection->sd,scheduler->last_read.tv_sec,scheduler->last_read.tv_usec);

	return;
}

int use_scheduler(mpsock_scheduler *scheduler)
{
	if(scheduler->connection->pool->current_file_size > MIN_SIZE_FOR_SCHEDULER && USE_SCHEDULER)
	{
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

size_t scheduler_get_chunk_size(mpsock_scheduler *scheduler)
{
	// TODO: bad connection reconnects -> max_connection needs to be adjusted!
	lock_for_calc(scheduler->connection->pool);

	size_t chunk_size;

	if(use_scheduler(scheduler))
	{
		if(USE_ASSERTS) assert(scheduler->bandwidth >= 0);

		/* ==================== Bandwidth Estimate ======================== */
		/*
		size_t bandwidth_sample = 0;

		double diff = (double)(scheduler->last_read.tv_sec - scheduler->send_start.tv_sec);
		diff += (double)(scheduler->last_read.tv_usec - scheduler->send_start.tv_usec)/1000000;
	
		LOG_DEBUG("%ssd#%d difference: %f",RESULT_EVENT,scheduler->connection->sd,diff);
	
		if(USE_ASSERTS) assert(diff >= 0);

		if(diff > 0)
		{
			bandwidth_sample = scheduler->total_number_bytes/diff;

			if(USE_ASSERTS) assert(bandwidth_sample > 0);

			if(scheduler->bandwidth > 0)
			{
				// calculate new Bandwidth
				// NewBandwidth = alpha*OldBandwidth + (1-alpha)*NewSample
				scheduler->bandwidth = DIST_FACTOR*scheduler->bandwidth + (1-DIST_FACTOR)*bandwidth_sample;
			}
			else
			{
				// very first sample
				scheduler->bandwidth = bandwidth_sample;
			}

			scheduler->last_bandwidth = bandwidth_sample;
		}
		*/
		/* ==================================================== */

		LOG_DEBUG("%ssd#%d Total Received Bytes = %zu",RESULT_EVENT,scheduler->connection->sd,scheduler->total_number_bytes);
		LOG_DEBUG("%ssd#%d Estimated Bandwidth = %zu KB/s, Previous Bandwidth = %zu KB/s",RESULT_EVENT,scheduler->connection->sd,scheduler->bandwidth/1024,scheduler->last_bandwidth/1024);

		//if(USE_ASSERTS) assert((DYNAMIC_ALPHA_ALGORITHM + TIME_CHUNK_ALGORITHM) == 1);

		if(scheduler->connection->pool->mpsocket->scheduler_algo == 1)
		{
			chunk_size = dynamic_alpha_algorithm(scheduler);
		}
		else if(scheduler->connection->pool->mpsocket->scheduler_algo == 2)
		{
			chunk_size = time_chunk_algorithm(scheduler);
		}
		else if(scheduler->connection->pool->mpsocket->scheduler_algo == 0)
		{
			chunk_size = scheduler->connection->pool->mpsocket->initial_chunk_size;
		}
		else
		{
			perror("No valid Scheduler Algorithm selected! Selected");
			exit(1);
		}
	}
	else
	{
		// no scheduler -> simply double until max
		chunk_size = scheduler->connection->chunk_size;
		while(scheduler->connection->pool->current_file_size - scheduler->connection->pool->mpsocket->max_connections*chunk_size > 0 && chunk_size < MAXIMUM_CHUNK_SIZE)
		{
			// double
			chunk_size *= 2;
		}
	}

	// final adjustments
	
	// in case it is too high
	if(chunk_size > MAXIMUM_CHUNK_SIZE)
	{
		chunk_size = MAXIMUM_CHUNK_SIZE;
		LOG_INFO("%ssd#%d due to maximum_size reduced chunk_size to %zu KB",RESULT_EVENT,scheduler->connection->sd,chunk_size/1024);
	}

	if(chunk_size < MINIMUM_CHUNK_SIZE)
	{
		chunk_size = MINIMUM_CHUNK_SIZE;
		LOG_INFO("%ssd#%d due to minimum_size raised chunk_size to %zu KB",RESULT_EVENT,scheduler->connection->sd,chunk_size/1024);
	}

	// adjust so we do not exceed maximum request limit
	if(scheduler->connection->pool->current_file_size - scheduler->connection->pool->next_start_byte > chunk_size*(scheduler->connection->pool->mpsocket->max_req_serv - scheduler->connection->pool->num_sends) && scheduler->connection->pool->mpsocket->max_req_serv > 0)
	{
		// adjust
		chunk_size = (scheduler->connection->pool->current_file_size - scheduler->connection->pool->next_start_byte)/(scheduler->connection->pool->mpsocket->max_req_serv - scheduler->connection->pool->num_sends);
		LOG_INFO("%ssd#%d to avoid request limit set chunk_size to %zu KB",RESULT_EVENT,scheduler->connection->sd,chunk_size/1024);
	}

	// adjust in case it is too big
	if(chunk_size > scheduler->connection->pool->current_file_size - scheduler->connection->pool->current_chunk_request->start_byte)
	{
		chunk_size = scheduler->connection->pool->current_file_size - scheduler->connection->pool->current_chunk_request->start_byte;
		LOG_INFO("%ssd#%d due to file_size reduced chunk_size to %zu KB",RESULT_EVENT,scheduler->connection->sd,chunk_size/1024);
	}

	if(USE_ASSERTS) assert(chunk_size >= 0);

	// determine best connection in terms of bandwidth
	// TODO: remove this!
	if(scheduler->connection->pool->best_connection == NULL)
	{
		scheduler->connection->pool->best_connection = scheduler->connection;
	}
	else if(scheduler->connection->pool->best_connection->scheduler->bandwidth < scheduler->bandwidth)
	{
		scheduler->connection->pool->best_connection = scheduler->connection;
	}

	// ensure that in case of single byte left, we also take care of that
	size_t rem_diff = scheduler->connection->pool->current_file_size - (scheduler->connection->pool->current_chunk_request->start_byte + chunk_size);
	if(rem_diff > 0 && rem_diff < MAX_BYTES_LEFT)
	{
		LOG_INFO("%ssd#%d just %zu bytes of file remaining -> add to current request",RESULT_EVENT,scheduler->connection->sd,rem_diff);
		chunk_size += rem_diff;
	}

	unlock_for_calc(scheduler->connection->pool);

	LOG_INFO("%ssd#%d bandwidth = %zu kb/s\tRTT = %f s",RESULT_EVENT,scheduler->connection->sd,scheduler->bandwidth/1024,scheduler->rtt);

	return chunk_size;
}

int enough_res_for_conn(mpsock_scheduler *scheduler)
{
	// baseline scheduler
	if(scheduler->connection->pool->mpsocket->scheduler_algo == 0)
	{
		if(scheduler->connection->pool->block_new_conns)
		{
			return FALSE;
		}
	}

	mpsock_connection *conn = scheduler->connection;
	// TODO: better
	//if(HASH_COUNT(conn->pool->connection_table) < conn->pool->mpsocket->max_connections && conn->parser->status_code == 206)
	if(num_active_connections(conn->pool) - num_waiting_connections(conn->pool) < conn->pool->mpsocket->max_connections && conn->parser->status_code == 206)
	{
		// TODO: good idea to place this here???
		if(scheduler->rtt * ALPHA_MIN * scheduler->bandwidth > conn->pool->current_file_size)
		{
			conn->pool->finishing = TRUE;
		}

		double download_time = (double)((double)(conn->pool->current_file_size)/(double)(scheduler->bandwidth));
		//if(conn->pool->current_file_size > conn->chunk_size)
		//if((conn->pool->next_start_byte + conn->chunk_size < conn->pool->current_file_size || HASH_COUNT(conn->pool->partial_chunk_table) > 1) && conn->pool->current_file_size > conn->chunk_size && conn->pool->current_file_size >= MIN_SIZE_FOR_MULTIPATH)
		if((download_time > MIN_DOWNLOAD_TIME && conn->pool->next_start_byte < conn->pool->current_file_size) || HASH_COUNT(conn->pool->partial_chunk_table) > 1)
		{
			LOG_DEBUG("%sEstimated Download Time: %f",FUN_EVENT,download_time);
			return TRUE;
		}
		else
		{
			return FALSE;
		}
	}
	else
	{
		return FALSE;
	}	
}

unsigned long scheduler_get_ip(mpsock_scheduler *scheduler, const char *host)
{
	int i;
	mpsock_addrs *e;
	e = lookup_by_name(host);
  
 	if(USE_ASSERTS) assert(e != NULL);

	scheduler->connection->adrs = e;

	if(e)
	{
		for(i = 0 ; i < e->count ; i++)
		{
			if(e->ipset[i].inuse == FALSE/*&& e->ipset[i].priority*/)
			{
				e->ipset[i].inuse = TRUE;
				scheduler->connection->adrs_index = i;
				LOG_DEBUG("%sIndex %d",RESULT_EVENT,i);
				scheduler->connection->ip = e->ipset[i].ip;
				LOG_INFO("%ssd#%d using host %zu",RESULT_EVENT,scheduler->connection->sd,e->ipset[i].ip);
				return e->ipset[i].ip;
			}
		}
	} // error handling? if (e == NULL)

	scheduler->connection->adrs_index = 0;
	scheduler->connection->ip = e->ipset[0].ip;
	LOG_DEBUG("%sBLAB %d",RESULT_EVENT,i);
	LOG_INFO("%ssd#%d using host %zu",RESULT_EVENT,scheduler->connection->sd,e->ipset[0].ip);
	return e->ipset[0].ip;
}

mpsock_interface *scheduler_get_interface(mpsock_scheduler *scheduler)
{
	mpsock_interface *first = NULL;
	mpsock_interface *second = NULL;
	mpsock_interface *e;

	lock_for_interface(scheduler->connection->pool);
	if(scheduler->connection->pool->mpsocket->use_random_path)
	{
		for(e = mpsock_interface_table ; e != NULL ; e = e->hh.next)
		{
			if(first != NULL && second == NULL)
			{
				second = e;
			}

			if(first == NULL)
			{
				first = e;
			}
		}

		mpsock_interface *res;

		if(first->num_used == second->num_used)
		{
			if(rand()%2)
			{
				res = first;
				scheduler->connection->pool->first_iface++;
			}
			else
			{
				res = second;
				scheduler->connection->pool->second_iface++;
			}
		}
		else if(first->num_used < second->num_used)
		{
			res = first;
			scheduler->connection->pool->first_iface++;
		}
		else
		{
			res = second;
			scheduler->connection->pool->second_iface++;
		}

		scheduler->connection->interface = res;
		res->num_used++;
		LOG_INFO("%ssd#%d using interface#%d: %s",RESULT_EVENT,scheduler->connection->sd,res->if_id,res->name);
		unlock_for_interface(scheduler->connection->pool);
		return res;
	}
	else
	{
		for(e = mpsock_interface_table ; e != NULL ; e = e->hh.next)
		{
			if(first != NULL && second == NULL)
			{
				second = e;
				if(scheduler->connection->pool->first_iface > scheduler->connection->pool->second_iface && scheduler->connection->pool->second_iface == 0)
				{
					scheduler->connection->interface = e;
					//e->used_by = scheduler->connection;
					e->num_used++;
					scheduler->connection->pool->second_iface++;
					LOG_INFO("%ssd#%d using interface#%d: %s",RESULT_EVENT,scheduler->connection->sd,e->if_id,e->name);
					unlock_for_interface(scheduler->connection->pool);
					return e;
				}
			}

			if(first == NULL)
			{
				first = e;
				if(scheduler->connection->pool->first_iface <= scheduler->connection->pool->second_iface && scheduler->connection->pool->first_iface == 0)
				{
					scheduler->connection->interface = e;
					//e->used_by = scheduler->connection;
					e->num_used++;
					scheduler->connection->pool->first_iface++;
					LOG_INFO("%ssd#%d using interface#%d: %s",RESULT_EVENT,scheduler->connection->sd,e->if_id,e->name);
					unlock_for_interface(scheduler->connection->pool);
					return e;
				}
			}

			// TODO: enhance: use usage counter instead of flag
			//if(e->in_use == FALSE)
			if(e->num_used == 0)
			{
				LOG_DEBUG("%swill use interface#%d: %s", COND_EVENT, e->if_id, e->name);
				//e->in_use = TRUE;
				//e->used_by = scheduler->connection;
				scheduler->connection->interface = e;
				//e->used_by = scheduler->connection;
				e->num_used++;
				LOG_INFO("%ssd#%d using interface#%d: %s",RESULT_EVENT,scheduler->connection->sd,e->if_id,e->name);
				unlock_for_interface(scheduler->connection->pool);
				return e;
			}
		}

		// TODO: verify
		if(first->num_used <= second->num_used) 
		{
			e = first;
		}
		else
		{
			e = second;
		}

		e->num_used++;

		unlock_for_interface(scheduler->connection->pool);

		// none free -> shuffle
		//if((rand()%10) < 5 || second == NULL)
		//{
		//	e = first;
		//}
		//else
		//{
		//	e = second;
		//}

		// return first interface otherwise
		//e = first;

		scheduler->connection->interface = e;
		LOG_INFO("%ssd#%d using interface#%d: %s",RESULT_EVENT,scheduler->connection->sd,e->if_id,e->name);
		return e;
	}
}

unsigned long scheduler_get_first_ip(mpsock_scheduler *scheduler, unsigned long ip)
{
	mpsock_addrs *e;
	e = lookup_by_keyip(ip); 

	//if(e == NULL) perror("Can't find the IP entry.");
	if(USE_ASSERTS) assert(e != NULL);

	scheduler->connection->adrs = e;
	scheduler->connection->adrs_index = 0;
	scheduler->connection->ip = e->ipset[0].ip;

	e->ipset[0].inuse = 1;
 
	return e->ipset[0].ip;
}
