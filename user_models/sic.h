/**
 *  \file   sic.h
 *  \brief  Sic Definitions
 *  \author Adam Xu
 *  \date   2018
 **/
#ifndef __sic_public__
#define __sic_public__

#define MIN_SIC_REMAIN 0
#define MIN_SIC_ITERATION 2
#define DEFAULT_SIC_THRESHOLD 1

typedef enum
{
	ADAM_ERROR_NO_ERROR = 0,
	ADAM_ERROR_DEFAULT,
	ADAM_ERROR_UNEXPECTED_INPUT,
	ADAM_ERROR_NO_MEMORY
} adam_error_code_t;

typedef struct sic_signal_t
{
	packetid_t id;
	double rxdBm;

	// begin time
	//uint64_t clock0;
	//end time
	uint64_t clock1;
	
	struct sic_signal_t* signal_pre_endtime;
	struct sic_signal_t* signal_next_endtime;
	
	struct sic_signal_t* signal_higher_power;
	struct sic_signal_t* signal_lower_power;
}sic_signal_t;


/* ************************************************** */
/* ************************************************** */
struct _sic_802_11_header {
	int src;
	int dst;
	int type;
};
struct _sic_802_11_rts_header {
	uint64_t nav;
	int size;
	char padding[8];
};
struct _sic_802_11_cts_header {
	uint64_t nav;
	char padding[6];
};
struct _sic_802_11_data_header {
	uint64_t nav;
	int size;
	int priority;
	char padding[22];
};
struct _sic_802_11_ack_header {
	char padding[14];
};

// <-RF00000000-AdamXu-2018/05/22-if a packet is decodable.
// packet_id: id of packet
// base_noise: environment noise
// sic_threshold: SINR threshold
// return: 1 decodable, 0 not
int adam_Is_Packet_Decodable(call_t *c, packetid_t id, double base_noise, double sic_threshold);
// ->RF00000000-AdamXu

// <-RF00000000-AdamXu-2018/05/22-insert new signal to canditate buffer by time.
// sic_signal: signal to insert
// return: error code
int adam_Insert_SIgnal2Candidate_Time(call_t *c, sic_signal_t* sic_signal);
// ->RF00000000-AdamXu

// <-RF00000000-AdamXu-2018/05/22-insert new signal to canditate buffer by power.
// sic_signal: signal to insert
// return: error code
int adam_Insert_SIgnal2Candidate_Power(call_t *c, sic_signal_t* sic_signal);
// ->RF00000000-AdamXu

// <-RF00000000-AdamXu-2018/05/22-insert new signal to canditate buffer.
// return: error code
int adam_Update_Candidate(call_t *c);
// ->RF00000000-AdamXu

#endif //__radio_public__
