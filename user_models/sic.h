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


/* ************************************************** */
/* ************************************************** */
#define ADAM_NO_SENSING
//#define ADAM_PRIORITY_TEST
#define ADAM_SLOT_CSMA
#define ADAM_HIGH_PRIOTITY_RATIO 0.2
#define ADAM_HIGH_POWER_RATIO 2.1
#define ADAM_HIGH_POWER_DBM_GAIN (log10(ADAM_HIGH_POWER_RATIO))

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
#ifdef ADAM_NO_SENSING
	// 0: low; 1: high
	int priority_type;
#endif//ADAM_NO_SENSING
	char padding[8];
};
struct _sic_802_11_cts_header {
	uint64_t nav;
#ifdef ADAM_NO_SENSING
	// 0: disabled; 1: high; 2: low; 3:data;
	int priority_type;
	int node_allowed;
#endif//ADAM_NO_SENSING
	char padding[6];
};
struct _sic_802_11_data_header {
#ifdef ADAM_TEST
	uint64_t mac_start;
#endif//ADAM_TEST
	uint64_t nav;
	int size;
	//int priority;
	char padding[22];
};
struct _sic_802_11_ack_header {
	char padding[14];
#ifdef ADAM_NO_SENSING
	int received_high;
	int received_low;
#endif//ADAM_NO_SENSING
};

// <-RF00000000-AdamXu-2018/05/22-if a packet is decodable.
// base_noise: environment noise
// return: interference plus noise by mw
double adam_Get_IN_MW(call_t *c, double base_noise);
// ->RF00000000-AdamXu

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
