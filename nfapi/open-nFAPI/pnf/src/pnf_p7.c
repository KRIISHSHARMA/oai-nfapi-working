/*
 * Copyright 2017 Cisco Systems, Inc.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 * http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include <sys/select.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>

#include "pnf_p7.h"

#define FAPI2_IP_DSCP	0

extern uint16_t sf_ahead;

//uint16_t sf_ahead=4;

void add_slot(uint16_t *frameP, uint16_t *slotP, int offset)
{
	uint16_t num_slots = 20; // set based on numerlogy (fixing for 1)

    *frameP    = (*frameP + ((*slotP + offset) / num_slots))%1024; 

    *slotP = ((*slotP + offset) % num_slots);
}

void add_sf(uint16_t *frameP, uint16_t *subframeP, int offset)
{
    *frameP    = (*frameP + ((*subframeP + offset) / 10))%1024;

    *subframeP = ((*subframeP + offset) % 10);
}

void subtract_sf(uint16_t *frameP, uint16_t *subframeP, int offset)
{
  if (*subframeP < offset)
  {
    *frameP = (*frameP+1024-1)%1024;
  }
  *subframeP = (*subframeP+10-offset)%10;
}

uint32_t sfnslot_add_slot(uint16_t sfn, uint16_t slot, int offset)
{
  uint32_t new_sfnslot;
//   uint16_t sfn = NFAPI_SFNSLOT2SFN(sfnslot);
//   uint16_t slot  = NFAPI_SFNSLOT2SLOT(sfnslot);

  //printf("%s() sfn:%u sf:%u\n", __FUNCTION__, sfn, sf);
  add_slot(&sfn, &slot, offset);

  new_sfnslot = sfn<<6|slot;

  //printf("%s() sfn:%u sf:%u offset:%d sfnsf:%d(DEC:%d) new:%d(DEC:%d)\n", __FUNCTION__, sfn, sf, offset, sfnsf, NFAPI_SFNSF2DEC(sfnsf), new_sfnsf, NFAPI_SFNSF2DEC(new_sfnsf));

  return new_sfnslot;
}

uint16_t sfnsf_add_sf(uint16_t sfnsf, int offset)
{
  uint16_t new_sfnsf;
  uint16_t sfn = NFAPI_SFNSF2SFN(sfnsf);
  uint16_t sf  = NFAPI_SFNSF2SF(sfnsf);

  //printf("%s() sfn:%u sf:%u\n", __FUNCTION__, sfn, sf);
  add_sf(&sfn, &sf, offset);

  new_sfnsf = sfn<<4|sf;

  //printf("%s() sfn:%u sf:%u offset:%d sfnsf:%d(DEC:%d) new:%d(DEC:%d)\n", __FUNCTION__, sfn, sf, offset, sfnsf, NFAPI_SFNSF2DEC(sfnsf), new_sfnsf, NFAPI_SFNSF2DEC(new_sfnsf));

  return new_sfnsf;
}

uint16_t sfnsf_subtract_sf(uint16_t sfnsf, int offset)
{
  uint16_t new_sfnsf;
  uint16_t sfn = NFAPI_SFNSF2SFN(sfnsf);
  uint16_t sf  = NFAPI_SFNSF2SF(sfnsf);

  //printf("%s() sfn:%u sf:%u\n", __FUNCTION__, sfn, sf);
  subtract_sf(&sfn, &sf, offset);

  new_sfnsf = sfn<<4|sf;

  //printf("%s() offset:%d sfnsf:%d(DEC:%d) new:%d(DEC:%d)\n", __FUNCTION__, offset, sfnsf, NFAPI_SFNSF2DEC(sfnsf), new_sfnsf, NFAPI_SFNSF2DEC(new_sfnsf));

  return new_sfnsf;
}

uint32_t pnf_get_current_time_hr(void)
{
	struct timeval now;
	(void)gettimeofday(&now, NULL);
	uint32_t time_hr = TIME2TIMEHR(now);
	return time_hr;
}

void* pnf_p7_malloc(pnf_p7_t* pnf_p7, size_t size)
{
	if(pnf_p7->_public.malloc)
	{
		return (pnf_p7->_public.malloc)(size);
	}
	else
	{
		return calloc(1, size); 
	}
}
void pnf_p7_free(pnf_p7_t* pnf_p7, void* ptr)
{
	if(pnf_p7->_public.free)
	{
		return (pnf_p7->_public.free)(ptr);
	}
	else
	{
		return free(ptr); 
	}
}

// todo : for now these just malloc/free need to move to a memory cache
nfapi_nr_dl_tti_request_t* allocate_nfapi_dl_tti_request(pnf_p7_t* pnf_p7) 
{ 
	void *ptr= pnf_p7_malloc(pnf_p7, sizeof(nfapi_nr_dl_tti_request_t));
        //printf("%s() ptr:%p\n", __FUNCTION__, ptr);
        return ptr;
}
nfapi_dl_config_request_t* allocate_nfapi_dl_config_request(pnf_p7_t* pnf_p7) 
{ 
	void *ptr= pnf_p7_malloc(pnf_p7, sizeof(nfapi_dl_config_request_t));
        //printf("%s() ptr:%p\n", __FUNCTION__, ptr);
        return ptr;
}
void deallocate_nfapi_dl_tti_request(nfapi_nr_dl_tti_request_t* req, pnf_p7_t* pnf_p7) 
{ 
  //printf("%s() SFN/SF:%d %s req:%p pdu_list:%p\n", __FUNCTION__, NFAPI_SFNSF2DEC(req->sfn_sf), pnf_p7->_public.codec_config.deallocate ? "DEALLOCATE" : "FREE", req, req->dl_config_request_body.dl_config_pdu_list);
	
	// if(pnf_p7->_public.codec_config.deallocate)
	// {	
	// 	(pnf_p7->_public.codec_config.deallocate)(req); 
	// }
	// else
	// {
	// 	free(req);
	// }

	pnf_p7_free(pnf_p7, req);
}

void deallocate_nfapi_dl_config_request(nfapi_dl_config_request_t* req, pnf_p7_t* pnf_p7) 
{ 
  //printf("%s() SFN/SF:%d %s req:%p pdu_list:%p\n", __FUNCTION__, NFAPI_SFNSF2DEC(req->sfn_sf), pnf_p7->_public.codec_config.deallocate ? "DEALLOCATE" : "FREE", req, req->dl_config_request_body.dl_config_pdu_list);
	if(pnf_p7->_public.codec_config.deallocate)
	{
		(pnf_p7->_public.codec_config.deallocate)(req->dl_config_request_body.dl_config_pdu_list);
	}
	else
	{
		free(req->dl_config_request_body.dl_config_pdu_list);
	}
        req->dl_config_request_body.dl_config_pdu_list=0;

	pnf_p7_free(pnf_p7, req);
}

nfapi_nr_ul_tti_request_t* allocate_nfapi_ul_tti_request(pnf_p7_t* pnf_p7) 
{ 
	void *ptr= pnf_p7_malloc(pnf_p7, sizeof(nfapi_nr_ul_tti_request_t));
        //printf("%s() ptr:%p\n", __FUNCTION__, ptr);
        return ptr;
}

nfapi_ul_config_request_t* allocate_nfapi_ul_config_request(pnf_p7_t* pnf_p7) 
{ 
	void *ptr= pnf_p7_malloc(pnf_p7, sizeof(nfapi_ul_config_request_t));
        //printf("%s() ptr:%p\n", __FUNCTION__, ptr);
        return ptr;
}

void deallocate_nfapi_ul_tti_request(nfapi_nr_ul_tti_request_t* req, pnf_p7_t* pnf_p7) 
{ 
  //printf("%s() SFN/SF:%d %s req:%p pdu_list:%p\n", __FUNCTION__, NFAPI_SFNSF2DEC(req->sfn_sf), pnf_p7->_public.codec_config.deallocate ? "DEALLOCATE" : "FREE", req, req->ul_config_request_body.ul_config_pdu_list);
	/*if(pnf_p7->_public.codec_config.deallocate)
	{
		(pnf_p7->_public.codec_config.deallocate)(req->pdus_list);
		(pnf_p7->_public.codec_config.deallocate)(req->groups_list);
	}
	else
	{
		free(req->pdus_list);
		free(req->groups_list);
	}
	*/
	pnf_p7_free(pnf_p7, req);
}

void deallocate_nfapi_ul_config_request(nfapi_ul_config_request_t* req, pnf_p7_t* pnf_p7) 
{ 
  //printf("%s() SFN/SF:%d %s req:%p pdu_list:%p\n", __FUNCTION__, NFAPI_SFNSF2DEC(req->sfn_sf), pnf_p7->_public.codec_config.deallocate ? "DEALLOCATE" : "FREE", req, req->ul_config_request_body.ul_config_pdu_list);
	if(pnf_p7->_public.codec_config.deallocate)
	{
		(pnf_p7->_public.codec_config.deallocate)(req->ul_config_request_body.ul_config_pdu_list);
	}
	else
	{
		free(req->ul_config_request_body.ul_config_pdu_list);
	}
        req->ul_config_request_body.ul_config_pdu_list=0;

	pnf_p7_free(pnf_p7, req);
}

nfapi_nr_ul_dci_request_t* allocate_nfapi_ul_dci_request(pnf_p7_t* pnf_p7) 
{ 
	return pnf_p7_malloc(pnf_p7, sizeof(nfapi_nr_ul_dci_request_t));
}

nfapi_hi_dci0_request_t* allocate_nfapi_hi_dci0_request(pnf_p7_t* pnf_p7) 
{ 
	return pnf_p7_malloc(pnf_p7, sizeof(nfapi_hi_dci0_request_t));
}

void deallocate_nfapi_ul_dci_request(nfapi_nr_ul_dci_request_t* req, pnf_p7_t* pnf_p7) 
{ 
  //printf("%s() SFN/SF:%d %s req:%p pdu_list:%p\n", __FUNCTION__, NFAPI_SFNSF2DEC(req->sfn_sf), pnf_p7->_public.codec_config.deallocate ? "DEALLOCATE" : "FREE", req, req->hi_dci0_request_body.hi_dci0_pdu_list);
	// if(pnf_p7->_public.codec_config.deallocate)
	// {
	// 	(pnf_p7->_public.codec_config.deallocate)(req->ul_dci_pdu_list);
	// }
	// else
	// {
	// 	free(req->ul_dci_pdu_list);
	// }

	pnf_p7_free(pnf_p7, req);
}

void deallocate_nfapi_hi_dci0_request(nfapi_hi_dci0_request_t* req, pnf_p7_t* pnf_p7) 
{ 
  //printf("%s() SFN/SF:%d %s req:%p pdu_list:%p\n", __FUNCTION__, NFAPI_SFNSF2DEC(req->sfn_sf), pnf_p7->_public.codec_config.deallocate ? "DEALLOCATE" : "FREE", req, req->hi_dci0_request_body.hi_dci0_pdu_list);
	if(pnf_p7->_public.codec_config.deallocate)
	{
		(pnf_p7->_public.codec_config.deallocate)(req->hi_dci0_request_body.hi_dci0_pdu_list);
	}
	else
	{
		free(req->hi_dci0_request_body.hi_dci0_pdu_list);
	}
        req->hi_dci0_request_body.hi_dci0_pdu_list=0;

	pnf_p7_free(pnf_p7, req);
}

nfapi_tx_request_t* allocate_nfapi_tx_request(pnf_p7_t* pnf_p7) 
{ 
	return pnf_p7_malloc(pnf_p7, sizeof(nfapi_tx_request_t));
}

void deallocate_nfapi_tx_request(nfapi_tx_request_t* req, pnf_p7_t* pnf_p7) 
{ 
	int i = 0;

  //printf("%s() SFN/SF:%d %s req:%p pdu[0]:data:%p\n", __FUNCTION__, NFAPI_SFNSF2DEC(req->sfn_sf), pnf_p7->_public.codec_config.deallocate ? "DEALLOCATE" : "FREE", req, req->tx_request_body.tx_pdu_list[i].segments[0].segment_data);

	for(i = 0; i < req->tx_request_body.number_of_pdus; ++i)
	{
		void* data = req->tx_request_body.tx_pdu_list[i].segments[0].segment_data;

		if(pnf_p7->_public.codec_config.deallocate)
		{
			(pnf_p7->_public.codec_config.deallocate)(data);
		}
		else
		{
			free(data);
		}
                data=0;
	}


	if(pnf_p7->_public.codec_config.deallocate)
	{
		(pnf_p7->_public.codec_config.deallocate)(req->tx_request_body.tx_pdu_list);
	}
	else
	{
		free(req->tx_request_body.tx_pdu_list);
	}
        req->tx_request_body.tx_pdu_list=0;

	pnf_p7_free(pnf_p7, req);
}

nfapi_lbt_dl_config_request_t* allocate_nfapi_lbt_dl_config_request(pnf_p7_t* pnf_p7) 
{ 
	return pnf_p7_malloc(pnf_p7, sizeof(nfapi_lbt_dl_config_request_t));
}

void deallocate_nfapi_lbt_dl_config_request(nfapi_lbt_dl_config_request_t* req, pnf_p7_t* pnf_p7) 
{ 
	if(pnf_p7->_public.codec_config.deallocate)
	{
		(pnf_p7->_public.codec_config.deallocate)(req->lbt_dl_config_request_body.lbt_dl_config_req_pdu_list);
	}
	else
	{
		free(req->lbt_dl_config_request_body.lbt_dl_config_req_pdu_list);
	}
        req->lbt_dl_config_request_body.lbt_dl_config_req_pdu_list=0;

	pnf_p7_free(pnf_p7, req);
}

nfapi_ue_release_request_t* allocate_nfapi_ue_release_request(pnf_p7_t* pnf_p7)
{
    return pnf_p7_malloc(pnf_p7, sizeof(nfapi_ue_release_request_t));
}

void deallocate_nfapi_ue_release_request(nfapi_ue_release_request_t* req, pnf_p7_t* pnf_p7)
{
	pnf_p7_free(pnf_p7, req);
}

pnf_p7_rx_message_t* pnf_p7_rx_reassembly_queue_add_segment(pnf_p7_t* pnf_p7, pnf_p7_rx_reassembly_queue_t* queue, uint32_t rx_hr_time, uint16_t sequence_number, uint16_t segment_number, uint8_t m, uint8_t* data, uint16_t data_len)
{
	pnf_p7_rx_message_t* msg = 0;
	// attempt to find a entry for this segment
	pnf_p7_rx_message_t* iterator = queue->msg_queue;
	while(iterator != 0)
	{
		if(iterator->sequence_number == sequence_number)
		{
			msg = iterator;
			break;
		}

		iterator = iterator->next;
	}
	
	// if found then copy data to message
	if(msg != 0)
	{
	
		msg->segments[segment_number].buffer = (uint8_t*)pnf_p7_malloc(pnf_p7, data_len);
		memcpy(msg->segments[segment_number].buffer, data, data_len);
		msg->segments[segment_number].length = data_len;

		msg->num_segments_received++;

		// set the segement number if we have the last segment
		if(m == 0)
			msg->num_segments_expected = segment_number + 1;
	}
	// else add new rx message entry
	else
	{
		// create a new message
		msg = (pnf_p7_rx_message_t*)(pnf_p7_malloc(pnf_p7, sizeof(pnf_p7_rx_message_t)));
		memset(msg, 0, sizeof(pnf_p7_rx_message_t));

		msg->sequence_number = sequence_number;
		msg->num_segments_expected = m ? 255 : segment_number + 1;
		msg->num_segments_received = 1;
		msg->rx_hr_time = rx_hr_time;

		msg->segments[segment_number].buffer = (uint8_t*)pnf_p7_malloc(pnf_p7, data_len);
		memcpy(msg->segments[segment_number].buffer, data, data_len);
		msg->segments[segment_number].length = data_len;

		// place the message at the head of the queue
		msg->next = queue->msg_queue;
		queue->msg_queue = msg;
	}

	return msg;
}

void pnf_p7_rx_reassembly_queue_remove_msg(pnf_p7_t* pnf_p7, pnf_p7_rx_reassembly_queue_t* queue, pnf_p7_rx_message_t* msg)
{
	// remove message if it has the same sequence number
	pnf_p7_rx_message_t* iterator = queue->msg_queue;
	pnf_p7_rx_message_t* previous = 0;

	while(iterator != 0)
	{
		if(iterator->sequence_number == msg->sequence_number)
		{
			if(previous == 0)
			{
				queue->msg_queue = iterator->next;
			}
			else
			{
				previous->next = iterator->next;
			}

			//NFAPI_TRACE(NFAPI_TRACE_INFO, "Deleting reassembly message\n");
			// delete the message
			uint16_t i;
			for(i = 0; i < 128; ++i)
			{
				if(iterator->segments[i].buffer)
					pnf_p7_free(pnf_p7, iterator->segments[i].buffer);
			}
			pnf_p7_free(pnf_p7, iterator);

			break;
		}

		previous = iterator;
		iterator = iterator->next;
	}
}

void pnf_p7_rx_reassembly_queue_remove_old_msgs(pnf_p7_t* pnf_p7, pnf_p7_rx_reassembly_queue_t* queue, uint32_t rx_hr_time, uint32_t delta)
{
	// remove all messages that are too old
	pnf_p7_rx_message_t* iterator = queue->msg_queue;
	pnf_p7_rx_message_t* previous = 0;

	while(iterator != 0)
	{
		if(rx_hr_time - iterator->rx_hr_time > delta)
		{
			if(previous == 0)
			{
				queue->msg_queue = iterator->next;
			}
			else
			{
				previous->next = iterator->next;
			}
			
			NFAPI_TRACE(NFAPI_TRACE_INFO, "Deleting stale reassembly message (%u %u %d)\n", iterator->rx_hr_time, rx_hr_time, delta);

			pnf_p7_rx_message_t* to_delete = iterator;
			iterator = iterator->next;

			// delete the message
			uint16_t i;
			for(i = 0; i < 128; ++i)
			{
				if(to_delete->segments[i].buffer)
					pnf_p7_free(pnf_p7, to_delete->segments[i].buffer);
			}
			pnf_p7_free(pnf_p7, to_delete);

		}
		else
		{
			previous = iterator;
			iterator = iterator->next;
		}
	}
}


static uint32_t get_slot_time(uint32_t now_hr, uint32_t slot_start_hr)
{
	if(now_hr < slot_start_hr)
	{
		//NFAPI_TRACE(NFAPI_TRACE_INFO, "now is earlier than start of subframe now_hr:%u sf_start_hr:%u\n", now_hr, sf_start_hr);
		return 0;
	}
	else
	{
		uint32_t now_us = TIMEHR_USEC(now_hr);
		uint32_t slot_start_us = TIMEHR_USEC(slot_start_hr);

		// if the us have wrapped adjust for it
		if(now_hr < slot_start_us)
		{
			now_us += 500000; 
		}

		return now_us - slot_start_us;
	}
}

static uint32_t get_sf_time(uint32_t now_hr, uint32_t sf_start_hr)
{
	if(now_hr < sf_start_hr)
	{
		NFAPI_TRACE(NFAPI_TRACE_INFO, "now is earlier than start of subframe\n");
		return 0;
	}
	else
	{
		uint32_t now_us = TIMEHR_USEC(now_hr);
		uint32_t sf_start_us = TIMEHR_USEC(sf_start_hr);

		// if the us have wrapped adjust for it
		if(now_hr < sf_start_us)
		{
			now_us += 1000000;
		}

		return now_us - sf_start_us;
	}
}



int pnf_p7_send_message(pnf_p7_t* pnf_p7, uint8_t* msg, uint32_t len)
{
	// todo : consider how to do this only once
	struct sockaddr_in remote_addr;
	memset((char*)&remote_addr, 0, sizeof(struct sockaddr_in));
	remote_addr.sin_family = AF_INET;
	remote_addr.sin_port = htons(pnf_p7->_public.remote_p7_port);

	
	if(inet_aton(pnf_p7->_public.remote_p7_addr, &remote_addr.sin_addr) == -1)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "inet_aton failed %d\n", errno);
		return -1;
	}
	
	socklen_t remote_addr_len = sizeof(struct sockaddr_in);
	
	int sendto_result;
	
	if ((sendto_result = sendto((int)pnf_p7->p7_sock, (const char*)msg, len, 0, (const struct sockaddr*)&remote_addr, remote_addr_len)) < 0)
//if ((sendto_result = sendto((int)pnf_p7->p7_sock,"hello", 6, 0, (const struct sockaddr*)&remote_addr, remote_addr_len)) < 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s %s:%d sendto(%d, %p, %d) %d failed errno: %d\n", __FUNCTION__, pnf_p7->_public.remote_p7_addr, pnf_p7->_public.remote_p7_port, (int)pnf_p7->p7_sock, (const char*)msg, len, remote_addr_len,  errno);
		return -1;
	}

	if(sendto_result != len)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s sendto failed to send the entire message %d %d\n", __FUNCTION__, sendto_result, len);
	}
	return 0;
}

int pnf_p7_pack_and_send_p7_message(pnf_p7_t* pnf_p7, nfapi_p7_message_header_t* header, uint32_t msg_len)
{
	header->m_segment_sequence = NFAPI_P7_SET_MSS(0, 0, pnf_p7->sequence_number);

	// Need to guard against different threads calling the encode function at the same time
	if(pthread_mutex_lock(&(pnf_p7->pack_mutex)) != 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to lock mutex\n");
		return -1;
	}

	int len = nfapi_p7_message_pack(header, pnf_p7->tx_message_buffer, sizeof(pnf_p7->tx_message_buffer), &pnf_p7->_public.codec_config);

	if (len < 0)
	{
		if(pthread_mutex_unlock(&(pnf_p7->pack_mutex)) != 0)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to unlock mutex\n");
			return -1;
		}
		
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "nfapi_p7_message_pack failed with return %d\n", len );
		return -1;
	}

	if(len > pnf_p7->_public.segment_size)
	{
		int msg_body_len = len - NFAPI_P7_HEADER_LENGTH ; 
		int seg_body_len = pnf_p7->_public.segment_size - NFAPI_P7_HEADER_LENGTH ; 
		int segment_count = (msg_body_len / (seg_body_len)) + ((msg_body_len % seg_body_len) ? 1 : 0); 

		int segment = 0;
		int offset = NFAPI_P7_HEADER_LENGTH;
		uint8_t buffer[pnf_p7->_public.segment_size];
		for(segment = 0; segment < segment_count; ++segment)
		{
			uint8_t last = 0;
			uint16_t size = pnf_p7->_public.segment_size - NFAPI_P7_HEADER_LENGTH;
			if(segment + 1 == segment_count)
			{
				last = 1;
				size = (msg_body_len) - (seg_body_len * segment);
			}

			uint16_t segment_size = size + NFAPI_P7_HEADER_LENGTH;

			// Update the header with the m and segement 
			memcpy(&buffer[0], pnf_p7->tx_message_buffer, NFAPI_P7_HEADER_LENGTH);

			// set the segment length
			buffer[4] = (segment_size & 0xFF00) >> 8;
			buffer[5] = (segment_size & 0xFF);

			// set the m & segment number
			buffer[6] = ((!last) << 7) + segment;

			memcpy(&buffer[NFAPI_P7_HEADER_LENGTH], pnf_p7->tx_message_buffer + offset, size);
			offset += size;

			if(pnf_p7->_public.checksum_enabled)
			{
				nfapi_p7_update_checksum(buffer, segment_size);
			}


			pnf_p7_send_message(pnf_p7, &buffer[0], segment_size);
		}
	}
	else
	{
		if(pnf_p7->_public.checksum_enabled)
		{
			nfapi_p7_update_checksum(pnf_p7->tx_message_buffer, len);
		}

		// simple case that the message fits in a single segment
		pnf_p7_send_message(pnf_p7, pnf_p7->tx_message_buffer, len);
	}

	pnf_p7->sequence_number++;
	
	if(pthread_mutex_unlock(&(pnf_p7->pack_mutex)) != 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to unlock mutex\n");
		return -1;
	}

	return 0;
}


int pnf_nr_p7_pack_and_send_p7_message(pnf_p7_t* pnf_p7, nfapi_p7_message_header_t* header, uint32_t msg_len)
{
	header->m_segment_sequence = NFAPI_P7_SET_MSS(0, 0, pnf_p7->sequence_number);

	// Need to guard against different threads calling the encode function at the same time
	if(pthread_mutex_lock(&(pnf_p7->pack_mutex)) != 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to lock mutex\n");
		return -1;
	}

	int len = nfapi_nr_p7_message_pack(header, pnf_p7->tx_message_buffer, sizeof(pnf_p7->tx_message_buffer), &pnf_p7->_public.codec_config);

	if (len < 0)
	{
		if(pthread_mutex_unlock(&(pnf_p7->pack_mutex)) != 0)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to unlock mutex\n");
			return -1;
		}
		
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "nfapi_p7_message_pack failed with return %d\n", len );
		return -1;
	}

	if(len > pnf_p7->_public.segment_size)
	{
		int msg_body_len = len - NFAPI_P7_HEADER_LENGTH ; 
		int seg_body_len = pnf_p7->_public.segment_size - NFAPI_P7_HEADER_LENGTH ; 
		int segment_count = (msg_body_len / (seg_body_len)) + ((msg_body_len % seg_body_len) ? 1 : 0); 

		int segment = 0;
		int offset = NFAPI_P7_HEADER_LENGTH;
		uint8_t buffer[pnf_p7->_public.segment_size];
		for(segment = 0; segment < segment_count; ++segment)
		{
			uint8_t last = 0;
			uint16_t size = pnf_p7->_public.segment_size - NFAPI_P7_HEADER_LENGTH;
			if(segment + 1 == segment_count)
			{
				last = 1;
				size = (msg_body_len) - (seg_body_len * segment);
			}

			uint16_t segment_size = size + NFAPI_P7_HEADER_LENGTH;

			// Update the header with the m and segement 
			memcpy(&buffer[0], pnf_p7->tx_message_buffer, NFAPI_P7_HEADER_LENGTH);

			// set the segment length
			buffer[4] = (segment_size & 0xFF00) >> 8;
			buffer[5] = (segment_size & 0xFF);

			// set the m & segment number
			buffer[6] = ((!last) << 7) + segment;

			memcpy(&buffer[NFAPI_P7_HEADER_LENGTH], pnf_p7->tx_message_buffer + offset, size);
			offset += size;

			if(pnf_p7->_public.checksum_enabled)
			{
				nfapi_p7_update_checksum(buffer, segment_size);
			}


			pnf_p7_send_message(pnf_p7, &buffer[0], segment_size);
		}
	}
	else
	{
		if(pnf_p7->_public.checksum_enabled)
		{
			nfapi_p7_update_checksum(pnf_p7->tx_message_buffer, len);
		}

		// simple case that the message fits in a single segment
		pnf_p7_send_message(pnf_p7, pnf_p7->tx_message_buffer, len);
	}

	pnf_p7->sequence_number++;
	
	if(pthread_mutex_unlock(&(pnf_p7->pack_mutex)) != 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to unlock mutex\n");
		return -1;
	}

	return 0;
}

void pnf_pack_and_send_timing_info(pnf_p7_t* pnf_p7)
{
	nfapi_timing_info_t timing_info;
	memset(&timing_info, 0, sizeof(timing_info));
	timing_info.header.message_id = NFAPI_TIMING_INFO;
	timing_info.header.phy_id = pnf_p7->_public.phy_id;

	timing_info.last_sfn_sf = pnf_p7->sfn_sf;
	timing_info.time_since_last_timing_info = pnf_p7->timing_info_ms_counter;

	timing_info.dl_config_jitter = pnf_p7->dl_config_jitter;
	timing_info.tx_request_jitter = pnf_p7->tx_jitter;
	timing_info.ul_config_jitter = pnf_p7->ul_config_jitter;
	timing_info.hi_dci0_jitter = pnf_p7->hi_dci0_jitter;

	timing_info.dl_config_latest_delay = 0;
	timing_info.tx_request_latest_delay = 0;
	timing_info.ul_config_latest_delay = 0;
	timing_info.hi_dci0_latest_delay = 0;

	timing_info.dl_config_earliest_arrival = 0;
	timing_info.tx_request_earliest_arrival = 0;
	timing_info.ul_config_earliest_arrival = 0;
	timing_info.hi_dci0_earliest_arrival = 0;


	pnf_p7_pack_and_send_p7_message(pnf_p7, &(timing_info.header), sizeof(timing_info));

	pnf_p7->timing_info_ms_counter = 0;
}


void pnf_nr_pack_and_send_timing_info(pnf_p7_t* pnf_p7)
{
	nfapi_nr_timing_info_t timing_info;
	memset(&timing_info, 0, sizeof(timing_info));
	timing_info.header.message_id = NFAPI_TIMING_INFO;
	timing_info.header.phy_id = pnf_p7->_public.phy_id;

	timing_info.last_sfn = pnf_p7->sfn;
	timing_info.last_slot = pnf_p7->slot;
	timing_info.time_since_last_timing_info = pnf_p7->timing_info_ms_counter;

	timing_info.dl_tti_jitter = pnf_p7->dl_tti_jitter;
	timing_info.tx_data_request_jitter = pnf_p7->tx_data_jitter;
	timing_info.ul_tti_jitter = pnf_p7->ul_tti_jitter;
	timing_info.ul_dci_jitter = pnf_p7->ul_dci_jitter;

	timing_info.dl_tti_latest_delay = 0;
	timing_info.tx_data_request_latest_delay = 0;
	timing_info.ul_tti_latest_delay = 0;
	timing_info.ul_dci_latest_delay = 0;

	timing_info.dl_tti_earliest_arrival = 0;
	timing_info.tx_data_request_earliest_arrival = 0;
	timing_info.ul_tti_earliest_arrival = 0;
	timing_info.ul_dci_earliest_arrival = 0;


	pnf_nr_p7_pack_and_send_p7_message(pnf_p7, &(timing_info.header), sizeof(timing_info));

	pnf_p7->timing_info_ms_counter = 0;
}

void send_dummy_subframe(pnf_p7_t* pnf_p7, uint16_t sfn_sf)
{
  struct timespec t;
  clock_gettime( CLOCK_MONOTONIC, &t);

  //NFAPI_TRACE(NFAPI_TRACE_INFO, "%s(sfn_sf:%d) t:%ld.%09ld\n", __FUNCTION__, NFAPI_SFNSF2DEC(sfn_sf), t.tv_sec, t.tv_nsec);

	if(pnf_p7->_public.tx_req && pnf_p7->_public.dummy_subframe.tx_req)
	{
		pnf_p7->_public.dummy_subframe.tx_req->sfn_sf = sfn_sf;
		//NFAPI_TRACE(NFAPI_TRACE_INFO, "Dummy tx_req - enter\n");
		(pnf_p7->_public.tx_req)(&pnf_p7->_public, pnf_p7->_public.dummy_subframe.tx_req);
		//NFAPI_TRACE(NFAPI_TRACE_INFO, "Dummy tx_req - exit\n");
	}
	if(pnf_p7->_public.dl_config_req && pnf_p7->_public.dummy_subframe.dl_config_req)
	{
		pnf_p7->_public.dummy_subframe.dl_config_req->sfn_sf = sfn_sf;
		//NFAPI_TRACE(NFAPI_TRACE_INFO, "Dummy dl_config_req - enter\n");
		(pnf_p7->_public.dl_config_req)(NULL, &(pnf_p7->_public), pnf_p7->_public.dummy_subframe.dl_config_req);
		//NFAPI_TRACE(NFAPI_TRACE_INFO, "Dummy dl_config_req - exit\n");
	}
	if(pnf_p7->_public.ul_config_req && pnf_p7->_public.dummy_subframe.ul_config_req)
	{
		pnf_p7->_public.dummy_subframe.ul_config_req->sfn_sf = sfn_sf;
		NFAPI_TRACE(NFAPI_TRACE_INFO, "Dummy ul_config_req - enter\n");
		(pnf_p7->_public.ul_config_req)(NULL, &pnf_p7->_public, pnf_p7->_public.dummy_subframe.ul_config_req);
	}
	if(pnf_p7->_public.hi_dci0_req && pnf_p7->_public.dummy_subframe.hi_dci0_req)
	{
		pnf_p7->_public.dummy_subframe.hi_dci0_req->sfn_sf = sfn_sf;
		NFAPI_TRACE(NFAPI_TRACE_INFO, "Dummy hi_dci0 - enter\n");
		(pnf_p7->_public.hi_dci0_req)(NULL, &pnf_p7->_public, pnf_p7->_public.dummy_subframe.hi_dci0_req);
	}
	if(pnf_p7->_public.lbt_dl_config_req && pnf_p7->_public.dummy_subframe.lbt_dl_config_req)
	{
		pnf_p7->_public.dummy_subframe.lbt_dl_config_req->sfn_sf = sfn_sf;
		NFAPI_TRACE(NFAPI_TRACE_INFO, "Dummy lbt - enter\n");
		(pnf_p7->_public.lbt_dl_config_req)(&pnf_p7->_public, pnf_p7->_public.dummy_subframe.lbt_dl_config_req);
	}
}


int pnf_p7_slot_ind(pnf_p7_t* pnf_p7, uint16_t phy_id, uint16_t sfn, uint16_t slot)
{	
	//This function is aligned with rx sfn/slot

	// We could either send an event to the p7 thread have have it run the
	// subframe or we could handle it here and lock access to the subframe
	// buffers. If we do it on the p7 thread then we run the risk of blocking
	// on the udp send. 
	
	// todo : consider a more efficent lock mechasium
	//uint16_t NUM_SLOTS = 20;//10* 2^mu
	
	if(pthread_mutex_lock(&(pnf_p7->mutex)) != 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to lock mutex\n");
		return -1;
	}

	// save the curren time, sfn and slot
	pnf_p7->slot_start_time_hr = pnf_get_current_time_hr();
	int slot_ahead = 0;
	uint32_t sfn_slot_tx = sfnslot_add_slot(sfn, slot, slot_ahead);
	uint16_t sfn_tx = NFAPI_SFNSLOT2SFN(sfn_slot_tx);
	uint8_t slot_tx = NFAPI_SFNSLOT2SLOT(sfn_slot_tx);

	//We align the pnf_p7 sfn/slot with tx sfn/slot, and vnf is synced with pnf_p7 sfn/slot. This is so that the scheduler runs slot_ahead from rx thread.

	pnf_p7->sfn = sfn_tx;
	pnf_p7->slot = slot_tx; 

	uint32_t rx_slot_dec = NFAPI_SFNSLOT2DEC(sfn, slot);
	uint8_t buffer_index_rx = rx_slot_dec % 20; 

	uint32_t tx_slot_dec = NFAPI_SFNSLOT2DEC(sfn_tx,slot_tx);
	uint8_t buffer_index_tx = tx_slot_dec % 20;

	// If the subframe_buffer has been configured
	if(pnf_p7->_public.slot_buffer_size!= 0) // for now value is same as sf_buffer_size
	{

		// apply the shift to the incoming sfn_sf
		if(pnf_p7->slot_shift != 0) // see in vnf_build_send_dl_node_sync
		{
			uint16_t shifted_slot = slot + pnf_p7->slot_shift; 

			// adjust for wrap-around
			if(shifted_slot < 0)
				shifted_slot += NFAPI_MAX_SFNSLOTDEC;
			else if(shifted_slot > NFAPI_MAX_SFNSLOTDEC)
				shifted_slot -= NFAPI_MAX_SFNSLOTDEC;

	//		NFAPI_TRACE(NFAPI_TRACE_INFO, "Applying shift %d to sfn/slot (%d -> %d)\n", pnf_p7->sfn_slot_shift, NFAPI_SFNSF2DEC(sfn_slot), shifted_sfn_slot);
			slot = shifted_slot;

			//
			// why does the shift not apply to pnf_p7->sfn_sf???
			//

			pnf_p7->slot_shift = 0;
		}

		nfapi_pnf_p7_slot_buffer_t* rx_slot_buffer = &(pnf_p7->slot_buffer[buffer_index_rx]);

		nfapi_pnf_p7_slot_buffer_t* tx_slot_buffer = &(pnf_p7->slot_buffer[buffer_index_tx]);

		if(tx_slot_buffer->dl_tti_req.dl_tti_request_body.nPDUs > 0 && tx_slot_buffer->dl_tti_req.SFN == sfn_tx && tx_slot_buffer->dl_tti_req.Slot == slot_tx)
		{
			DevAssert(pnf_p7->_public.dl_tti_req_fn != NULL);
			// pnf_phy_dl_tti_req()
			(pnf_p7->_public.dl_tti_req_fn)(NULL, &(pnf_p7->_public), &tx_slot_buffer->dl_tti_req);
		}

		if(tx_slot_buffer->ul_tti_req.n_pdus > 0 && tx_slot_buffer->ul_tti_req.SFN == sfn_tx && tx_slot_buffer->ul_tti_req.Slot == slot_tx)
		{
			DevAssert(pnf_p7->_public.ul_tti_req_fn != NULL);
			// pnf_phy_ul_tti_req()
			(pnf_p7->_public.ul_tti_req_fn)(NULL, &(pnf_p7->_public), &tx_slot_buffer->ul_tti_req);
		}

		if(tx_slot_buffer->tx_data_req.SFN == sfn_tx && tx_slot_buffer->tx_data_req.Slot == slot_tx)
		{
				
			DevAssert(pnf_p7->_public.tx_data_req_fn != NULL);
			LOG_D(PHY, "Process tx_data SFN/slot %d.%d buffer index: %d \n",sfn_tx,slot_tx,buffer_index_tx);	
			// pnf_phy_tx_data_req()
			(pnf_p7->_public.tx_data_req_fn)(&(pnf_p7->_public), &tx_slot_buffer->tx_data_req);
		}

		if(tx_slot_buffer->ul_dci_req.numPdus > 0 && tx_slot_buffer->ul_dci_req.SFN == sfn_tx && tx_slot_buffer->ul_dci_req.Slot == slot_tx)
		{
			DevAssert(pnf_p7->_public.ul_dci_req_fn != NULL);
			LOG_D(PHY, "Process ul_dci SFN/slot %d.%d buffer index: %d \n",sfn_tx,slot_tx,buffer_index_tx);
			// pnf_phy_ul_dci_req()
     		(pnf_p7->_public.ul_dci_req_fn)(NULL, &(pnf_p7->_public), &tx_slot_buffer->ul_dci_req);
		}

		//reset slot buffer 

		if (rx_slot_buffer->ul_tti_req.n_pdus == 0)
		{
			pnf_p7->slot_buffer[buffer_index_rx].sfn = -1;
			pnf_p7->slot_buffer[buffer_index_rx].slot = -1;
		}

		//send the periodic timing info if configured
		if(pnf_p7->_public.timing_info_mode_periodic && (pnf_p7->timing_info_period_counter++) == pnf_p7->_public.timing_info_period)
		{
			pnf_nr_pack_and_send_timing_info(pnf_p7);

			pnf_p7->timing_info_period_counter = 0;
		}
		else if(pnf_p7->_public.timing_info_mode_aperiodic && pnf_p7->timing_info_aperiodic_send)
		{
			pnf_nr_pack_and_send_timing_info(pnf_p7);

			pnf_p7->timing_info_aperiodic_send = 0;
		}
		else
		{
			pnf_p7->timing_info_ms_counter++;
		}

	}

	if(pthread_mutex_unlock(&(pnf_p7->mutex)) != 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to unlock mutex\n");
		return -1;
	}

	return 0;
}



int pnf_p7_subframe_ind(pnf_p7_t* pnf_p7, uint16_t phy_id, uint16_t sfn_sf)
{
	// We could either send an event to the p7 thread have have it run the
	// subframe or we could handle it here and lock access to the subframe
	// buffers. If we do it on the p7 thread then we run the risk of blocking
	// on the udp send. 
	//
	// todo : start a timer to give us more of the 1 ms tick before send back
	// the frame
	
	// todo : consider a more efficent lock mechasium
	if(pthread_mutex_lock(&(pnf_p7->mutex)) != 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to lock mutex\n");
		return -1;
	}

#if 1
	// save the curren time and sfn_sf
	pnf_p7->sf_start_time_hr = pnf_get_current_time_hr();
	pnf_p7->sfn_sf = sfn_sf;

	uint32_t sfn_sf_tx = sfnsf_add_sf(sfn_sf, sf_ahead);
	uint32_t tx_sfn_sf_dec = NFAPI_SFNSF2DEC(sfn_sf_tx);

	// If the subframe_buffer has been configured
	if(pnf_p7->_public.subframe_buffer_size != 0)
	{

		// apply the shift to the incoming sfn_sf
		if(pnf_p7->sfn_sf_shift != 0)
		{
			int32_t sfn_sf_dec = NFAPI_SFNSF2DEC(sfn_sf);

			int32_t shifted_sfn_sf = sfn_sf_dec += pnf_p7->sfn_sf_shift;

			// adjust for wrap-around
			if(shifted_sfn_sf < 0)
				shifted_sfn_sf += NFAPI_MAX_SFNSFDEC;
			else if(shifted_sfn_sf > NFAPI_MAX_SFNSFDEC)
				shifted_sfn_sf -= NFAPI_MAX_SFNSFDEC;

			NFAPI_TRACE(NFAPI_TRACE_INFO, "Applying shift %d to sfn/sf (%d -> %d)\n", pnf_p7->sfn_sf_shift, NFAPI_SFNSF2DEC(sfn_sf), shifted_sfn_sf);
			sfn_sf = shifted_sfn_sf;

                        //
                        // why does the shift not apply to pnf_p7->sfn_sf???
                        //

			pnf_p7->sfn_sf_shift = 0;
		}

		uint32_t sfn_sf_dec = NFAPI_SFNSF2DEC(sfn_sf);
		uint8_t buffer_index = sfn_sf_dec % pnf_p7->_public.subframe_buffer_size;

		nfapi_pnf_p7_subframe_buffer_t* subframe_buffer = &(pnf_p7->subframe_buffer[buffer_index]);

		uint8_t tx_buffer_index = tx_sfn_sf_dec % pnf_p7->_public.subframe_buffer_size;
		nfapi_pnf_p7_subframe_buffer_t* tx_subframe_buffer = &(pnf_p7->subframe_buffer[tx_buffer_index]);

                //printf("sfn_sf_dec:%d tx_sfn_sf_dec:%d\n", sfn_sf_dec, tx_sfn_sf_dec);

                if (0) NFAPI_TRACE(NFAPI_TRACE_INFO, "%s() shift:%d subframe_buffer->sfn_sf:%d tx_subframe_buffer->sfn_sf:%d sfn_sf:%d subframe_buffer[buffer_index:%u dl_config_req:%p tx_req:%p] "
                    "TX:sfn_sf:%d:tx_buffer_index:%d[dl_config_req:%p tx_req:%p]\n", 
                    __FUNCTION__, 
                    pnf_p7->sfn_sf_shift, 
                    NFAPI_SFNSF2DEC(subframe_buffer->sfn_sf), 
                    NFAPI_SFNSF2DEC(tx_subframe_buffer->sfn_sf), 
                       sfn_sf_dec,    buffer_index,    subframe_buffer->dl_config_req,    subframe_buffer->tx_req, 
                    tx_sfn_sf_dec, tx_buffer_index, tx_subframe_buffer->dl_config_req, tx_subframe_buffer->tx_req);

		// if the subframe buffer sfn sf is set then we have atlease 1 message
		// from the vnf. 
		// todo : how to handle the messages we don't have, send dummies for
		// now

                //printf("tx_subframe_buffer->sfn_sf:%d sfn_sf_tx:%d\n", tx_subframe_buffer->sfn_sf, sfn_sf_tx);
                //printf("subframe_buffer->sfn_sf:%d sfn_sf:%d\n", subframe_buffer->sfn_sf, sfn_sf);
		if(tx_subframe_buffer->sfn_sf == sfn_sf_tx)
		{
			if(tx_subframe_buffer->tx_req != 0)
			{
				if(pnf_p7->_public.tx_req)
					(pnf_p7->_public.tx_req)(&(pnf_p7->_public), tx_subframe_buffer->tx_req);

				//deallocate_nfapi_tx_request(subframe_buffer->tx_req, pnf_p7);
			}
			else
			{
				// send dummy
				if(pnf_p7->_public.tx_req && pnf_p7->_public.dummy_subframe.tx_req)
				{
					pnf_p7->_public.dummy_subframe.tx_req->sfn_sf = sfn_sf_tx;
					(pnf_p7->_public.tx_req)(&(pnf_p7->_public), pnf_p7->_public.dummy_subframe.tx_req);
				}
			}

			if(tx_subframe_buffer->dl_config_req != 0)
			{
				if(pnf_p7->_public.dl_config_req)
					(pnf_p7->_public.dl_config_req)(NULL, &(pnf_p7->_public), tx_subframe_buffer->dl_config_req);

				//deallocate_nfapi_dl_config_request(subframe_buffer->dl_config_req, pnf_p7);
			}
			else
			{
				// send dummy
				if(pnf_p7->_public.dl_config_req && pnf_p7->_public.dummy_subframe.dl_config_req)
				{
					pnf_p7->_public.dummy_subframe.dl_config_req->sfn_sf = sfn_sf_tx;
					(pnf_p7->_public.dl_config_req)(NULL, &(pnf_p7->_public), pnf_p7->_public.dummy_subframe.dl_config_req);
				}
			}

			if(tx_subframe_buffer->hi_dci0_req != 0)
			{
				if(pnf_p7->_public.hi_dci0_req)
					(pnf_p7->_public.hi_dci0_req)(NULL, &(pnf_p7->_public), tx_subframe_buffer->hi_dci0_req);

				//deallocate_nfapi_hi_dci0_request(subframe_buffer->hi_dci0_req, pnf_p7);
			}
			else
			{
				//send dummy
				if(pnf_p7->_public.hi_dci0_req && pnf_p7->_public.dummy_subframe.hi_dci0_req)
				{
					pnf_p7->_public.dummy_subframe.hi_dci0_req->sfn_sf = sfn_sf_tx;
					(pnf_p7->_public.hi_dci0_req)(NULL, &(pnf_p7->_public), pnf_p7->_public.dummy_subframe.hi_dci0_req);
				}
			}

			if(tx_subframe_buffer->ue_release_req != 0)
			{
				if(pnf_p7->_public.ue_release_req)
					(pnf_p7->_public.ue_release_req)(&(pnf_p7->_public), tx_subframe_buffer->ue_release_req);
			}
			else
			{
				//send dummy
				if(pnf_p7->_public.ue_release_req && pnf_p7->_public.dummy_subframe.ue_release_req)
				{
					pnf_p7->_public.dummy_subframe.ue_release_req->sfn_sf = sfn_sf_tx;
					(pnf_p7->_public.ue_release_req)(&(pnf_p7->_public), pnf_p7->_public.dummy_subframe.ue_release_req);
				}
			}

                        if(tx_subframe_buffer->dl_config_req != 0)
                        {
                          deallocate_nfapi_dl_config_request(tx_subframe_buffer->dl_config_req, pnf_p7);
                          tx_subframe_buffer->dl_config_req = 0;
                        }
			if(tx_subframe_buffer->tx_req != 0)
                        {
                          deallocate_nfapi_tx_request(tx_subframe_buffer->tx_req, pnf_p7);
                          tx_subframe_buffer->tx_req = 0;
                        }
                        if(tx_subframe_buffer->hi_dci0_req != 0)
                        {
                          deallocate_nfapi_hi_dci0_request(tx_subframe_buffer->hi_dci0_req, pnf_p7);
                          tx_subframe_buffer->hi_dci0_req = 0;
                        }
                        if(tx_subframe_buffer->ue_release_req != 0){
                            deallocate_nfapi_ue_release_request(tx_subframe_buffer->ue_release_req, pnf_p7);
                            tx_subframe_buffer->ue_release_req = 0;
                        }
                }
		else
		{
                  // If we ever need to "send" a dummy ul_config this won't work!!!
                  send_dummy_subframe(pnf_p7, sfn_sf_tx);
		}

                if(subframe_buffer->sfn_sf == sfn_sf)
		{

			if(subframe_buffer->ul_config_req != 0)
			{
				if(pnf_p7->_public.ul_config_req)
					(pnf_p7->_public.ul_config_req)(NULL, &(pnf_p7->_public), subframe_buffer->ul_config_req);

				//deallocate_nfapi_ul_config_request(subframe_buffer->ul_config_req, pnf_p7);
			}
			else
			{
				// send dummy
				if(pnf_p7->_public.ul_config_req && pnf_p7->_public.dummy_subframe.ul_config_req)
				{
					pnf_p7->_public.dummy_subframe.ul_config_req->sfn_sf = sfn_sf;
					(pnf_p7->_public.ul_config_req)(NULL, &(pnf_p7->_public), pnf_p7->_public.dummy_subframe.ul_config_req);
				}
			}

			if(subframe_buffer->lbt_dl_config_req != 0)
			{
				if(pnf_p7->_public.lbt_dl_config_req)
					(pnf_p7->_public.lbt_dl_config_req)(&(pnf_p7->_public), subframe_buffer->lbt_dl_config_req);

				//deallocate_nfapi_lbt_dl_config_request(subframe_buffer->lbt_dl_config_req, pnf_p7);
			}
			else
			{
				// send dummy
				if(pnf_p7->_public.lbt_dl_config_req && pnf_p7->_public.dummy_subframe.lbt_dl_config_req)
				{
					pnf_p7->_public.dummy_subframe.lbt_dl_config_req->sfn_sf = sfn_sf;
					(pnf_p7->_public.lbt_dl_config_req)(&(pnf_p7->_public), pnf_p7->_public.dummy_subframe.lbt_dl_config_req);
				}

			}

                        //if(subframe_buffer->dl_config_req != 0)
                          //deallocate_nfapi_dl_config_request(subframe_buffer->dl_config_req, pnf_p7);
			//if(subframe_buffer->tx_req != 0)
                          //deallocate_nfapi_tx_request(subframe_buffer->tx_req, pnf_p7);
                        if(subframe_buffer->ul_config_req != 0)
                        {
                          deallocate_nfapi_ul_config_request(subframe_buffer->ul_config_req, pnf_p7);
                          subframe_buffer->ul_config_req = 0;

                        }
                        //if(subframe_buffer->hi_dci0_req != 0)
                          //deallocate_nfapi_hi_dci0_request(subframe_buffer->hi_dci0_req, pnf_p7);
			if(subframe_buffer->lbt_dl_config_req != 0)
                        {
                          deallocate_nfapi_lbt_dl_config_request(subframe_buffer->lbt_dl_config_req, pnf_p7);
                          subframe_buffer->lbt_dl_config_req = 0;
                        }
                } // sfn_sf match

                if (subframe_buffer->dl_config_req == 0 && subframe_buffer->tx_req == 0 && subframe_buffer->ul_config_req == 0 && subframe_buffer->lbt_dl_config_req == 0 && subframe_buffer->ue_release_req == 0)
                {
                  memset(&(pnf_p7->subframe_buffer[buffer_index]), 0, sizeof(nfapi_pnf_p7_subframe_buffer_t));
                  pnf_p7->subframe_buffer[buffer_index].sfn_sf = -1;
		}

                //printf("pnf_p7->_public.timing_info_mode_periodic:%d pnf_p7->timing_info_period_counter:%d pnf_p7->_public.timing_info_period:%d\n", pnf_p7->_public.timing_info_mode_periodic, pnf_p7->timing_info_period_counter, pnf_p7->_public.timing_info_period);
                //printf("pnf_p7->_public.timing_info_mode_aperiodic:%d pnf_p7->timing_info_aperiodic_send:%d\n", pnf_p7->_public.timing_info_mode_aperiodic, pnf_p7->timing_info_aperiodic_send);
                //printf("pnf_p7->timing_info_ms_counter:%d\n", pnf_p7->timing_info_ms_counter);

		// send the periodic timing info if configured
		if(pnf_p7->_public.timing_info_mode_periodic && (pnf_p7->timing_info_period_counter++) == pnf_p7->_public.timing_info_period)
		{
			pnf_pack_and_send_timing_info(pnf_p7);

			pnf_p7->timing_info_period_counter = 0;
		}
		else if(pnf_p7->_public.timing_info_mode_aperiodic && pnf_p7->timing_info_aperiodic_send)
		{
			pnf_pack_and_send_timing_info(pnf_p7);

			pnf_p7->timing_info_aperiodic_send = 0;
		}
		else
		{
			pnf_p7->timing_info_ms_counter++;
		}
	}
	else
	{
		//send_dummy_subframe(pnf_p7, sfn_sf_tx);
	}


        //printf("pnf_p7->tick:%d\n", pnf_p7->tick);
	if(pnf_p7->tick == 1000)
	{

		NFAPI_TRACE(NFAPI_TRACE_INFO, "[PNF P7:%d] (ONTIME/LATE) DL:(%d/%d) UL:(%d/%d) HI:(%d/%d) TX:(%d/%d)\n", pnf_p7->_public.phy_id,
					pnf_p7->stats.dl_conf_ontime, pnf_p7->stats.dl_conf_late, 
					pnf_p7->stats.ul_conf_ontime, pnf_p7->stats.ul_conf_late, 
					pnf_p7->stats.hi_dci0_ontime, pnf_p7->stats.hi_dci0_late, 
					pnf_p7->stats.tx_ontime, pnf_p7->stats.tx_late);
		pnf_p7->tick = 0;
		memset(&pnf_p7->stats, 0, sizeof(pnf_p7->stats));
	}
	pnf_p7->tick++;
#endif

	if(pthread_mutex_unlock(&(pnf_p7->mutex)) != 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to unlock mutex\n");
		return -1;
	}

	return 0;
}

// return 1 if in window
// return 0 if out of window
bool is_nr_p7_request_in_window(uint16_t sfn, uint16_t slot, const char* name, pnf_p7_t* phy)
{
    uint32_t recv = NFAPI_SFNSLOT2DEC(sfn, slot); // unpack sfn/slot
    uint32_t curr = NFAPI_SFNSLOT2DEC(phy->sfn, phy->slot);
    uint8_t timing_window = phy->_public.slot_buffer_size;
    uint32_t diff = abs(curr - recv);
    if (diff > NFAPI_MAX_SFNSLOTDEC / 2)
        diff = NFAPI_MAX_SFNSLOTDEC - diff;
    if (diff > timing_window)
    {
        NFAPI_TRACE(NFAPI_TRACE_NOTE, "[%d] %s is out of window %d (delta:%d) [max:%d]\n", curr, name, recv, diff, timing_window);
        return false;
    }
    return true;
}

uint8_t is_p7_request_in_window(uint16_t sfnsf, const char* name, pnf_p7_t* phy)
{
	uint32_t recv_sfn_sf_dec = NFAPI_SFNSF2DEC(sfnsf);
	uint32_t current_sfn_sf_dec = NFAPI_SFNSF2DEC(phy->sfn_sf);

	uint8_t in_window = 0;
	uint8_t timing_window = phy->_public.subframe_buffer_size;

	if(recv_sfn_sf_dec <= current_sfn_sf_dec)
	{
		// Need to check for wrap in window
		if(((current_sfn_sf_dec + timing_window) % NFAPI_MAX_SFNSFDEC) < current_sfn_sf_dec)
		{
			if(recv_sfn_sf_dec > ((current_sfn_sf_dec + timing_window) % NFAPI_MAX_SFNSFDEC))
			{
				// out of window
				NFAPI_TRACE(NFAPI_TRACE_NOTE, "[%d] %s is late %d (with wrap)\n", current_sfn_sf_dec, name, recv_sfn_sf_dec);
			}
			else
			{
				// ok
				//NFAPI_TRACE(NFAPI_TRACE_NOTE, "[%d] %s is in window %d (with wrap)\n", current_sfn_sf_dec, name, recv_sfn_sf_dec);
				in_window = 1;
			}
		}
		else
		{
			// too late
			NFAPI_TRACE(NFAPI_TRACE_NOTE, "[%d] %s is in late %d (delta:%d)\n", current_sfn_sf_dec, name, recv_sfn_sf_dec, (current_sfn_sf_dec - recv_sfn_sf_dec));
		}

	}
	else
	{
		// Need to check it is in window
		if((recv_sfn_sf_dec - current_sfn_sf_dec) <= timing_window)
		{
			// in window
			//NFAPI_TRACE(NFAPI_TRACE_NOTE, "[%d] %s is in window %d\n", current_sfn_sf_dec, name, recv_sfn_sf_dec);
			in_window = 1;
		}
		else
		{
			// too far in the future
			NFAPI_TRACE(NFAPI_TRACE_NOTE, "[%d] %s is out of window %d (delta:%d) [max:%d]\n", current_sfn_sf_dec, name, recv_sfn_sf_dec,  (recv_sfn_sf_dec - current_sfn_sf_dec), timing_window);
		}

	}

	return in_window;
}


// P7 messages 
static void cp_nr_dl_tti_pdcch_pdu(nfapi_nr_dl_tti_pdcch_pdu_rel15_t *dst_pdcch_pdu, const nfapi_nr_dl_tti_pdcch_pdu_rel15_t *src_pdcch_pdu) {
    if (dst_pdcch_pdu == NULL || src_pdcch_pdu == NULL) {
        return;
    }
	dst_pdcch_pdu->BWPSize = src_pdcch_pdu->BWPSize;
	dst_pdcch_pdu->BWPStart = src_pdcch_pdu->BWPStart;
	dst_pdcch_pdu->SubcarrierSpacing = src_pdcch_pdu->SubcarrierSpacing;
	dst_pdcch_pdu->CyclicPrefix = src_pdcch_pdu->CyclicPrefix;
	dst_pdcch_pdu->StartSymbolIndex = src_pdcch_pdu->StartSymbolIndex;
	dst_pdcch_pdu->DurationSymbols = src_pdcch_pdu->DurationSymbols;
	memcpy(dst_pdcch_pdu->FreqDomainResource, src_pdcch_pdu->FreqDomainResource, sizeof(src_pdcch_pdu->FreqDomainResource));
	dst_pdcch_pdu->CceRegMappingType = src_pdcch_pdu->CceRegMappingType;
	dst_pdcch_pdu->RegBundleSize = src_pdcch_pdu->RegBundleSize;
	dst_pdcch_pdu->InterleaverSize = src_pdcch_pdu->InterleaverSize;
	dst_pdcch_pdu->CoreSetType = src_pdcch_pdu->CoreSetType;
	dst_pdcch_pdu->ShiftIndex = src_pdcch_pdu->ShiftIndex;
	dst_pdcch_pdu->precoderGranularity = src_pdcch_pdu->precoderGranularity;
	dst_pdcch_pdu->numDlDci = src_pdcch_pdu->numDlDci;
	for (int i = 0; i < src_pdcch_pdu->numDlDci; ++i) {
		dst_pdcch_pdu->dci_pdu[i] = src_pdcch_pdu->dci_pdu[i];
	}
}

static void cp_nr_dl_tti_pdsch_pdu(nfapi_nr_dl_tti_pdsch_pdu_rel15_t *dst_pdsch_pdu, const nfapi_nr_dl_tti_pdsch_pdu_rel15_t *src_pdsch_pdu) {
    if (dst_pdsch_pdu == NULL || src_pdsch_pdu == NULL) {
        return;
    }
		dst_pdsch_pdu->pduBitmap = src_pdsch_pdu->pduBitmap;
		dst_pdsch_pdu->rnti = src_pdsch_pdu->rnti;
		dst_pdsch_pdu->pduIndex = src_pdsch_pdu->pduIndex;
		dst_pdsch_pdu->BWPSize = src_pdsch_pdu->BWPSize;
		dst_pdsch_pdu->BWPStart = src_pdsch_pdu->BWPStart;
		dst_pdsch_pdu->SubcarrierSpacing = src_pdsch_pdu->SubcarrierSpacing;
		dst_pdsch_pdu->CyclicPrefix = src_pdsch_pdu->CyclicPrefix;
		dst_pdsch_pdu->NrOfCodewords = src_pdsch_pdu->NrOfCodewords;
		memcpy(dst_pdsch_pdu->targetCodeRate, src_pdsch_pdu->targetCodeRate, sizeof(src_pdsch_pdu->targetCodeRate));
		memcpy(dst_pdsch_pdu->qamModOrder, src_pdsch_pdu->qamModOrder, sizeof(src_pdsch_pdu->qamModOrder));
		memcpy(dst_pdsch_pdu->mcsIndex, src_pdsch_pdu->mcsIndex, sizeof(src_pdsch_pdu->mcsIndex));
		memcpy(dst_pdsch_pdu->mcsTable, src_pdsch_pdu->mcsTable, sizeof(src_pdsch_pdu->mcsTable));
		memcpy(dst_pdsch_pdu->rvIndex, src_pdsch_pdu->rvIndex, sizeof(src_pdsch_pdu->rvIndex));
		memcpy(dst_pdsch_pdu->TBSize, src_pdsch_pdu->TBSize, sizeof(src_pdsch_pdu->TBSize));
		dst_pdsch_pdu->dataScramblingId = src_pdsch_pdu->dataScramblingId;
		dst_pdsch_pdu->nrOfLayers = src_pdsch_pdu->nrOfLayers;
		dst_pdsch_pdu->transmissionScheme = src_pdsch_pdu->transmissionScheme;
		dst_pdsch_pdu->refPoint = src_pdsch_pdu->refPoint;
		dst_pdsch_pdu->dlDmrsSymbPos = src_pdsch_pdu->dlDmrsSymbPos;
		dst_pdsch_pdu->dmrsConfigType = src_pdsch_pdu->dmrsConfigType;
		dst_pdsch_pdu->dlDmrsScramblingId = src_pdsch_pdu->dlDmrsScramblingId;
		dst_pdsch_pdu->SCID = src_pdsch_pdu->SCID;
		dst_pdsch_pdu->numDmrsCdmGrpsNoData = src_pdsch_pdu->numDmrsCdmGrpsNoData;
		dst_pdsch_pdu->dmrsPorts = src_pdsch_pdu->dmrsPorts;
		dst_pdsch_pdu->resourceAlloc = src_pdsch_pdu->resourceAlloc;
		memcpy(dst_pdsch_pdu->rbBitmap, src_pdsch_pdu->rbBitmap, sizeof(src_pdsch_pdu->rbBitmap));
		dst_pdsch_pdu->rbStart = src_pdsch_pdu->rbStart;
		dst_pdsch_pdu->rbSize = src_pdsch_pdu->rbSize;
		dst_pdsch_pdu->VRBtoPRBMapping = src_pdsch_pdu->VRBtoPRBMapping;
		dst_pdsch_pdu->StartSymbolIndex = src_pdsch_pdu->StartSymbolIndex;
		dst_pdsch_pdu->NrOfSymbols = src_pdsch_pdu->NrOfSymbols;
		dst_pdsch_pdu->PTRSPortIndex = src_pdsch_pdu->PTRSPortIndex;
		dst_pdsch_pdu->PTRSTimeDensity = src_pdsch_pdu->PTRSTimeDensity;
		dst_pdsch_pdu->PTRSFreqDensity = src_pdsch_pdu->PTRSFreqDensity;
		dst_pdsch_pdu->PTRSReOffset = src_pdsch_pdu->PTRSReOffset;
		dst_pdsch_pdu->nEpreRatioOfPDSCHToPTRS = src_pdsch_pdu->nEpreRatioOfPDSCHToPTRS;
		dst_pdsch_pdu->precodingAndBeamforming = src_pdsch_pdu->precodingAndBeamforming;
		dst_pdsch_pdu->maintenance_parms_v3 = src_pdsch_pdu->maintenance_parms_v3;
}

static void cp_nr_dl_tti_csi_rs_pdu(nfapi_nr_dl_tti_csi_rs_pdu_rel15_t *dst_csi_rs_pdu, const nfapi_nr_dl_tti_csi_rs_pdu_rel15_t *src_csi_rs_pdu) {
    if (dst_csi_rs_pdu == NULL || src_csi_rs_pdu == NULL) {
        return;
    }
		dst_csi_rs_pdu->bwp_size = src_csi_rs_pdu->bwp_size;
		dst_csi_rs_pdu->bwp_start = src_csi_rs_pdu->bwp_start;
		dst_csi_rs_pdu->subcarrier_spacing = src_csi_rs_pdu->subcarrier_spacing;
		dst_csi_rs_pdu->cyclic_prefix = src_csi_rs_pdu->cyclic_prefix;
		dst_csi_rs_pdu->start_rb = src_csi_rs_pdu->start_rb;
		dst_csi_rs_pdu->nr_of_rbs = src_csi_rs_pdu->nr_of_rbs;
		dst_csi_rs_pdu->csi_type = src_csi_rs_pdu->csi_type;
		dst_csi_rs_pdu->row = src_csi_rs_pdu->row;
		dst_csi_rs_pdu->freq_domain = src_csi_rs_pdu->freq_domain;
		dst_csi_rs_pdu->symb_l0 = src_csi_rs_pdu->symb_l0;
		dst_csi_rs_pdu->symb_l1 = src_csi_rs_pdu->symb_l1;
		dst_csi_rs_pdu->cdm_type = src_csi_rs_pdu->cdm_type;
		dst_csi_rs_pdu->freq_density = src_csi_rs_pdu->freq_density;
		dst_csi_rs_pdu->scramb_id = src_csi_rs_pdu->scramb_id;
		dst_csi_rs_pdu->power_control_offset = src_csi_rs_pdu->power_control_offset;
		dst_csi_rs_pdu->power_control_offset_ss = src_csi_rs_pdu->power_control_offset_ss;
}

static void cp_nr_dl_tti_ssb_pdu(nfapi_nr_dl_tti_ssb_pdu_rel15_t *dst_ssb_pdu, const nfapi_nr_dl_tti_ssb_pdu_rel15_t *src_ssb_pdu) {
    if (dst_ssb_pdu == NULL || src_ssb_pdu == NULL) {
        return;
    }
		dst_ssb_pdu->PhysCellId = src_ssb_pdu->PhysCellId;
		dst_ssb_pdu->BetaPss = src_ssb_pdu->BetaPss;
		dst_ssb_pdu->SsbBlockIndex = src_ssb_pdu->SsbBlockIndex;
		dst_ssb_pdu->SsbSubcarrierOffset = src_ssb_pdu->SsbSubcarrierOffset;
		dst_ssb_pdu->ssbOffsetPointA = src_ssb_pdu->ssbOffsetPointA;
		dst_ssb_pdu->bchPayloadFlag = src_ssb_pdu->bchPayloadFlag;
		dst_ssb_pdu->bchPayload = src_ssb_pdu->bchPayload;
		dst_ssb_pdu->ssbRsrp = src_ssb_pdu->ssbRsrp;
		dst_ssb_pdu->precoding_and_beamforming = src_ssb_pdu->precoding_and_beamforming;
}
static void cp_nr_dl_tti_req(nfapi_nr_dl_tti_request_t *dst, const nfapi_nr_dl_tti_request_t *src)
{
	if (dst == NULL || src == NULL) {
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "cp_nr_dl_tti_req: dst or src is NULL\n");
        return;
	}
    dst->header = src->header;
    dst->SFN = src->SFN;
    dst->Slot = src->Slot;
    // Copy dl_tti_request_body
    dst->dl_tti_request_body.nPDUs = src->dl_tti_request_body.nPDUs;
    dst->dl_tti_request_body.nGroup = src->dl_tti_request_body.nGroup;
	// Iterate over dl_tti_pdu_list
    for (int i = 0; i < dst->dl_tti_request_body.nPDUs; ++i) {
        nfapi_nr_dl_tti_request_pdu_t *dst_pdu = &dst->dl_tti_request_body.dl_tti_pdu_list[i];
        const nfapi_nr_dl_tti_request_pdu_t *src_pdu = &src->dl_tti_request_body.dl_tti_pdu_list[i];
        dst_pdu->PDUType = src_pdu->PDUType;
        dst_pdu->PDUSize = src_pdu->PDUSize;
        // Copy union based on PDUType
        switch (src_pdu->PDUType) {
	        case NFAPI_NR_DL_TTI_PDCCH_PDU_TYPE:
	            cp_nr_dl_tti_pdcch_pdu(&dst_pdu->pdcch_pdu.pdcch_pdu_rel15, &src_pdu->pdcch_pdu.pdcch_pdu_rel15);
	            break;
	        case NFAPI_NR_DL_TTI_PDSCH_PDU_TYPE:
	            cp_nr_dl_tti_pdsch_pdu(&dst_pdu->pdsch_pdu.pdsch_pdu_rel15, &src_pdu->pdsch_pdu.pdsch_pdu_rel15);
	            break;
	        case NFAPI_NR_DL_TTI_CSI_RS_PDU_TYPE:
	            cp_nr_dl_tti_csi_rs_pdu(&dst_pdu->csi_rs_pdu.csi_rs_pdu_rel15, &src_pdu->csi_rs_pdu.csi_rs_pdu_rel15);
	            break;
	        case NFAPI_NR_DL_TTI_SSB_PDU_TYPE:
	            cp_nr_dl_tti_ssb_pdu(&dst_pdu->ssb_pdu.ssb_pdu_rel15, &src_pdu->ssb_pdu.ssb_pdu_rel15);
	            break;
		}
    }
   // Copy the nUe array
    for (int i = 0; i < dst->dl_tti_request_body.nGroup; ++i) {
        dst->dl_tti_request_body.nUe[i] = src->dl_tti_request_body.nUe[i];
    }

    // Copy the PduIdx array
    for (int i = 0; i < dst->dl_tti_request_body.nPDUs; ++i) {
        for (int j = 0; j < dst->dl_tti_request_body.nUe[i]; ++j) {
            dst->dl_tti_request_body.PduIdx[i][j] = src->dl_tti_request_body.PduIdx[i][j];
        }
    }

    // Copy vendor_extension
    dst->vendor_extension = src->vendor_extension;
}


void pnf_handle_dl_tti_request(void* pRecvMsg, int recvMsgLen, pnf_p7_t* pnf_p7)
{
	// NFAPI_TRACE(NFAPI_TRACE_INFO, "DL_CONFIG.req Received\n");
	nfapi_nr_dl_tti_request_t req;
	int unpack_result = nfapi_nr_p7_message_unpack(pRecvMsg, recvMsgLen, &req, sizeof(nfapi_nr_dl_tti_request_t), &(pnf_p7->_public.codec_config));

	if(unpack_result == 0)
	{
		if(pthread_mutex_lock(&(pnf_p7->mutex)) != 0)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to lock mutex\n");
			return;
		}

        if(is_nr_p7_request_in_window(req.SFN,req.Slot, "dl_tti_request", pnf_p7))
		{
			uint32_t sfn_slot_dec = NFAPI_SFNSLOT2DEC(req.SFN,req.Slot);
			uint8_t buffer_index = sfn_slot_dec % 20;

			struct timespec t;
			clock_gettime(CLOCK_MONOTONIC, &t);

			NFAPI_TRACE(NFAPI_TRACE_DEBUG,"%s() %ld.%09ld POPULATE DL_TTI_REQ current tx sfn/slot:%d.%d p7 msg sfn/slot: %d.%d buffer_index:%d\n", __FUNCTION__, t.tv_sec, t.tv_nsec, pnf_p7->sfn,pnf_p7->slot, req.SFN, req.Slot, buffer_index);

			// filling dl_tti_request in slot buffer
			pnf_p7->slot_buffer[buffer_index].sfn = req.SFN;
			pnf_p7->slot_buffer[buffer_index].slot = req.Slot;
			cp_nr_dl_tti_req(&pnf_p7->slot_buffer[buffer_index].dl_tti_req, &req);

			pnf_p7->stats.dl_tti_ontime++;
		}
		else
		{
			if(pnf_p7->_public.timing_info_mode_aperiodic)
				pnf_p7->timing_info_aperiodic_send = 1;

			pnf_p7->stats.dl_tti_late++;
		} 
		if(pthread_mutex_unlock(&(pnf_p7->mutex)) != 0)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to unlock mutex\n");
			return;
		}
	}
	else
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "Failed to unpack dl_tti_req");
	}
}


void pnf_handle_dl_config_request(void* pRecvMsg, int recvMsgLen, pnf_p7_t* pnf_p7)
{
	//NFAPI_TRACE(NFAPI_TRACE_INFO, "DL_CONFIG.req Received\n");

	nfapi_dl_config_request_t* req  = allocate_nfapi_dl_config_request(pnf_p7);

	if(req == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to allocate nfapi_dl_config_request structure\n");
		return;
	}

	int unpack_result = nfapi_p7_message_unpack(pRecvMsg, recvMsgLen, req, sizeof(nfapi_dl_config_request_t), &(pnf_p7->_public.codec_config));

	if(unpack_result == 0)
	{
		if(pthread_mutex_lock(&(pnf_p7->mutex)) != 0)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to lock mutex\n");
			return;
		}

                if (
                    0 && 
                    (NFAPI_SFNSF2DEC(req->sfn_sf) % 100 ==0 ||
                     NFAPI_SFNSF2DEC(req->sfn_sf) % 105 ==0 
                    )
                )
                  NFAPI_TRACE(NFAPI_TRACE_INFO, "DL_CONFIG.req sfn_sf:%d pdcch:%u dci:%u pdu:%u pdsch_rnti:%u pcfich:%u\n", 
                      NFAPI_SFNSF2DEC(req->sfn_sf),
                      req->dl_config_request_body.number_pdcch_ofdm_symbols,
                      req->dl_config_request_body.number_dci,
                      req->dl_config_request_body.number_pdu,
                      req->dl_config_request_body.number_pdsch_rnti,
                      req->dl_config_request_body.transmission_power_pcfich
                      );

                if(is_p7_request_in_window(req->sfn_sf, "dl_config_request", pnf_p7))
                {
                  uint32_t sfn_sf_dec = NFAPI_SFNSF2DEC(req->sfn_sf);
                  uint8_t buffer_index = sfn_sf_dec % pnf_p7->_public.subframe_buffer_size;

                        struct timespec t;
                        clock_gettime(CLOCK_MONOTONIC, &t);

                  NFAPI_TRACE(NFAPI_TRACE_INFO,"%s() %ld.%09ld POPULATE DL_CONFIG_REQ sfn_sf:%d buffer_index:%d\n", __FUNCTION__, t.tv_sec, t.tv_nsec, sfn_sf_dec, buffer_index);

			// if there is already an dl_config_req make sure we free it.
			if(pnf_p7->subframe_buffer[buffer_index].dl_config_req != 0)
			{
				NFAPI_TRACE(NFAPI_TRACE_NOTE, "%s() is_p7_request_in_window()=TRUE buffer_index occupied - free it first sfn_sf:%d buffer_index:%d\n", __FUNCTION__, NFAPI_SFNSF2DEC(req->sfn_sf), buffer_index);
				//NFAPI_TRACE(NFAPI_TRACE_NOTE, "[%d] Freeing dl_config_req at index %d (%d/%d)", 
				//			pMyPhyInfo->sfnSf, bufferIdx,
				//			SFNSF2SFN(dreq->sfn_sf), SFNSF2SF(dreq->sfn_sf));
				deallocate_nfapi_dl_config_request(pnf_p7->subframe_buffer[buffer_index].dl_config_req, pnf_p7);
			}

			// saving dl_config_request in subframe buffer
			pnf_p7->subframe_buffer[buffer_index].sfn_sf = req->sfn_sf;
			pnf_p7->subframe_buffer[buffer_index].dl_config_req = req;

			pnf_p7->stats.dl_conf_ontime++;
			
		}
		else
		{
			//NFAPI_TRACE(NFAPI_TRACE_NOTE, "NOT storing dl_config_req SFN/SF %d\n", req->sfn_sf);
			deallocate_nfapi_dl_config_request(req, pnf_p7);

			if(pnf_p7->_public.timing_info_mode_aperiodic)
			{
				pnf_p7->timing_info_aperiodic_send = 1;
			}

			pnf_p7->stats.dl_conf_late++;
		}

		if(pthread_mutex_unlock(&(pnf_p7->mutex)) != 0)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to unlock mutex\n");
			return;
		}
	}
	else
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "Failed to unpack dl_config_req");
		deallocate_nfapi_dl_config_request(req, pnf_p7);
	}
}

void cp_nr_ul_tti_req_prach_pdu(const nfapi_nr_prach_pdu_t *src_prach_pdu, nfapi_nr_prach_pdu_t *dst_prach_pdu) {
    dst_prach_pdu->phys_cell_id = src_prach_pdu->phys_cell_id;
    dst_prach_pdu->num_prach_ocas = src_prach_pdu->num_prach_ocas;
    dst_prach_pdu->prach_format = src_prach_pdu->prach_format;
    dst_prach_pdu->num_ra = src_prach_pdu->num_ra;
    dst_prach_pdu->prach_start_symbol = src_prach_pdu->prach_start_symbol;
		dst_prach_pdu->num_cs = src_prach_pdu->num_cs;
		dst_prach_pdu->beamforming.trp_scheme = src_prach_pdu->beamforming.trp_scheme;
		dst_prach_pdu->beamforming.num_prgs = src_prach_pdu->beamforming.num_prgs;
		dst_prach_pdu->beamforming.prg_size = src_prach_pdu->beamforming.prg_size;
		dst_prach_pdu->beamforming.dig_bf_interface = src_prach_pdu->beamforming.dig_bf_interface;
		if(src_prach_pdu->beamforming.num_prgs>0){
	    dst_prach_pdu->beamforming.prgs_list->dig_bf_interface_list->beam_idx = src_prach_pdu->beamforming.prgs_list->dig_bf_interface_list->beam_idx;			
    }
}
void cp_nr_ul_tti_req_pusch_pdu(const nfapi_nr_pusch_pdu_t *src_pusch_pdu, nfapi_nr_pusch_pdu_t *dst_pusch_pdu) {
    // Copy common fields
    dst_pusch_pdu->pdu_bit_map = src_pusch_pdu->pdu_bit_map;
    dst_pusch_pdu->rnti = src_pusch_pdu->rnti;
    dst_pusch_pdu->handle = src_pusch_pdu->handle;
    dst_pusch_pdu->bwp_size = src_pusch_pdu->bwp_size;
    dst_pusch_pdu->bwp_start = src_pusch_pdu->bwp_start;
    dst_pusch_pdu->subcarrier_spacing = src_pusch_pdu->subcarrier_spacing;
    dst_pusch_pdu->cyclic_prefix = src_pusch_pdu->cyclic_prefix;
    dst_pusch_pdu->target_code_rate = src_pusch_pdu->target_code_rate;
    dst_pusch_pdu->qam_mod_order = src_pusch_pdu->qam_mod_order;
    dst_pusch_pdu->mcs_index = src_pusch_pdu->mcs_index;
    dst_pusch_pdu->mcs_table = src_pusch_pdu->mcs_table;
    dst_pusch_pdu->transform_precoding = src_pusch_pdu->transform_precoding;
    dst_pusch_pdu->data_scrambling_id = src_pusch_pdu->data_scrambling_id;
    dst_pusch_pdu->nrOfLayers = src_pusch_pdu->nrOfLayers;
    dst_pusch_pdu->ul_dmrs_symb_pos = src_pusch_pdu->ul_dmrs_symb_pos;
    dst_pusch_pdu->dmrs_config_type = src_pusch_pdu->dmrs_config_type;
    dst_pusch_pdu->ul_dmrs_scrambling_id = src_pusch_pdu->ul_dmrs_scrambling_id;
    dst_pusch_pdu->pusch_identity = src_pusch_pdu->pusch_identity;
    dst_pusch_pdu->scid = src_pusch_pdu->scid;
    dst_pusch_pdu->num_dmrs_cdm_grps_no_data = src_pusch_pdu->num_dmrs_cdm_grps_no_data;
    dst_pusch_pdu->dmrs_ports = src_pusch_pdu->dmrs_ports;
    dst_pusch_pdu->resource_alloc = src_pusch_pdu->resource_alloc;
    memcpy(dst_pusch_pdu->rb_bitmap, src_pusch_pdu->rb_bitmap, sizeof(src_pusch_pdu->rb_bitmap));
    dst_pusch_pdu->rb_start = src_pusch_pdu->rb_start;
    dst_pusch_pdu->rb_size = src_pusch_pdu->rb_size;
    dst_pusch_pdu->vrb_to_prb_mapping = src_pusch_pdu->vrb_to_prb_mapping;
    dst_pusch_pdu->frequency_hopping = src_pusch_pdu->frequency_hopping;
    dst_pusch_pdu->tx_direct_current_location = src_pusch_pdu->tx_direct_current_location;
    dst_pusch_pdu->uplink_frequency_shift_7p5khz = src_pusch_pdu->uplink_frequency_shift_7p5khz;
    dst_pusch_pdu->start_symbol_index = src_pusch_pdu->start_symbol_index;
    dst_pusch_pdu->nr_of_symbols = src_pusch_pdu->nr_of_symbols;

    // Copy optional data if present
    if (src_pusch_pdu->pdu_bit_map & 0x01) // data 
        memcpy(&dst_pusch_pdu->pusch_data, &src_pusch_pdu->pusch_data, sizeof(nfapi_nr_pusch_data_t));
    if (src_pusch_pdu->pdu_bit_map & 0x10) // UCI
        memcpy(&dst_pusch_pdu->pusch_uci, &src_pusch_pdu->pusch_uci, sizeof(nfapi_nr_pusch_uci_t));
    if (src_pusch_pdu->pdu_bit_map & 0x100){ // PTRS
        // Copy the PUSCH PTRS fields directly
        dst_pusch_pdu->pusch_ptrs.num_ptrs_ports = src_pusch_pdu->pusch_ptrs.num_ptrs_ports;
        dst_pusch_pdu->pusch_ptrs.ptrs_time_density = src_pusch_pdu->pusch_ptrs.ptrs_time_density;
        dst_pusch_pdu->pusch_ptrs.ptrs_freq_density = src_pusch_pdu->pusch_ptrs.ptrs_freq_density;
        dst_pusch_pdu->pusch_ptrs.ul_ptrs_power = src_pusch_pdu->pusch_ptrs.ul_ptrs_power;
        for (int j = 0; j < src_pusch_pdu->pusch_ptrs.num_ptrs_ports; j++) {
            dst_pusch_pdu->pusch_ptrs.ptrs_ports_list[j].ptrs_port_index = src_pusch_pdu->pusch_ptrs.ptrs_ports_list[j].ptrs_port_index;
            dst_pusch_pdu->pusch_ptrs.ptrs_ports_list[j].ptrs_dmrs_port = src_pusch_pdu->pusch_ptrs.ptrs_ports_list[j].ptrs_dmrs_port;
            dst_pusch_pdu->pusch_ptrs.ptrs_ports_list[j].ptrs_re_offset = src_pusch_pdu->pusch_ptrs.ptrs_ports_list[j].ptrs_re_offset;
        }
    }
    if (src_pusch_pdu->pdu_bit_map & 0x1000) // DFTS
        memcpy(&dst_pusch_pdu->dfts_ofdm, &src_pusch_pdu->dfts_ofdm, sizeof(nfapi_nr_dfts_ofdm_t));
				
    dst_pusch_pdu->beamforming.trp_scheme = src_pusch_pdu->beamforming.trp_scheme;
    dst_pusch_pdu->beamforming.num_prgs = src_pusch_pdu->beamforming.num_prgs;
    dst_pusch_pdu->beamforming.prg_size = src_pusch_pdu->beamforming.prg_size;
    dst_pusch_pdu->beamforming.dig_bf_interface = src_pusch_pdu->beamforming.dig_bf_interface;
    if(src_pusch_pdu->beamforming.num_prgs>0)
        dst_pusch_pdu->beamforming.prgs_list->dig_bf_interface_list->beam_idx = src_pusch_pdu->beamforming.prgs_list->dig_bf_interface_list->beam_idx;
    
    memcpy(&dst_pusch_pdu->maintenance_parms_v3, &src_pusch_pdu->maintenance_parms_v3, sizeof(nfapi_v3_pdsch_maintenance_parameters_t));
}
void cp_nr_ul_tti_req_pucch_pdu(const nfapi_nr_pucch_pdu_t *src_pucch_pdu, nfapi_nr_pucch_pdu_t *dst_pucch_pdu) {
    // Copy common fields
    dst_pucch_pdu->rnti = src_pucch_pdu->rnti;
    dst_pucch_pdu->handle = src_pucch_pdu->handle;
    dst_pucch_pdu->bwp_size = src_pucch_pdu->bwp_size;
    dst_pucch_pdu->bwp_start = src_pucch_pdu->bwp_start;
    dst_pucch_pdu->subcarrier_spacing = src_pucch_pdu->subcarrier_spacing;
    dst_pucch_pdu->cyclic_prefix = src_pucch_pdu->cyclic_prefix;
    dst_pucch_pdu->format_type = src_pucch_pdu->format_type;
    dst_pucch_pdu->multi_slot_tx_indicator = src_pucch_pdu->multi_slot_tx_indicator;
    dst_pucch_pdu->pi_2bpsk = src_pucch_pdu->pi_2bpsk;
    dst_pucch_pdu->prb_start = src_pucch_pdu->prb_start;
    dst_pucch_pdu->prb_size = src_pucch_pdu->prb_size;
    dst_pucch_pdu->start_symbol_index = src_pucch_pdu->start_symbol_index;
    dst_pucch_pdu->nr_of_symbols = src_pucch_pdu->nr_of_symbols;
    dst_pucch_pdu->freq_hop_flag = src_pucch_pdu->freq_hop_flag;
    dst_pucch_pdu->second_hop_prb = src_pucch_pdu->second_hop_prb;
    dst_pucch_pdu->group_hop_flag = src_pucch_pdu->group_hop_flag;
    dst_pucch_pdu->sequence_hop_flag = src_pucch_pdu->sequence_hop_flag;
    dst_pucch_pdu->hopping_id = src_pucch_pdu->hopping_id;
    dst_pucch_pdu->initial_cyclic_shift = src_pucch_pdu->initial_cyclic_shift;
    dst_pucch_pdu->data_scrambling_id = src_pucch_pdu->data_scrambling_id;
    dst_pucch_pdu->time_domain_occ_idx = src_pucch_pdu->time_domain_occ_idx;
    dst_pucch_pdu->pre_dft_occ_idx = src_pucch_pdu->pre_dft_occ_idx;
    dst_pucch_pdu->pre_dft_occ_len = src_pucch_pdu->pre_dft_occ_len;
    dst_pucch_pdu->add_dmrs_flag = src_pucch_pdu->add_dmrs_flag;
    dst_pucch_pdu->dmrs_scrambling_id = src_pucch_pdu->dmrs_scrambling_id;
    dst_pucch_pdu->dmrs_cyclic_shift = src_pucch_pdu->dmrs_cyclic_shift;
    dst_pucch_pdu->sr_flag = src_pucch_pdu->sr_flag;
    dst_pucch_pdu->bit_len_harq = src_pucch_pdu->bit_len_harq;
    dst_pucch_pdu->bit_len_csi_part1 = src_pucch_pdu->bit_len_csi_part1;
    dst_pucch_pdu->bit_len_csi_part2 = src_pucch_pdu->bit_len_csi_part2;
    
    dst_pucch_pdu->beamforming.trp_scheme = src_pucch_pdu->beamforming.trp_scheme;
    dst_pucch_pdu->beamforming.num_prgs = src_pucch_pdu->beamforming.num_prgs;
    dst_pucch_pdu->beamforming.prg_size = src_pucch_pdu->beamforming.prg_size;
    dst_pucch_pdu->beamforming.dig_bf_interface = src_pucch_pdu->beamforming.dig_bf_interface;
	
    if(src_pucch_pdu->beamforming.num_prgs>0)
        dst_pucch_pdu->beamforming.prgs_list->dig_bf_interface_list->beam_idx = src_pucch_pdu->beamforming.prgs_list->dig_bf_interface_list->beam_idx;
}

void cp_nr_ul_tti_req_srs_pdu(const nfapi_nr_srs_pdu_t *src_srs_pdu, nfapi_nr_srs_pdu_t *dst_srs_pdu) {
    dst_srs_pdu->rnti = src_srs_pdu->rnti;
    dst_srs_pdu->handle = src_srs_pdu->handle;
    dst_srs_pdu->bwp_size = src_srs_pdu->bwp_size;
    dst_srs_pdu->bwp_start = src_srs_pdu->bwp_start;
    dst_srs_pdu->subcarrier_spacing = src_srs_pdu->subcarrier_spacing;
    dst_srs_pdu->cyclic_prefix = src_srs_pdu->cyclic_prefix;
    dst_srs_pdu->num_ant_ports = src_srs_pdu->num_ant_ports;
    dst_srs_pdu->num_symbols = src_srs_pdu->num_symbols;
    dst_srs_pdu->num_repetitions = src_srs_pdu->num_repetitions;
    dst_srs_pdu->time_start_position = src_srs_pdu->time_start_position;
    dst_srs_pdu->config_index = src_srs_pdu->config_index;
    dst_srs_pdu->sequence_id = src_srs_pdu->sequence_id;
    dst_srs_pdu->bandwidth_index = src_srs_pdu->bandwidth_index;
    dst_srs_pdu->comb_size = src_srs_pdu->comb_size;
    dst_srs_pdu->comb_offset = src_srs_pdu->comb_offset;
    dst_srs_pdu->cyclic_shift = src_srs_pdu->cyclic_shift;
    dst_srs_pdu->frequency_position = src_srs_pdu->frequency_position;
    dst_srs_pdu->frequency_shift = src_srs_pdu->frequency_shift;
    dst_srs_pdu->frequency_hopping = src_srs_pdu->frequency_hopping;
    dst_srs_pdu->group_or_sequence_hopping = src_srs_pdu->group_or_sequence_hopping;
    dst_srs_pdu->resource_type = src_srs_pdu->resource_type;
    dst_srs_pdu->t_srs = src_srs_pdu->t_srs;
    dst_srs_pdu->t_offset = src_srs_pdu->t_offset;
    //beamforming
    dst_srs_pdu->beamforming.trp_scheme = src_srs_pdu->beamforming.trp_scheme;
    dst_srs_pdu->beamforming.num_prgs = src_srs_pdu->beamforming.num_prgs;
    dst_srs_pdu->beamforming.prg_size = src_srs_pdu->beamforming.prg_size;
    dst_srs_pdu->beamforming.dig_bf_interface = src_srs_pdu->beamforming.dig_bf_interface;
    dst_srs_pdu->beamforming.prgs_list = src_srs_pdu->beamforming.prgs_list;
    if(src_srs_pdu->beamforming.num_prgs>0)
        dst_srs_pdu->beamforming.prgs_list->dig_bf_interface_list->beam_idx = src_srs_pdu->beamforming.prgs_list->dig_bf_interface_list->beam_idx;
    
    //srs_parameters_v4
    dst_srs_pdu->srs_parameters_v4.srs_bandwidth_size = src_srs_pdu->srs_parameters_v4.srs_bandwidth_size;
    dst_srs_pdu->srs_parameters_v4.symbol_list->srs_bandwidth_start = src_srs_pdu->srs_parameters_v4.symbol_list->srs_bandwidth_start;
    dst_srs_pdu->srs_parameters_v4.symbol_list->sequence_group = src_srs_pdu->srs_parameters_v4.symbol_list->sequence_group;
    dst_srs_pdu->srs_parameters_v4.symbol_list->sequence_number = src_srs_pdu->srs_parameters_v4.symbol_list->sequence_number;
    dst_srs_pdu->srs_parameters_v4.usage = src_srs_pdu->srs_parameters_v4.usage;
    memcpy(dst_srs_pdu->srs_parameters_v4.report_type, src_srs_pdu->srs_parameters_v4.report_type, sizeof(src_srs_pdu->srs_parameters_v4.report_type));
    dst_srs_pdu->srs_parameters_v4.singular_Value_representation = src_srs_pdu->srs_parameters_v4.singular_Value_representation;
    dst_srs_pdu->srs_parameters_v4.iq_representation = src_srs_pdu->srs_parameters_v4.iq_representation;
    dst_srs_pdu->srs_parameters_v4.prg_size = src_srs_pdu->srs_parameters_v4.prg_size;
    dst_srs_pdu->srs_parameters_v4.num_total_ue_antennas = src_srs_pdu->srs_parameters_v4.num_total_ue_antennas;
    dst_srs_pdu->srs_parameters_v4.ue_antennas_in_this_srs_resource_set = src_srs_pdu->srs_parameters_v4.ue_antennas_in_this_srs_resource_set;
    dst_srs_pdu->srs_parameters_v4.sampled_ue_antennas = src_srs_pdu->srs_parameters_v4.sampled_ue_antennas;
    dst_srs_pdu->srs_parameters_v4.report_scope = src_srs_pdu->srs_parameters_v4.report_scope;
    dst_srs_pdu->srs_parameters_v4.num_ul_spatial_streams_ports = src_srs_pdu->srs_parameters_v4.num_ul_spatial_streams_ports;
    memcpy(dst_srs_pdu->srs_parameters_v4.Ul_spatial_stream_ports, src_srs_pdu->srs_parameters_v4.Ul_spatial_stream_ports, sizeof(src_srs_pdu->srs_parameters_v4.Ul_spatial_stream_ports));
}
static void cp_nr_ul_tti_req(nfapi_nr_ul_tti_request_t *dst, const nfapi_nr_ul_tti_request_t *src)
{
    dst->header = src->header;
    dst->SFN = src->SFN;
    dst->Slot = src->Slot;
    dst->n_pdus = src->n_pdus;
    dst->rach_present = src->rach_present;
    dst->n_ulsch = src->n_ulsch;
    dst->n_ulcch = src->n_ulcch;
    dst->n_group = src->n_group;
	// LOG_I(PHY,"cp_nr_ul_tti_req: dst->n_pdus=%d\n", dst->n_pdus);
	for (int i = 0; i < dst->n_pdus; ++i) {
		dst->pdus_list[i].pdu_type = src->pdus_list[i].pdu_type;
		dst->pdus_list[i].pdu_size = src->pdus_list[i].pdu_size;
		switch (src->pdus_list[i].pdu_type) {
			case 0: // PRACH PDU
				cp_nr_ul_tti_req_prach_pdu(&src->pdus_list[i].prach_pdu, &dst->pdus_list[i].prach_pdu);
				break;
			case 1: // PUSCH PDU
				cp_nr_ul_tti_req_pusch_pdu(&src->pdus_list[i].pusch_pdu, &dst->pdus_list[i].pusch_pdu);
				break;
			case 2: // PUCCH PDU
				cp_nr_ul_tti_req_pucch_pdu(&src->pdus_list[i].pucch_pdu, &dst->pdus_list[i].pucch_pdu);
				break;
			case 3: // SRS PDU
				cp_nr_ul_tti_req_srs_pdu(&src->pdus_list[i].srs_pdu, &dst->pdus_list[i].srs_pdu);
				break;
		}
	}
	for (int i = 0; i < dst->n_group; ++i) {
		dst->groups_list[i].n_ue = src->groups_list[i].n_ue;
		for (int j = 0; j < src->groups_list[i].n_ue; ++j) {
			dst->groups_list[i].ue_list[j].pdu_idx = src->groups_list[i].ue_list[j].pdu_idx;
		}
	}
}

void pnf_handle_ul_tti_request(void* pRecvMsg, int recvMsgLen, pnf_p7_t* pnf_p7)
{
	//NFAPI_TRACE(NFAPI_TRACE_INFO, "UL_CONFIG.req Received\n");

	nfapi_nr_ul_tti_request_t req;
	int unpack_result = nfapi_nr_p7_message_unpack(pRecvMsg, recvMsgLen, &req, sizeof(nfapi_nr_ul_tti_request_t), &(pnf_p7->_public.codec_config));

	if(unpack_result == 0)
	{
		if(pthread_mutex_lock(&(pnf_p7->mutex)) != 0)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to lock mutex\n");
			return;
		}

		if(is_nr_p7_request_in_window(req.SFN,req.Slot, "ul_tti_request", pnf_p7))
		{
			uint32_t sfn_slot_dec = NFAPI_SFNSLOT2DEC(req.SFN,req.Slot);
			uint8_t buffer_index = (sfn_slot_dec % 20);

                        struct timespec t;
                        clock_gettime(CLOCK_MONOTONIC, &t);
                        NFAPI_TRACE(NFAPI_TRACE_DEBUG,"%s() %ld.%09ld POPULATE UL_TTI_REQ current tx sfn/slot:%d.%d p7 msg sfn/slot: %d.%d buffer_index:%d\n", __FUNCTION__, t.tv_sec, t.tv_nsec, pnf_p7->sfn,pnf_p7->slot, req.SFN, req.Slot, buffer_index);
			
			//filling slot buffer

			pnf_p7->slot_buffer[buffer_index].sfn = req.SFN;
			pnf_p7->slot_buffer[buffer_index].slot = req.Slot;
			cp_nr_ul_tti_req(&pnf_p7->slot_buffer[buffer_index].ul_tti_req, &req);
			
			pnf_p7->stats.ul_tti_ontime++;
		}
		else
		{
			NFAPI_TRACE(NFAPI_TRACE_NOTE, "[%d] NOT storing ul_tti_req OUTSIDE OF TRANSMIT BUFFER WINDOW SFN/SLOT %d\n", NFAPI_SFNSLOT2DEC(pnf_p7->sfn,pnf_p7->slot), NFAPI_SFNSLOT2DEC(req.SFN,req.Slot));
			if(pnf_p7->_public.timing_info_mode_aperiodic)
				pnf_p7->timing_info_aperiodic_send = 1;

			pnf_p7->stats.ul_tti_late++;
		}

		if(pthread_mutex_unlock(&(pnf_p7->mutex)) != 0)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to unlock mutex\n");
			return;
		}
	}
	else
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "Failed to unpack ul_tti_req\n");
	}
}

void pnf_handle_ul_config_request(void* pRecvMsg, int recvMsgLen, pnf_p7_t* pnf_p7)
{
	//NFAPI_TRACE(NFAPI_TRACE_INFO, "UL_CONFIG.req Received\n");

	nfapi_ul_config_request_t* req  = allocate_nfapi_ul_config_request(pnf_p7);

	if(req == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to allocate nfapi_ul_config_request structure\n");
		return;
	}

	int unpack_result = nfapi_p7_message_unpack(pRecvMsg, recvMsgLen, req, sizeof(nfapi_ul_config_request_t), &(pnf_p7->_public.codec_config));

	if(unpack_result == 0)
	{
		if(pthread_mutex_lock(&(pnf_p7->mutex)) != 0)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to lock mutex\n");
			return;
		}

		if(is_p7_request_in_window(req->sfn_sf, "ul_config_request", pnf_p7))
		{
			uint32_t sfn_sf_dec = NFAPI_SFNSF2DEC(req->sfn_sf);
			uint8_t buffer_index = sfn_sf_dec % pnf_p7->_public.subframe_buffer_size;

                        struct timespec t;
                        clock_gettime(CLOCK_MONOTONIC, &t);

                        NFAPI_TRACE(NFAPI_TRACE_INFO,"%s() %ld.%09ld POPULATE UL_CONFIG_REQ sfn_sf:%d buffer_index:%d\n", __FUNCTION__, t.tv_sec, t.tv_nsec, sfn_sf_dec, buffer_index);

			if(pnf_p7->subframe_buffer[buffer_index].ul_config_req != 0)
			{
				//NFAPI_TRACE(NFAPI_TRACE_NOTE, "[%d] Freeing ul_config_req at index %d (%d/%d)", 
				//			pMyPhyInfo->sfnSf, bufferIdx,
				//			SFNSF2SFN(dreq->sfn_sf), SFNSF2SF(dreq->sfn_sf));

				deallocate_nfapi_ul_config_request(pnf_p7->subframe_buffer[buffer_index].ul_config_req, pnf_p7);
			}

			pnf_p7->subframe_buffer[buffer_index].sfn_sf = req->sfn_sf;
			pnf_p7->subframe_buffer[buffer_index].ul_config_req = req;
			
			pnf_p7->stats.ul_conf_ontime++;
		}
		else
		{
			NFAPI_TRACE(NFAPI_TRACE_NOTE, "[%d] NOT storing ul_config_req OUTSIDE OF TRANSMIT BUFFER WINDOW SFN/SF %d\n", NFAPI_SFNSF2DEC(pnf_p7->sfn_sf), NFAPI_SFNSF2DEC(req->sfn_sf));
			deallocate_nfapi_ul_config_request(req, pnf_p7);

			if(pnf_p7->_public.timing_info_mode_aperiodic)
			{
				pnf_p7->timing_info_aperiodic_send = 1;
			}

			pnf_p7->stats.ul_conf_late++;
		}

		if(pthread_mutex_unlock(&(pnf_p7->mutex)) != 0)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to unlock mutex\n");
			return;
		}
	}
	else
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "Failed to unpack ul_config_req\n");
		deallocate_nfapi_ul_config_request(req, pnf_p7);
	}
}

static void cp_nr_ul_dci_req(nfapi_nr_ul_dci_request_t *dst, const nfapi_nr_ul_dci_request_t *src)
{
  dst->header = src->header;
  dst->SFN = src->SFN;
  dst->Slot = src->Slot;
  dst->numPdus = src->numPdus;
  
  for (int i = 0; i < dst->numPdus; ++i) {
    nfapi_nr_ul_dci_request_pdus_t *dst_pdu = &dst->ul_dci_pdu_list[i];
    const nfapi_nr_ul_dci_request_pdus_t *src_pdu = &src->ul_dci_pdu_list[i];
    
    dst_pdu->PDUType = src_pdu->PDUType;
    dst_pdu->PDUSize = src_pdu->PDUSize;
    dst_pdu->pdcch_pdu = src_pdu->pdcch_pdu;
  }
}

void pnf_handle_ul_dci_request(void* pRecvMsg, int recvMsgLen, pnf_p7_t* pnf_p7)
{
	nfapi_nr_ul_dci_request_t req;

	int unpack_result = nfapi_nr_p7_message_unpack(pRecvMsg, recvMsgLen, &req, sizeof(nfapi_nr_ul_dci_request_t), &pnf_p7->_public.codec_config);

	if(unpack_result == 0)
	{
		if(pthread_mutex_lock(&(pnf_p7->mutex)) != 0)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to lock mutex\n");
			return;
		}

		if(is_nr_p7_request_in_window(req.SFN,req.Slot,"ul_dci_request", pnf_p7))
		{
			uint32_t sfn_slot_dec = NFAPI_SFNSLOT2DEC(req.SFN,req.Slot);
			uint8_t buffer_index = sfn_slot_dec % 20;

			pnf_p7->slot_buffer[buffer_index].sfn = req.SFN;
			cp_nr_ul_dci_req(&pnf_p7->slot_buffer[buffer_index].ul_dci_req, &req);


			pnf_p7->stats.ul_dci_ontime++;
			
		}
		else
		{
			if(pnf_p7->_public.timing_info_mode_aperiodic)
			{
				pnf_p7->timing_info_aperiodic_send = 1;
			}

			pnf_p7->stats.ul_dci_late++;
		}

		if(pthread_mutex_unlock(&(pnf_p7->mutex)) != 0)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to unlock mutex\n");
			return;
		}
	}
	else
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "Failed to unpack UL DCI req\n");
	}
}



void pnf_handle_hi_dci0_request(void* pRecvMsg, int recvMsgLen, pnf_p7_t* pnf_p7)

{
	//NFAPI_TRACE(NFAPI_TRACE_INFO, "HI_DCI0.req Received\n");

	nfapi_hi_dci0_request_t* req  = allocate_nfapi_hi_dci0_request(pnf_p7);

	if(req == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to allocate nfapi_hi_dci0_request structure\n");
		return;
	}

	int unpack_result = nfapi_p7_message_unpack(pRecvMsg, recvMsgLen, req, sizeof(nfapi_hi_dci0_request_t), &pnf_p7->_public.codec_config);

	if(unpack_result == 0)
	{
		if(pthread_mutex_lock(&(pnf_p7->mutex)) != 0)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to lock mutex\n");
			return;
		}

		if(is_p7_request_in_window(req->sfn_sf, "hi_dci0_request", pnf_p7))
		{
			uint32_t sfn_sf_dec = NFAPI_SFNSF2DEC(req->sfn_sf);
			uint8_t buffer_index = sfn_sf_dec % pnf_p7->_public.subframe_buffer_size;

			if(pnf_p7->subframe_buffer[buffer_index].hi_dci0_req!= 0)
			{
				//NFAPI_TRACE(NFAPI_TRACE_NOTE, "[%d] Freeing hi_dci0_req at index %d (%d/%d)", 
				//			pMyPhyInfo->sfnSf, bufferIdx,
				//			SFNSF2SFN(dreq->sfn_sf), SFNSF2SF(dreq->sfn_sf));

				deallocate_nfapi_hi_dci0_request(pnf_p7->subframe_buffer[buffer_index].hi_dci0_req, pnf_p7);
			}

			pnf_p7->subframe_buffer[buffer_index].sfn_sf = req->sfn_sf;
			pnf_p7->subframe_buffer[buffer_index].hi_dci0_req = req;

			pnf_p7->stats.hi_dci0_ontime++;
			
		}
		else
		{
			//NFAPI_TRACE(NFAPI_TRACE_NOTE, "[%d] NOT storing hi_dci0_req SFN/SF %d/%d\n", pMyPhyInfo->sfnSf, SFNSF2SFN(req->sfn_sf), SFNSF2SF(req->sfn_sf));
			deallocate_nfapi_hi_dci0_request(req, pnf_p7);

			if(pnf_p7->_public.timing_info_mode_aperiodic)
			{
				pnf_p7->timing_info_aperiodic_send = 1;
			}

			pnf_p7->stats.hi_dci0_late++;
		}

		if(pthread_mutex_unlock(&(pnf_p7->mutex)) != 0)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to unlock mutex\n");
			return;
		}
	}
	else
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "Failed to unpack hi_dci0_req\n");
		deallocate_nfapi_hi_dci0_request(req, pnf_p7);
	}
}

static void cp_nr_tx_data_req(nfapi_nr_tx_data_request_t *dst, const nfapi_nr_tx_data_request_t *src)
{
  dst->header = src->header;
  dst->SFN = src->SFN;
  dst->Slot = src->Slot;
  dst->Number_of_PDUs = src->Number_of_PDUs;
  for (int i = 0; i < dst->Number_of_PDUs; ++i) {
    nfapi_nr_pdu_t *dst_pdu = &dst->pdu_list[i];
    const nfapi_nr_pdu_t *src_pdu = &src->pdu_list[i];
    dst_pdu->PDU_length = src_pdu->PDU_length;
    dst_pdu->PDU_index = src_pdu->PDU_index;
    dst_pdu->num_TLV = src_pdu->num_TLV;
    for (int j = 0; j < dst->pdu_list[i].num_TLV; ++j) {
      nfapi_nr_tx_data_request_tlv_t *dst_tlv = &dst_pdu->TLVs[j];
      const nfapi_nr_tx_data_request_tlv_t *src_tlv = &src_pdu->TLVs[j];
      dst_tlv->tag = src_tlv->tag;
      dst_tlv->length = src_tlv->length;
      memcpy(dst_tlv->value.direct, src_tlv->value.direct, dst_tlv->length);
    }
  }
}

void pnf_handle_tx_data_request(void* pRecvMsg, int recvMsgLen, pnf_p7_t* pnf_p7)
{
	//NFAPI_TRACE(NFAPI_TRACE_INFO, "TX.req Received\n");
	
	nfapi_nr_tx_data_request_t req;

	int unpack_result = nfapi_nr_p7_message_unpack(pRecvMsg, recvMsgLen, &req, sizeof(nfapi_nr_tx_data_request_t), &pnf_p7->_public.codec_config);
	if(unpack_result == 0)
	{
		if(pthread_mutex_lock(&(pnf_p7->mutex)) != 0)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to lock mutex\n");
			return;
		}

		if(is_nr_p7_request_in_window(req.SFN, req.Slot,"tx_request", pnf_p7))
		{
			uint32_t sfn_slot_dec = NFAPI_SFNSLOT2DEC(req.SFN,req.Slot);
			uint8_t buffer_index = sfn_slot_dec % 20;

                        struct timespec t;
                        clock_gettime(CLOCK_MONOTONIC, &t);

                        //NFAPI_TRACE(NFAPI_TRACE_INFO,"%s() %ld.%09ld POPULATE TX_DATA_REQ sfn_sf:%d buffer_index:%d\n", __FUNCTION__, t.tv_sec, t.tv_nsec, sfn_slot_dec, buffer_index);
#if 0
                        if (0 && NFAPI_SFNSF2DEC(req->sfn_sf)%100==0) NFAPI_TRACE(NFAPI_TRACE_INFO, "%s() TX_REQ.req sfn_sf:%d pdus:%d - TX_REQ is within window\n",
                            __FUNCTION__,
                            NFAPI_SFNSF2DEC(req->sfn_sf),
                            req->tx_request_body.number_of_pdus);
#endif


			pnf_p7->slot_buffer[buffer_index].sfn = req.SFN;
			pnf_p7->slot_buffer[buffer_index].slot = req.Slot;
      		cp_nr_tx_data_req(&pnf_p7->slot_buffer[buffer_index].tx_data_req, &req);
			pnf_p7->stats.tx_data_ontime++;
		}
		else
		{
                  NFAPI_TRACE(NFAPI_TRACE_INFO,"%s() TX_DATA_REQUEST Request is outside of window REQ:SFN_SLOT:%d CURR:SFN_SLOT:%d\n", __FUNCTION__, NFAPI_SFNSLOT2DEC(req.SFN,req.Slot), NFAPI_SFNSLOT2DEC(pnf_p7->sfn,pnf_p7->slot));

			if(pnf_p7->_public.timing_info_mode_aperiodic)
			{
				pnf_p7->timing_info_aperiodic_send = 1;
			}

			pnf_p7->stats.tx_data_late++;
		}

		if(pthread_mutex_unlock(&(pnf_p7->mutex)) != 0)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to unlock mutex\n");
			return;
		}
  }
}



void pnf_handle_tx_request(void* pRecvMsg, int recvMsgLen, pnf_p7_t* pnf_p7)
{
	//NFAPI_TRACE(NFAPI_TRACE_INFO, "TX.req Received\n");
	
	nfapi_tx_request_t* req = allocate_nfapi_tx_request(pnf_p7);

	if(req == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to allocate nfapi_tx_request structure\n");
		return;
	}

	int unpack_result = nfapi_p7_message_unpack(pRecvMsg, recvMsgLen, req, sizeof(nfapi_tx_request_t), &pnf_p7->_public.codec_config);
	if(unpack_result == 0)
	{
		if(pthread_mutex_lock(&(pnf_p7->mutex)) != 0)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to lock mutex\n");
			return;
		}

		if(is_p7_request_in_window(req->sfn_sf, "tx_request", pnf_p7))
		{
			uint32_t sfn_sf_dec = NFAPI_SFNSF2DEC(req->sfn_sf);
			uint8_t buffer_index = sfn_sf_dec % pnf_p7->_public.subframe_buffer_size;

                        struct timespec t;
                        clock_gettime(CLOCK_MONOTONIC, &t);

                        NFAPI_TRACE(NFAPI_TRACE_INFO,"%s() %ld.%09ld POPULATE TX_REQ sfn_sf:%d buffer_index:%d\n", __FUNCTION__, t.tv_sec, t.tv_nsec, sfn_sf_dec, buffer_index);

                        if (0 && NFAPI_SFNSF2DEC(req->sfn_sf)%100==0) NFAPI_TRACE(NFAPI_TRACE_INFO, "%s() TX_REQ.req sfn_sf:%d pdus:%d - TX_REQ is within window\n",
                            __FUNCTION__,
                            NFAPI_SFNSF2DEC(req->sfn_sf),
                            req->tx_request_body.number_of_pdus);

			if(pnf_p7->subframe_buffer[buffer_index].tx_req != 0)
			{
				//NFAPI_TRACE(NFAPI_TRACE_NOTE, "[%d] Freeing tx_req at index %d (%d/%d)", 
				//			pMyPhyInfo->sfnSf, bufferIdx,
				//			SFNSF2SFN(dreq->sfn_sf), SFNSF2SF(dreq->sfn_sf));

				deallocate_nfapi_tx_request(pnf_p7->subframe_buffer[buffer_index].tx_req, pnf_p7);
			}

			pnf_p7->subframe_buffer[buffer_index].sfn_sf = req->sfn_sf;
			pnf_p7->subframe_buffer[buffer_index].tx_req = req;

			pnf_p7->stats.tx_ontime++;
		}
		else
		{
                  NFAPI_TRACE(NFAPI_TRACE_INFO,"%s() TX_REQUEST Request is outside of window REQ:SFN_SF:%d CURR:SFN_SF:%d\n", __FUNCTION__, NFAPI_SFNSF2DEC(req->sfn_sf), NFAPI_SFNSF2DEC(pnf_p7->sfn_sf));

			deallocate_nfapi_tx_request(req, pnf_p7);

			if(pnf_p7->_public.timing_info_mode_aperiodic)
			{
				pnf_p7->timing_info_aperiodic_send = 1;
			}

			pnf_p7->stats.tx_late++;
		}

		if(pthread_mutex_unlock(&(pnf_p7->mutex)) != 0)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to unlock mutex\n");
			return;
		}
	}
	else
	{
		deallocate_nfapi_tx_request(req, pnf_p7);
	}
}

void pnf_handle_lbt_dl_config_request(void* pRecvMsg, int recvMsgLen, pnf_p7_t* pnf_p7)
{
	nfapi_lbt_dl_config_request_t* req = allocate_nfapi_lbt_dl_config_request(pnf_p7);

	if(req == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to allocate nfapi_lbt_dl_config_request structure\n");
		return;
	}

	int unpack_result = nfapi_p7_message_unpack(pRecvMsg, recvMsgLen, req, sizeof(nfapi_lbt_dl_config_request_t), &pnf_p7->_public.codec_config);

	if(unpack_result == 0)
	{
		if(pthread_mutex_lock(&(pnf_p7->mutex)) != 0)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to lock mutex\n");
			return;
		}

		if(is_p7_request_in_window(req->sfn_sf, "lbt_dl_request", pnf_p7))
		{
			uint32_t sfn_sf_dec = NFAPI_SFNSF2DEC(req->sfn_sf);
			uint8_t buffer_index = sfn_sf_dec % pnf_p7->_public.subframe_buffer_size;

			if(pnf_p7->subframe_buffer[buffer_index].lbt_dl_config_req != 0)
			{
				//NFAPI_TRACE(NFAPI_TRACE_NOTE, "[%d] Freeing tx_req at index %d (%d/%d)", 
				//			pMyPhyInfo->sfnSf, bufferIdx,
				//			SFNSF2SFN(dreq->sfn_sf), SFNSF2SF(dreq->sfn_sf));

				deallocate_nfapi_lbt_dl_config_request(pnf_p7->subframe_buffer[buffer_index].lbt_dl_config_req, pnf_p7);
			}

			pnf_p7->subframe_buffer[buffer_index].sfn_sf = req->sfn_sf;
			pnf_p7->subframe_buffer[buffer_index].lbt_dl_config_req = req;
		}
		else
		{
			deallocate_nfapi_lbt_dl_config_request(req, pnf_p7);

			if(pnf_p7->_public.timing_info_mode_aperiodic)
			{
				pnf_p7->timing_info_aperiodic_send = 1;
			}
		}

		if(pthread_mutex_unlock(&(pnf_p7->mutex)) != 0)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to unlock mutex\n");
			return;
		}
	}
	else
	{
		deallocate_nfapi_lbt_dl_config_request(req, pnf_p7);
	}

}

void pnf_handle_p7_vendor_extension(void* pRecvMsg, int recvMsgLen, pnf_p7_t* pnf_p7, uint16_t message_id)
{
	if(pnf_p7->_public.allocate_p7_vendor_ext)
	{
		uint16_t msg_size;
		nfapi_p7_message_header_t* msg = pnf_p7->_public.allocate_p7_vendor_ext(message_id, &msg_size);

		if(msg == 0)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to allocate vendor extention structure\n");
			return;
		}

		int unpack_result = nfapi_p7_message_unpack(pRecvMsg, recvMsgLen, msg, msg_size, &pnf_p7->_public.codec_config);

		if(unpack_result == 0)
		{
			if(pnf_p7->_public.vendor_ext)
				pnf_p7->_public.vendor_ext(&(pnf_p7->_public), msg);
		}
		
		if(pnf_p7->_public.deallocate_p7_vendor_ext)
			pnf_p7->_public.deallocate_p7_vendor_ext(msg);
		
	}
	
}

void pnf_handle_ue_release_request(void* pRecvMsg, int recvMsgLen, pnf_p7_t* pnf_p7)
{
    nfapi_ue_release_request_t* req = allocate_nfapi_ue_release_request(pnf_p7);
    if(req == NULL)
    {
        NFAPI_TRACE(NFAPI_TRACE_ERROR, "failed to allocate nfapi_ue_release_request structure\n");
        return;
    }

    int unpack_result = nfapi_p7_message_unpack(pRecvMsg, recvMsgLen, req, sizeof(nfapi_ue_release_request_t), &pnf_p7->_public.codec_config);
    if(unpack_result == 0)
    {
        if(pthread_mutex_lock(&(pnf_p7->mutex)) != 0)
        {
            NFAPI_TRACE(NFAPI_TRACE_ERROR, "failed to lock mutex\n");
            return;
        }

        if(is_p7_request_in_window(req->sfn_sf, "ue_release_request", pnf_p7))
        {
            uint32_t sfn_sf_dec = NFAPI_SFNSF2DEC(req->sfn_sf);
            uint8_t buffer_index = sfn_sf_dec % pnf_p7->_public.subframe_buffer_size;

            struct timespec t;
            clock_gettime(CLOCK_MONOTONIC, &t);

            NFAPI_TRACE(NFAPI_TRACE_INFO,"%s() %ld.%09ld POPULATE UE_RELEASE_REQ sfn_sf:%d buffer_index:%d\n", __FUNCTION__, t.tv_sec, t.tv_nsec, sfn_sf_dec, buffer_index);

            if (0 && NFAPI_SFNSF2DEC(req->sfn_sf)%100==0) NFAPI_TRACE(NFAPI_TRACE_INFO, "%s() UE_RELEASE_REQ.req sfn_sf:%d rntis:%d - UE_RELEASE_REQ is within window\n",
                            __FUNCTION__,
                            NFAPI_SFNSF2DEC(req->sfn_sf),
                            req->ue_release_request_body.number_of_TLVs);

            if(pnf_p7->subframe_buffer[buffer_index].ue_release_req != 0)
            {
                deallocate_nfapi_ue_release_request(pnf_p7->subframe_buffer[buffer_index].ue_release_req, pnf_p7);
            }

            pnf_p7->subframe_buffer[buffer_index].sfn_sf = req->sfn_sf;
            pnf_p7->subframe_buffer[buffer_index].ue_release_req = req;

            pnf_p7->stats.tx_ontime++;
        }
        else
        {
            NFAPI_TRACE(NFAPI_TRACE_INFO,"%s() UE_RELEASE_REQUEST Request is outside of window REQ:SFN_SF:%d CURR:SFN_SF:%d\n", __FUNCTION__, NFAPI_SFNSF2DEC(req->sfn_sf), NFAPI_SFNSF2DEC(pnf_p7->sfn_sf));

            deallocate_nfapi_ue_release_request(req, pnf_p7);
            if(pnf_p7->_public.timing_info_mode_aperiodic)
            {
                pnf_p7->timing_info_aperiodic_send = 1;
            }

            pnf_p7->stats.tx_late++;
        }
        nfapi_ue_release_response_t resp;
        memset(&resp, 0, sizeof(resp));
        resp.header.message_id = NFAPI_UE_RELEASE_RESPONSE;
        resp.header.phy_id = req->header.phy_id;
        resp.error_code = NFAPI_MSG_OK;
        nfapi_pnf_ue_release_resp(&(pnf_p7->_public), &resp);
        NFAPI_TRACE(NFAPI_TRACE_INFO, "do ue_release_response\n");

        if(pthread_mutex_unlock(&(pnf_p7->mutex)) != 0)
        {
            NFAPI_TRACE(NFAPI_TRACE_ERROR, "failed to unlock mutex\n");
            return;
        }
    }
    else
    {
        deallocate_nfapi_ue_release_request(req, pnf_p7);
    }
}

uint32_t calculate_t2(uint32_t now_time_hr, uint16_t sfn_sf, uint32_t sf_start_time_hr)
{
	uint32_t sf_time_us = get_sf_time(now_time_hr, sf_start_time_hr);
	uint32_t t2 = (NFAPI_SFNSF2DEC(sfn_sf) * 1000) + sf_time_us;

        if (0)
        {
          static uint32_t prev_t2 = 0;

          NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s(now_time_hr:%u sfn_sf:%d sf_start_time_Hr:%u) sf_time_us:%u t2:%u prev_t2:%u diff:%u\n",
              __FUNCTION__,
              now_time_hr, NFAPI_SFNSF2DEC(sfn_sf), sf_start_time_hr,
              sf_time_us,
              t2,
              prev_t2,
              t2-prev_t2);

          prev_t2 = t2;
        }

	return t2;
}

uint32_t calculate_nr_t2(uint32_t now_time_hr, uint16_t sfn,uint16_t slot, uint32_t slot_start_time_hr)
{
	uint32_t slot_time_us = get_slot_time(now_time_hr, slot_start_time_hr);
	uint32_t t2 = (NFAPI_SFNSLOT2DEC(sfn, slot) * 500) + slot_time_us;
	
        if (0)
        {
          static uint32_t prev_t2 = 0;

          NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s(now_time_hr:%u sfn to slot:%d slot_start_time_Hr:%u) slot_time_us:%u t2:%u prev_t2:%u diff:%u\n",
              __FUNCTION__,
              now_time_hr, NFAPI_SFNSLOT2DEC(sfn, slot), slot_start_time_hr,
              slot_time_us,
              t2,
              prev_t2,
              t2-prev_t2);

          prev_t2 = t2;
        }

	return t2;
}

uint32_t calculate_t3(uint16_t sfn_sf, uint32_t sf_start_time_hr)
{
	uint32_t now_time_hr = pnf_get_current_time_hr();

	uint32_t sf_time_us = get_sf_time(now_time_hr, sf_start_time_hr);

	uint32_t t3 = (NFAPI_SFNSF2DEC(sfn_sf) * 1000) + sf_time_us;

	return t3;
}

uint32_t calculate_nr_t3(uint16_t sfn, uint16_t slot, uint32_t slot_start_time_hr)
{
	uint32_t now_time_hr = pnf_get_current_time_hr();

	uint32_t slot_time_us = get_slot_time(now_time_hr, slot_start_time_hr);

	uint32_t t3 = (NFAPI_SFNSLOT2DEC(sfn, slot) * 500) + slot_time_us;
	
	return t3;
}

void pnf_handle_dl_node_sync(void *pRecvMsg, int recvMsgLen, pnf_p7_t* pnf_p7, uint32_t rx_hr_time)
{
	nfapi_dl_node_sync_t dl_node_sync;

	//NFAPI_TRACE(NFAPI_TRACE_INFO, "DL_NODE_SYNC Received\n");

	if (pRecvMsg == NULL || pnf_p7 == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: NULL parameters\n", __FUNCTION__);
		return;
	}

	// unpack the message
	if (nfapi_p7_message_unpack(pRecvMsg, recvMsgLen, &dl_node_sync, sizeof(dl_node_sync), &pnf_p7->_public.codec_config) < 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: Unpack message failed, ignoring\n", __FUNCTION__);
		return;
	}

	if(pthread_mutex_lock(&(pnf_p7->mutex)) != 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to lock mutex\n");
		return;
	}


	if (dl_node_sync.delta_sfn_sf != 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_INFO, "Will shift SF timing by %d on next subframe\n", dl_node_sync.delta_sfn_sf);

		pnf_p7->sfn_sf_shift = dl_node_sync.delta_sfn_sf;
	}

	nfapi_ul_node_sync_t ul_node_sync;
	memset(&ul_node_sync, 0, sizeof(ul_node_sync));
	ul_node_sync.header.message_id = NFAPI_UL_NODE_SYNC;
	ul_node_sync.header.phy_id = dl_node_sync.header.phy_id;
	ul_node_sync.t1 = dl_node_sync.t1;
	ul_node_sync.t2 = calculate_t2(rx_hr_time, pnf_p7->sfn_sf, pnf_p7->sf_start_time_hr);
	ul_node_sync.t3 = calculate_t3(pnf_p7->sfn_sf, pnf_p7->sf_start_time_hr);

	if(pthread_mutex_unlock(&(pnf_p7->mutex)) != 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to unlock mutex\n");
		return;
	}

	pnf_p7_pack_and_send_p7_message(pnf_p7, &(ul_node_sync.header), sizeof(ul_node_sync));
}


void pnf_nr_handle_dl_node_sync(void *pRecvMsg, int recvMsgLen, pnf_p7_t* pnf_p7, uint32_t rx_hr_time)
{	
	//printf("Received DL node sync");

	nfapi_nr_dl_node_sync_t dl_node_sync;

	//NFAPI_TRACE(NFAPI_TRACE_INFO, "DL_NODE_SYNC Received\n");

	if (pRecvMsg == NULL || pnf_p7 == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: NULL parameters\n", __FUNCTION__);
		return;
	}

	// unpack the message
	if (nfapi_nr_p7_message_unpack(pRecvMsg, recvMsgLen, &dl_node_sync, sizeof(dl_node_sync), &pnf_p7->_public.codec_config) < 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: Unpack message failed, ignoring\n", __FUNCTION__);
		return;
	}

	if(pthread_mutex_lock(&(pnf_p7->mutex)) != 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to lock mutex\n");
		return;
	}


	if (dl_node_sync.delta_sfn_slot != 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_INFO, "Will shift Slot timing by %d on next slot\n", dl_node_sync.delta_sfn_slot);

		pnf_p7->slot_shift = dl_node_sync.delta_sfn_slot;
	}

	nfapi_nr_ul_node_sync_t ul_node_sync;
	memset(&ul_node_sync, 0, sizeof(ul_node_sync));
	ul_node_sync.header.message_id = NFAPI_NR_PHY_MSG_TYPE_UL_NODE_SYNC;
	ul_node_sync.header.phy_id = dl_node_sync.header.phy_id;
	ul_node_sync.t1 = dl_node_sync.t1;
	ul_node_sync.t2 = calculate_nr_t2(rx_hr_time, pnf_p7->sfn,pnf_p7->slot, pnf_p7->slot_start_time_hr);
	ul_node_sync.t3 = calculate_nr_t3(pnf_p7->sfn,pnf_p7->slot, pnf_p7->slot_start_time_hr);

	if(pthread_mutex_unlock(&(pnf_p7->mutex)) != 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_INFO, "failed to unlock mutex\n");
		return;
	}

	pnf_nr_p7_pack_and_send_p7_message(pnf_p7, &(ul_node_sync.header), sizeof(ul_node_sync));
	//printf("\nSSent UL Node Sync sfn:%d,slot:%d\n",pnf_p7->sfn,pnf_p7->slot);
}

void pnf_dispatch_p7_message(void *pRecvMsg, int recvMsgLen, pnf_p7_t* pnf_p7,  uint32_t rx_hr_time)
{
	nfapi_p7_message_header_t header;

	// validate the input params
	if(pRecvMsg == NULL || recvMsgLen < 4 || pnf_p7 == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: invalid input params\n", __FUNCTION__);
		return;
	}

	// unpack the message header
	if (nfapi_p7_message_header_unpack(pRecvMsg, recvMsgLen, &header, sizeof(header), &pnf_p7->_public.codec_config) < 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "Unpack message header failed, ignoring\n");
		return;
	}

	// ensure the message is sensible
	if (recvMsgLen < 8 || pRecvMsg == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_WARN, "Invalid message size: %d, ignoring\n", recvMsgLen);
		return;
	}

	switch (header.message_id)
	{
		case NFAPI_DL_NODE_SYNC:
			pnf_handle_dl_node_sync(pRecvMsg, recvMsgLen, pnf_p7, rx_hr_time);
			break;
		case NFAPI_DL_CONFIG_REQUEST:
			pnf_handle_dl_config_request(pRecvMsg, recvMsgLen, pnf_p7);
			break;

		case NFAPI_UL_CONFIG_REQUEST:
			pnf_handle_ul_config_request(pRecvMsg, recvMsgLen, pnf_p7);
			break;

		case NFAPI_HI_DCI0_REQUEST:
			pnf_handle_hi_dci0_request(pRecvMsg, recvMsgLen, pnf_p7);
			break;

		case NFAPI_TX_REQUEST:
			pnf_handle_tx_request(pRecvMsg, recvMsgLen, pnf_p7);
			break;
		
		case NFAPI_LBT_DL_CONFIG_REQUEST:
			pnf_handle_lbt_dl_config_request(pRecvMsg, recvMsgLen, pnf_p7);
			break;

		case NFAPI_UE_RELEASE_REQUEST:
			pnf_handle_ue_release_request(pRecvMsg, recvMsgLen, pnf_p7);
			break;

		default:
			{
				if(header.message_id >= NFAPI_VENDOR_EXT_MSG_MIN &&
				   header.message_id <= NFAPI_VENDOR_EXT_MSG_MAX)
				{
					pnf_handle_p7_vendor_extension(pRecvMsg, recvMsgLen, pnf_p7, header.message_id);
				}
				else
				{
					NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s P7 Unknown message ID %d\n", __FUNCTION__, header.message_id);
				}
			}
			break;
	}
}

void pnf_nr_dispatch_p7_message(void *pRecvMsg, int recvMsgLen, pnf_p7_t* pnf_p7,  uint32_t rx_hr_time)
{
	nfapi_p7_message_header_t header;

	// validate the input params
	if(pRecvMsg == NULL || recvMsgLen < 4 || pnf_p7 == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s: invalid input params\n", __FUNCTION__);
		return;
	}

	// unpack the message header
	if (nfapi_p7_message_header_unpack(pRecvMsg, recvMsgLen, &header, sizeof(header), &pnf_p7->_public.codec_config) < 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "Unpack message header failed, ignoring\n");
		return;
	}

	// ensure the message is sensible
	if (recvMsgLen < 8 || pRecvMsg == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_WARN, "Invalid message size: %d, ignoring\n", recvMsgLen);
		return;
	}

	switch (header.message_id)
	{
		case NFAPI_NR_PHY_MSG_TYPE_DL_NODE_SYNC:
			pnf_nr_handle_dl_node_sync(pRecvMsg, recvMsgLen, pnf_p7, rx_hr_time);
			break;
		case NFAPI_NR_PHY_MSG_TYPE_DL_TTI_REQUEST:
			pnf_handle_dl_tti_request(pRecvMsg, recvMsgLen, pnf_p7);
			break;
		case NFAPI_NR_PHY_MSG_TYPE_UL_TTI_REQUEST:
			pnf_handle_ul_tti_request(pRecvMsg, recvMsgLen, pnf_p7);
			break;
		case NFAPI_NR_PHY_MSG_TYPE_UL_DCI_REQUEST:
			pnf_handle_ul_dci_request(pRecvMsg, recvMsgLen, pnf_p7);
			break;
		case NFAPI_NR_PHY_MSG_TYPE_TX_DATA_REQUEST:
			pnf_handle_tx_data_request(pRecvMsg, recvMsgLen, pnf_p7);
			break;
		default:
			{
				if(header.message_id >= NFAPI_VENDOR_EXT_MSG_MIN &&
				   header.message_id <= NFAPI_VENDOR_EXT_MSG_MAX)
				{
					pnf_handle_p7_vendor_extension(pRecvMsg, recvMsgLen, pnf_p7, header.message_id);
				}
				else
				{
					NFAPI_TRACE(NFAPI_TRACE_ERROR, "%s P7 Unknown message ID %d\n", __FUNCTION__, header.message_id);
				}
			}
			break;
	}
}

void pnf_handle_p7_message(void *pRecvMsg, int recvMsgLen, pnf_p7_t* pnf_p7,  uint32_t rx_hr_time)
{
	nfapi_p7_message_header_t messageHeader;

	// validate the input params
	if(pRecvMsg == NULL || recvMsgLen < 4 || pnf_p7 == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "pnf_handle_p7_message: invalid input params (%p %d %p)\n", pRecvMsg, recvMsgLen, pnf_p7);
		return;
	}

	// unpack the message header
	if (nfapi_p7_message_header_unpack(pRecvMsg, recvMsgLen, &messageHeader, sizeof(nfapi_p7_message_header_t), &pnf_p7->_public.codec_config) < 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "Unpack message header failed, ignoring\n");
		return;
	}

	uint8_t m = NFAPI_P7_GET_MORE(messageHeader.m_segment_sequence);
	uint8_t sequence_num = NFAPI_P7_GET_SEQUENCE(messageHeader.m_segment_sequence);
	uint8_t segment_num = NFAPI_P7_GET_SEGMENT(messageHeader.m_segment_sequence);

	if(pnf_p7->_public.checksum_enabled)
	{
		uint32_t checksum = nfapi_p7_calculate_checksum(pRecvMsg, recvMsgLen);
		if(checksum != messageHeader.checksum)
		{
			NFAPI_TRACE(NFAPI_TRACE_ERROR, "Checksum verification failed %d %d\n", checksum, messageHeader.checksum);
			return;
		}
	}

	if(m == 0 && segment_num == 0)
	{
		// we have a complete message
		// ensure the message is sensible
		if (recvMsgLen < 8 || pRecvMsg == NULL)
		{
			NFAPI_TRACE(NFAPI_TRACE_WARN, "Invalid message size: %d, ignoring\n", recvMsgLen);
			return;
		}

		pnf_dispatch_p7_message(pRecvMsg, recvMsgLen, pnf_p7, rx_hr_time);
	}
	else
	{
		pnf_p7_rx_message_t* rx_msg = pnf_p7_rx_reassembly_queue_add_segment(pnf_p7, &(pnf_p7->reassembly_queue), rx_hr_time, sequence_num, segment_num, m, pRecvMsg, recvMsgLen);

		if(rx_msg->num_segments_received == rx_msg->num_segments_expected)
		{
			// send the buffer on
			uint16_t i = 0;
			uint16_t length = 0;
			for(i = 0; i < rx_msg->num_segments_expected; ++i)
			{
				length += rx_msg->segments[i].length - (i > 0 ? NFAPI_P7_HEADER_LENGTH : 0);
			}
			
			if(pnf_p7->reassemby_buffer_size < length)
			{
				pnf_p7_free(pnf_p7, pnf_p7->reassemby_buffer);
				pnf_p7->reassemby_buffer = 0;
			}

			if(pnf_p7->reassemby_buffer == 0)
			{
				NFAPI_TRACE(NFAPI_TRACE_NOTE, "Resizing PNF_P7 Reassembly buffer %d->%d\n", pnf_p7->reassemby_buffer_size, length);
				pnf_p7->reassemby_buffer = (uint8_t*)pnf_p7_malloc(pnf_p7, length);

				if(pnf_p7->reassemby_buffer == 0)
				{
					NFAPI_TRACE(NFAPI_TRACE_NOTE, "Failed to allocate PNF_P7 reassemby buffer len:%d\n", length);
					return;
				}
                                memset(pnf_p7->reassemby_buffer, 0, length);
				pnf_p7->reassemby_buffer_size = length;
			}
			
			uint16_t offset = 0;
			for(i = 0; i < rx_msg->num_segments_expected; ++i)
			{
				if(i == 0)
				{
					memcpy(pnf_p7->reassemby_buffer, rx_msg->segments[i].buffer, rx_msg->segments[i].length);
					offset += rx_msg->segments[i].length;
				}
				else
				{
					memcpy(pnf_p7->reassemby_buffer + offset, rx_msg->segments[i].buffer + NFAPI_P7_HEADER_LENGTH, rx_msg->segments[i].length - NFAPI_P7_HEADER_LENGTH);
					offset += rx_msg->segments[i].length - NFAPI_P7_HEADER_LENGTH;
				}
			}

			
			pnf_dispatch_p7_message(pnf_p7->reassemby_buffer, length, pnf_p7, rx_msg->rx_hr_time);


			// delete the structure
			pnf_p7_rx_reassembly_queue_remove_msg(pnf_p7, &(pnf_p7->reassembly_queue), rx_msg);
		}
	}

	pnf_p7_rx_reassembly_queue_remove_old_msgs(pnf_p7, &(pnf_p7->reassembly_queue), rx_hr_time, 1000);
	
}

void pnf_nr_handle_p7_message(void *pRecvMsg, int recvMsgLen, pnf_p7_t* pnf_p7,  uint32_t rx_hr_time)
{
	nfapi_p7_message_header_t messageHeader;

	// validate the input params
	if(pRecvMsg == NULL || recvMsgLen < 4 || pnf_p7 == NULL)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "pnf_handle_p7_message: invalid input params (%p %d %p)\n", pRecvMsg, recvMsgLen, pnf_p7);
		return;
	}

	// unpack the message header
	if (nfapi_p7_message_header_unpack(pRecvMsg, recvMsgLen, &messageHeader, sizeof(nfapi_p7_message_header_t), &pnf_p7->_public.codec_config) < 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "Unpack message header failed, ignoring\n");
		return;
	}

	uint8_t m = NFAPI_P7_GET_MORE(messageHeader.m_segment_sequence);
	uint8_t sequence_num = NFAPI_P7_GET_SEQUENCE(messageHeader.m_segment_sequence);
	uint8_t segment_num = NFAPI_P7_GET_SEGMENT(messageHeader.m_segment_sequence);

	if(pnf_p7->_public.checksum_enabled)
	{
		uint32_t checksum = nfapi_p7_calculate_checksum(pRecvMsg, recvMsgLen);
		if(checksum != messageHeader.checksum)
		{
			NFAPI_TRACE(NFAPI_TRACE_ERROR, "Checksum verification failed %d %d\n", checksum, messageHeader.checksum);
			return;
		}
	}

	if(m == 0 && segment_num == 0)
	{
		// we have a complete message
		// ensure the message is sensible
		if (recvMsgLen < 8 || pRecvMsg == NULL)
		{
			NFAPI_TRACE(NFAPI_TRACE_WARN, "Invalid message size: %d, ignoring\n", recvMsgLen);
			return;
		}

		pnf_nr_dispatch_p7_message(pRecvMsg, recvMsgLen, pnf_p7, rx_hr_time);
	}
	else
	{
		pnf_p7_rx_message_t* rx_msg = pnf_p7_rx_reassembly_queue_add_segment(pnf_p7, &(pnf_p7->reassembly_queue), rx_hr_time, sequence_num, segment_num, m, pRecvMsg, recvMsgLen);

		if(rx_msg->num_segments_received == rx_msg->num_segments_expected)
		{
			// send the buffer on
			uint16_t i = 0;
			uint16_t length = 0;
			for(i = 0; i < rx_msg->num_segments_expected; ++i)
			{
				length += rx_msg->segments[i].length - (i > 0 ? NFAPI_P7_HEADER_LENGTH : 0);
			}
			
			if(pnf_p7->reassemby_buffer_size < length)
			{
				pnf_p7_free(pnf_p7, pnf_p7->reassemby_buffer);
				pnf_p7->reassemby_buffer = 0;
			}

			if(pnf_p7->reassemby_buffer == 0)
			{
				NFAPI_TRACE(NFAPI_TRACE_NOTE, "Resizing PNF_P7 Reassembly buffer %d->%d\n", pnf_p7->reassemby_buffer_size, length);
				pnf_p7->reassemby_buffer = (uint8_t*)pnf_p7_malloc(pnf_p7, length);

				if(pnf_p7->reassemby_buffer == 0)
				{
					NFAPI_TRACE(NFAPI_TRACE_NOTE, "Failed to allocate PNF_P7 reassemby buffer len:%d\n", length);
					return;
				}
                                memset(pnf_p7->reassemby_buffer, 0, length);
				pnf_p7->reassemby_buffer_size = length;
			}
			
			uint16_t offset = 0;
			for(i = 0; i < rx_msg->num_segments_expected; ++i)
			{
				if(i == 0)
				{
					memcpy(pnf_p7->reassemby_buffer, rx_msg->segments[i].buffer, rx_msg->segments[i].length);
					offset += rx_msg->segments[i].length;
				}
				else
				{
					memcpy(pnf_p7->reassemby_buffer + offset, rx_msg->segments[i].buffer + NFAPI_P7_HEADER_LENGTH, rx_msg->segments[i].length - NFAPI_P7_HEADER_LENGTH);
					offset += rx_msg->segments[i].length - NFAPI_P7_HEADER_LENGTH;
				}
			}

			
			pnf_nr_dispatch_p7_message(pnf_p7->reassemby_buffer, length, pnf_p7, rx_msg->rx_hr_time);


			// delete the structure
			pnf_p7_rx_reassembly_queue_remove_msg(pnf_p7, &(pnf_p7->reassembly_queue), rx_msg);
		}
	}

	pnf_p7_rx_reassembly_queue_remove_old_msgs(pnf_p7, &(pnf_p7->reassembly_queue), rx_hr_time, 1000);
	
}


void pnf_nfapi_p7_read_dispatch_message(pnf_p7_t* pnf_p7, uint32_t now_hr_time)
{
	int recvfrom_result = 0;
	struct sockaddr_in remote_addr;
	socklen_t remote_addr_size = sizeof(remote_addr);

	do
	{
		// peek the header
		uint8_t header_buffer[NFAPI_P7_HEADER_LENGTH];
		recvfrom_result = recvfrom(pnf_p7->p7_sock, header_buffer, NFAPI_P7_HEADER_LENGTH, MSG_DONTWAIT | MSG_PEEK, (struct sockaddr*)&remote_addr, &remote_addr_size);

		if(recvfrom_result > 0)
		{
			// get the segment size
			nfapi_p7_message_header_t header;
			nfapi_p7_message_header_unpack(header_buffer, NFAPI_P7_HEADER_LENGTH, &header, 34, 0);

			// resize the buffer if we have a large segment
			if(header.message_length > pnf_p7->rx_message_buffer_size)
			{
				NFAPI_TRACE(NFAPI_TRACE_NOTE, "reallocing rx buffer %d\n", header.message_length); 
				pnf_p7->rx_message_buffer = realloc(pnf_p7->rx_message_buffer, header.message_length);
				pnf_p7->rx_message_buffer_size = header.message_length;
			}

			// read the segment
			recvfrom_result = recvfrom(pnf_p7->p7_sock, pnf_p7->rx_message_buffer, pnf_p7->rx_message_buffer_size,
                                                   MSG_DONTWAIT | MSG_TRUNC, (struct sockaddr*)&remote_addr, &remote_addr_size);

		now_hr_time = pnf_get_current_time_hr(); //moved to here - get closer timestamp???

			if(recvfrom_result > 0)
			{
				if (recvfrom_result != header.message_length)
				{
					NFAPI_TRACE(NFAPI_TRACE_ERROR, "(%d) Received unexpected number of bytes. %d != %d",
                                                    __LINE__, recvfrom_result, header.message_length);
					break;
				}
				pnf_handle_p7_message(pnf_p7->rx_message_buffer, recvfrom_result, pnf_p7, now_hr_time);
				//printf("\npnf_handle_p7_message sfn=%d,slot=%d\n",pnf_p7->sfn,pnf_p7->slot);
			}
		}
		else if(recvfrom_result == 0)
		{
			// recv zero length message
			recvfrom_result = recvfrom(pnf_p7->p7_sock, header_buffer, 0, MSG_DONTWAIT, (struct sockaddr*)&remote_addr, &remote_addr_size);
		}

		if(recvfrom_result == -1)
		{
			if(errno == EAGAIN || errno == EWOULDBLOCK)
			{
				// return to the select
				//NFAPI_TRACE(NFAPI_TRACE_WARN, "%s recvfrom would block :%d\n", __FUNCTION__, errno);
			}
			else
			{
				NFAPI_TRACE(NFAPI_TRACE_WARN, "%s recvfrom failed errno:%d\n", __FUNCTION__, errno);
			}
		}

		// need to update the time as we would only use the value from the
		// select
	}
	while(recvfrom_result > 0);
}

void pnf_nr_nfapi_p7_read_dispatch_message(pnf_p7_t* pnf_p7, uint32_t now_hr_time)
{
	int recvfrom_result = 0;
	struct sockaddr_in remote_addr;
	socklen_t remote_addr_size = sizeof(remote_addr);
	remote_addr.sin_family = 2; //hardcoded
	do
	{
		// peek the header
		uint8_t header_buffer[NFAPI_P7_HEADER_LENGTH];
		recvfrom_result = recvfrom(pnf_p7->p7_sock, header_buffer, NFAPI_P7_HEADER_LENGTH, MSG_DONTWAIT | MSG_PEEK, (struct sockaddr*)&remote_addr, &remote_addr_size);
		if(recvfrom_result > 0)
		{
			// get the segment size
			nfapi_p7_message_header_t header;
			nfapi_p7_message_header_unpack(header_buffer, NFAPI_P7_HEADER_LENGTH, &header, 34, 0);

			// resize the buffer if we have a large segment
			if(header.message_length > pnf_p7->rx_message_buffer_size)
			{
				NFAPI_TRACE(NFAPI_TRACE_NOTE, "reallocing rx buffer %d\n", header.message_length); 
				pnf_p7->rx_message_buffer = realloc(pnf_p7->rx_message_buffer, header.message_length);
				pnf_p7->rx_message_buffer_size = header.message_length;
			}

			// read the segment
			recvfrom_result = recvfrom(pnf_p7->p7_sock, pnf_p7->rx_message_buffer, header.message_length, MSG_DONTWAIT, (struct sockaddr*)&remote_addr, &remote_addr_size);

		now_hr_time = pnf_get_current_time_hr(); //moved to here - get closer timestamp???

			if(recvfrom_result > 0)
			{
				pnf_nr_handle_p7_message(pnf_p7->rx_message_buffer, recvfrom_result, pnf_p7, now_hr_time);
				//printf("\npnf_handle_p7_message sfn=%d,slot=%d\n",pnf_p7->sfn,pnf_p7->slot);
			}
		}
		else if(recvfrom_result == 0)
		{
			// recv zero length message
			recvfrom_result = recvfrom(pnf_p7->p7_sock, header_buffer, 0, MSG_DONTWAIT, (struct sockaddr*)&remote_addr, &remote_addr_size);
		}

		if(recvfrom_result == -1)
		{
			if(errno == EAGAIN || errno == EWOULDBLOCK)
			{
				// return to the select
				//NFAPI_TRACE(NFAPI_TRACE_WARN, "%s recvfrom would block :%d\n", __FUNCTION__, errno);
			}
			else
			{
				NFAPI_TRACE(NFAPI_TRACE_WARN, "%s recvfrom failed errno:%d\n", __FUNCTION__, errno);
			}
		}

		// need to update the time as we would only use the value from the
		// select
	}
	while(recvfrom_result > 0);
}


int pnf_p7_message_pump(pnf_p7_t* pnf_p7)
{

	// initialize the mutex lock
	if(pthread_mutex_init(&(pnf_p7->mutex), NULL) != 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "After P7 mutex init: %d\n", errno);
		return -1;
	}
	
	if(pthread_mutex_init(&(pnf_p7->pack_mutex), NULL) != 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "After P7 mutex init: %d\n", errno);
		return -1;
	}	

	// create the pnf p7 socket
	if ((pnf_p7->p7_sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "After P7 socket errno: %d\n", errno);
		return -1;
	}
	NFAPI_TRACE(NFAPI_TRACE_INFO, "PNF P7 socket created (%d)...\n", pnf_p7->p7_sock);

	// configure the UDP socket options
	int reuseaddr_enable = 1;
	if (setsockopt(pnf_p7->p7_sock, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_enable, sizeof(int)) < 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "PNF P7 setsockopt (SOL_SOCKET, SO_REUSEADDR) failed  errno: %d\n", errno);
		return -1;
	}

/*
	int reuseport_enable = 1;
	if (setsockopt(pnf_p7->p7_sock, SOL_SOCKET, SO_REUSEPORT, &reuseport_enable, sizeof(int)) < 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "PNF P7 setsockopt (SOL_SOCKET, SO_REUSEPORT) failed  errno: %d\n", errno);
		return -1;
	}
*/
		
	int iptos_value = FAPI2_IP_DSCP << 2;
	if (setsockopt(pnf_p7->p7_sock, IPPROTO_IP, IP_TOS, &iptos_value, sizeof(iptos_value)) < 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "PNF P7 setsockopt (IPPROTO_IP, IP_TOS) failed errno: %d\n", errno);
		return -1;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(pnf_p7->_public.local_p7_port);

	if(pnf_p7->_public.local_p7_addr == 0)
	{
		addr.sin_addr.s_addr = INADDR_ANY;
	}
	else
	{
		//addr.sin_addr.s_addr = inet_addr(pnf_p7->_public.local_p7_addr);
		if(inet_aton(pnf_p7->_public.local_p7_addr, &addr.sin_addr) == -1)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "inet_aton failed\n");
		}
	}


	NFAPI_TRACE(NFAPI_TRACE_INFO, "PNF P7 binding %d too %s:%d\n", pnf_p7->p7_sock, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
	if (bind(pnf_p7->p7_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "PNF_P7 bind error fd:%d errno: %d\n", pnf_p7->p7_sock, errno);
		return -1;
	}
	NFAPI_TRACE(NFAPI_TRACE_INFO, "PNF P7 bind succeeded...\n");

	while(pnf_p7->terminate == 0)
	{
		fd_set rfds;
		int selectRetval = 0;

		// select on a timeout and then get the message
		FD_ZERO(&rfds);
		FD_SET(pnf_p7->p7_sock, &rfds);

		struct timeval timeout;
		timeout.tv_sec = 1;
		timeout.tv_usec = 0;

		selectRetval = select(pnf_p7->p7_sock+1, &rfds, NULL, NULL, &timeout);

		uint32_t now_hr_time = pnf_get_current_time_hr();

		
		

		if(selectRetval == 0)
		{	
			// timeout
			continue;
		}
		else if (selectRetval == -1 && (errno == EINTR))
		{
			// interrupted by signal
			NFAPI_TRACE(NFAPI_TRACE_WARN, "PNF P7 Signal Interrupt %d\n", errno);
			continue;
		}
		else if (selectRetval == -1)
		{
			NFAPI_TRACE(NFAPI_TRACE_WARN, "PNF P7 select() failed\n");
			sleep(1);
			continue;
		}

		if(FD_ISSET(pnf_p7->p7_sock, &rfds)) 

		{
			pnf_nfapi_p7_read_dispatch_message(pnf_p7, now_hr_time);
		}
	}
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "PNF_P7 Terminating..\n");

	// close the connection and socket
	if (close(pnf_p7->p7_sock) < 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "close failed errno: %d\n", errno);
	}

	if(pthread_mutex_destroy(&(pnf_p7->pack_mutex)) != 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "mutex destroy failed errno: %d\n", errno);
	}

	if(pthread_mutex_destroy(&(pnf_p7->mutex)) != 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "mutex destroy failed errno: %d\n", errno);
	}

	return 0;
}

struct timespec pnf_timespec_add(struct timespec lhs, struct timespec rhs)
{
	struct timespec result;

	result.tv_sec = lhs.tv_sec + rhs.tv_sec;
	result.tv_nsec = lhs.tv_nsec + rhs.tv_nsec;

	if(result.tv_nsec > 1e9)
	{
		result.tv_sec++;
		result.tv_nsec-= 1e9;
	}

	return result;
}

struct timespec pnf_timespec_sub(struct timespec lhs, struct timespec rhs)
{
	struct timespec result;
	if ((lhs.tv_nsec-rhs.tv_nsec)<0) 
	{
		result.tv_sec = lhs.tv_sec-rhs.tv_sec-1;
		result.tv_nsec = 1000000000+lhs.tv_nsec-rhs.tv_nsec;
	} 
	else 
	{
		result.tv_sec = lhs.tv_sec-rhs.tv_sec;
		result.tv_nsec = lhs.tv_nsec-rhs.tv_nsec;
	}
	return result;
}

int pnf_nr_p7_message_pump(pnf_p7_t* pnf_p7)
{

	// initialize the mutex lock
	if(pthread_mutex_init(&(pnf_p7->mutex), NULL) != 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "After P7 mutex init: %d\n", errno);
		return -1;
	}
	
	if(pthread_mutex_init(&(pnf_p7->pack_mutex), NULL) != 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "After P7 mutex init: %d\n", errno);
		return -1;
	}	

	// create the pnf p7 socket
	if ((pnf_p7->p7_sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "After P7 socket errno: %d\n", errno);
		return -1;
	}
	NFAPI_TRACE(NFAPI_TRACE_INFO, "PNF P7 socket created (%d)...\n", pnf_p7->p7_sock);

	// configure the UDP socket options
	int reuseaddr_enable = 1;
	if (setsockopt(pnf_p7->p7_sock, SOL_SOCKET, SO_REUSEADDR, &reuseaddr_enable, sizeof(int)) < 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "PNF P7 setsockopt (SOL_SOCKET, SO_REUSEADDR) failed  errno: %d\n", errno);
		return -1;
	}

/*
	int reuseport_enable = 1;
	if (setsockopt(pnf_p7->p7_sock, SOL_SOCKET, SO_REUSEPORT, &reuseport_enable, sizeof(int)) < 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "PNF P7 setsockopt (SOL_SOCKET, SO_REUSEPORT) failed  errno: %d\n", errno);
		return -1;
	}
*/
		
	int iptos_value = FAPI2_IP_DSCP << 2;
	if (setsockopt(pnf_p7->p7_sock, IPPROTO_IP, IP_TOS, &iptos_value, sizeof(iptos_value)) < 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "PNF P7 setsockopt (IPPROTO_IP, IP_TOS) failed errno: %d\n", errno);
		return -1;
	}

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(pnf_p7->_public.local_p7_port);

	if(pnf_p7->_public.local_p7_addr == 0)
	{
		addr.sin_addr.s_addr = INADDR_ANY;
	}
	else
	{
		//addr.sin_addr.s_addr = inet_addr(pnf_p7->_public.local_p7_addr);
		if(inet_aton(pnf_p7->_public.local_p7_addr, &addr.sin_addr) == -1)
		{
			NFAPI_TRACE(NFAPI_TRACE_INFO, "inet_aton failed\n");
		}
	}


	NFAPI_TRACE(NFAPI_TRACE_INFO, "PNF P7 binding %d too %s:%d\n", pnf_p7->p7_sock, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
	if (bind(pnf_p7->p7_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "PNF_P7 bind error fd:%d errno: %d\n", pnf_p7->p7_sock, errno);
		return -1;
	}
	NFAPI_TRACE(NFAPI_TRACE_INFO, "PNF P7 bind succeeded...\n");

	//Initializaing timing structures needed for slot ticking 

	struct timespec slot_start;
	clock_gettime(CLOCK_MONOTONIC, &slot_start);

	struct timespec pselect_start;

	struct timespec slot_duration;
	slot_duration.tv_sec = 0;
	slot_duration.tv_nsec = 0.5e6;

	//Infinite loop 

	while(pnf_p7->terminate == 0)
	{
		fd_set rfds;
		int selectRetval = 0;

		// select on a timeout and then get the message
		FD_ZERO(&rfds);
		FD_SET(pnf_p7->p7_sock, &rfds);

		struct timespec timeout;
		timeout.tv_sec = 100;
		timeout.tv_nsec = 0;
		clock_gettime(CLOCK_MONOTONIC, &pselect_start);

		//setting the timeout

		if((pselect_start.tv_sec > slot_start.tv_sec) || ((pselect_start.tv_sec == slot_start.tv_sec) && (pselect_start.tv_nsec > slot_start.tv_nsec)))
		{
			// overran the end of the subframe we do not want to wait
			timeout.tv_sec = 0;
			timeout.tv_nsec = 0;

			//struct timespec overrun = pnf_timespec_sub(pselect_start, sf_start);
			//NFAPI_TRACE(NFAPI_TRACE_INFO, "Subframe overrun detected of %d.%d running to catchup\n", overrun.tv_sec, overrun.tv_nsec);
		}
		else
		{
			// still time before the end of the subframe wait
			timeout = pnf_timespec_sub(slot_start, pselect_start);
		}

		selectRetval = pselect(pnf_p7->p7_sock+1, &rfds, NULL, NULL, &timeout, NULL);

		uint32_t now_hr_time = pnf_get_current_time_hr();

		
		

		if(selectRetval == 0)
		{	
			// timeout

			//update slot start timing
			slot_start = pnf_timespec_add(slot_start, slot_duration);

			//increment sfn/slot
			if (++pnf_p7->slot == 20)
                        {
                                pnf_p7->slot = 0;
                                pnf_p7->sfn = (pnf_p7->sfn + 1) % 1024;
                        }

			continue;
		}
		else if (selectRetval == -1 && (errno == EINTR))
		{
			// interrupted by signal
			NFAPI_TRACE(NFAPI_TRACE_WARN, "PNF P7 Signal Interrupt %d\n", errno);
			continue;
		}
		else if (selectRetval == -1)
		{
			NFAPI_TRACE(NFAPI_TRACE_WARN, "PNF P7 select() failed\n");
			sleep(1);
			continue;
		}

		if(FD_ISSET(pnf_p7->p7_sock, &rfds)) 

		{
			pnf_nr_nfapi_p7_read_dispatch_message(pnf_p7, now_hr_time); 
		}
	}
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "PNF_P7 Terminating..\n");

	// close the connection and socket
	if (close(pnf_p7->p7_sock) < 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "close failed errno: %d\n", errno);
	}

	if(pthread_mutex_destroy(&(pnf_p7->pack_mutex)) != 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "mutex destroy failed errno: %d\n", errno);
	}

	if(pthread_mutex_destroy(&(pnf_p7->mutex)) != 0)
	{
		NFAPI_TRACE(NFAPI_TRACE_ERROR, "mutex destroy failed errno: %d\n", errno);
	}

	return 0;
}