/**
 *  \file   sic_priority_mac.c
 *  \brief  MAC sic priority protocol
 *  \author xuyida
 *  \date   2018
 **/
#include <stdio.h>

#include <include/modelutils.h>

#include <sic.h>

/**
 * TODO: 
 *  - 
 *  - 
 *  - 
 **/
#define SPEED_LIGHT 3000


/* ************************************************** */
/* ************************************************** */
#define STATE_IDLE			1
#define STATE_BACKOFF		2
#define STATE_RTS			3
#define STATE_TIMEOUT		4
#define STATE_CTS		    5
#define STATE_CTS_TIMEOUT	6
#define STATE_DATA			7
#define STATE_BROADCAST		8
#define STATE_ACK			9
#define STATE_DONE			10
#define STATE_BROAD_DONE	11

#define STATE_CONTENTION_BEGIN			12
#define STATE_CONTENTION					13
#define STATE_CONTENTION_WAITING_DATA	14


/* ************************************************** */
/* ************************************************** */
#define macMinDIFSPeriod      50000   
#define macMinSIFSPeriod      10000
#define macMinBE              5     /* 32 slots */
#define macMaxBE              10    /* 1024 slots */
#define macMaxCSMARetries     7     /* 7 trials before dropping */
#define aUnitBackoffPeriod    20000
#define EDThresholdMin        -74

#define MAX_CONTENTION_WINDOW_HIGH	3	/* 4 slots */
#define MAX_CONTENTION_WINDOW_LOW	3	/* 8 slots */

#define RTS_TIME							((sizeof(struct _sic_802_11_header) + sizeof(struct _sic_802_11_rts_header)) * 8 * radio_get_Tb(&c0))
#define CTS_TIME							((sizeof(struct _sic_802_11_header) + sizeof(struct _sic_802_11_cts_header)) * 8 * radio_get_Tb(&c0))
#define MIN_CONTENTION_BACKOFF_PERIOD  	(RTS_TIME + CTS_TIME)
/* ************************************************** */
/* ************************************************** */
#define RTS_TYPE			1
#define CTS_TYPE			2
#define DATA_TYPE			3
#define ACK_TYPE			4
#define BROADCAST_TYPE      5

#define CONTENTION_BEGIN_TYPE	6
#define CONTENTION_END_TYPE	7
#define CONTENTION_TYPE		8

/* ************************************************** */
/* ************************************************** */
struct nodedata {
	uint64_t clock;
	int state;
	int state_pending;
	int dst;
	int size;

	uint64_t backoff;
	int backoff_suspended;
	int NB;
	int BE;

	uint64_t nav;
	int rts_threshold;

	void *packets;
	packet_t *txbuf;

	int cs;
	int cca;
	double EDThreshold;
	double HighThreshold_mw;
	double base_power_tx;
#ifdef ADAM_NO_SENSING
	double priority_ratio;
	// 0: idle; -1: wait for high; -2: wait for low; -3: wait for data;
	int sink_state;
	// 0: idle; -1: sent high rts; -2: sent low rts; -3: sent data;
	int source_state;
	// 0: prohibit; 1: low; 2: high
	int power_type_data;
	int allowed_high;
	int allowed_low;
#endif//ADAM_NO_SENSING
;
};

struct entitydata {
    int maxCSMARetries;
};


/* ************************************************** */
/* ************************************************** */
model_t model =  {
    "802.11 DCF mac module",
    "Guillaume Chelius and Elyes Ben Hamida",
    "0.1",
    MODELTYPE_MAC, 
    {NULL, 0}
};


/* ************************************************** */
/* ************************************************** */
static int s_sent_mac = 0;
static int s_received_mac = 0;

/* ************************************************** */
/* ************************************************** */
int init(call_t *c, void *params) {
    struct entitydata *entitydata = malloc(sizeof(struct entitydata));
    param_t *param;

    /* default values */
    entitydata->maxCSMARetries = macMaxCSMARetries;

    /* get parameters */
    das_init_traverse(params);
    while ((param = (param_t *) das_traverse(params)) != NULL) {
        if (!strcmp(param->key, "retry")) {
            if (get_param_integer(param->value, &(entitydata->maxCSMARetries))) {
                goto error;
            }
        }
    }

    set_entity_private_data(c, entitydata);
    return 0;

 error:
    free(entitydata);
    return -1;
}

int destroy(call_t *c) {
    free(get_entity_private_data(c));
    return 0;
}


/* ************************************************** */
/* ************************************************** */
int setnode(call_t *c, void *params) {
	struct nodedata *nodedata = malloc(sizeof(struct nodedata));
	param_t *param;

	/* default values */
	nodedata->rts_threshold = 500;
	nodedata->clock = 0;
	nodedata->state = STATE_IDLE;
	nodedata->state_pending = STATE_IDLE;
	nodedata->nav = 0;
	nodedata->cca = 1;
	nodedata->cs = 1;
	nodedata->EDThreshold = EDThresholdMin;
	nodedata->HighThreshold_mw = -1;

	/* Init packets buffer */
	nodedata->packets = das_create();
	nodedata->txbuf = NULL;
//#ifdef ADAM_PRIORITY_TEST
	nodedata->base_power_tx = 0;
//#endif//ADAM_PRIORITY_TEST
#ifdef ADAM_NO_SENSING
	nodedata->priority_ratio = ADAM_HIGH_PRIOTITY_RATIO;
	nodedata->sink_state = 0;
	nodedata->source_state = 0;
	nodedata->power_type_data = 0;
	nodedata->allowed_high = -1;
	nodedata->allowed_low = -1;
#endif//ADAM_NO_SENSING

    /* get params */
    das_init_traverse(params);
    while ((param = (param_t *) das_traverse(params)) != NULL) {
       if (!strcmp(param->key, "cca-threshold")) {
            if (get_param_double(param->value, &(nodedata->EDThreshold))) {
                goto error;
            }
        }
        if (!strcmp(param->key, "cca")) {
            if (get_param_integer(param->value, &(nodedata->cca))) {
                goto error;
            }
        }
        if (!strcmp(param->key, "cs")) {
            if (get_param_integer(param->value, &(nodedata->cs))) {
                goto error;
            }
        }
        if (!strcmp(param->key, "rts-threshold")) {
            if (get_param_integer(param->value, &(nodedata->rts_threshold))) {
                goto error;
            }
        }
        if (!strcmp(param->key, "priority-ratio")) {
            if (get_param_double(param->value, &(nodedata->priority_ratio))) {
                goto error;
            }
        }
    }

	set_node_private_data(c, nodedata);
	//PRINT_MAC("MAC c->node=%d\n", c->node);
	return 0;

 error:
    free(nodedata);
    return -1;
}

int unsetnode(call_t *c) {
    struct nodedata *nodedata = get_node_private_data(c);
    packet_t *packet;
    while ((packet = (packet_t *) das_pop(nodedata->packets)) != NULL) {
        packet_dealloc(packet);
    }
    das_destroy(nodedata->packets);    
    if (nodedata->txbuf) {
        packet_dealloc(nodedata->txbuf);
    }
    free(nodedata);
    return 0;
}


/* ************************************************** */
/* ************************************************** */
int bootstrap(call_t *c) {
    return 0;
}

int ioctl(call_t *c, int option, void *in, void **out) {
    return 0;
}


/* ************************************************** */
// for low power transmission, low threshold block backoff
// for high power transmission, high threshold block backoff
// 0: idle; 1: low; 2: high
/* ************************************************** */
int adam_check_channel_busy(call_t *c) {
	struct nodedata *nodedata = get_node_private_data(c);
	call_t c0 = {get_entity_bindings_down(c)->elts[0], c->node, c->entity};
	int channel_state = 0;
	double noise_mw = 0;
	double threshold_mw = dBm2mW(nodedata->EDThreshold);
	double high_threshold_mw = nodedata->HighThreshold_mw>=0?nodedata->HighThreshold_mw:threshold_mw;
	
	//PRINT_MAC("B: threshold_mw=%f\n", threshold_mw);
	//PRINT_MAC("B: nodedata->EDThreshold=%f\n", nodedata->EDThreshold);
	
	if (nodedata->cs)
	{
		noise_mw = dBm2mW(radio_get_cs(&c0));
		if( noise_mw >= high_threshold_mw)
		{
			channel_state = 2;
		}
		else if(noise_mw >= threshold_mw)
		{
			channel_state = 1;
		}
	}
	else if (nodedata->cca) {
		noise_mw = dBm2mW(radio_get_noise(&c0));
		if(noise_mw >= high_threshold_mw)
		{
			channel_state = 2;
		}
		else if(noise_mw >= threshold_mw)
		{
			channel_state = 1;
		}
	}
	//PRINT_RESULT("noise_mw=%f, threshold_mw=%f, high_threshold_mw=%f\n", noise_mw, threshold_mw, high_threshold_mw);
	PRINT_MAC("E: noise_mw=%f, channel_state=%d\n", noise_mw, channel_state);

	return channel_state;
}

int dcf_802_11_state_machine(call_t *c, void *args) { 
	struct nodedata *nodedata = get_node_private_data(c);
	struct entitydata *entitydata = get_entity_private_data(c);
	packet_t *packet;	
	struct _sic_802_11_header *header;
	struct _sic_802_11_rts_header *rts_header;
	struct _sic_802_11_cts_header *cts_header;
	struct _sic_802_11_data_header *data_header;
#if 0//def ADAM_NO_SENSING
	struct _sic_802_11_ack_header *ack_header;
#endif// ADAM_NO_SENSING
	uint64_t timeout;
	call_t c0 = {get_entity_bindings_down(c)->elts[0], c->node, c->entity};
	int priority, channel_state;
	adam_error_code_t error_id = ADAM_ERROR_NO_ERROR;
		
	PRINT_MAC("B: c->node=%d, nodedata->state=%d\n", c->node, nodedata->state);
	PRINT_MAC("nodedata->power_type_data=%d\n", nodedata->power_type_data);
        /* Next packet to send */
	if (NULL != nodedata->txbuf)
	{
		PRINT_MAC("nodedata->txbuf->type=%d\n", nodedata->txbuf->type);
	}
	/* Drop unscheduled events */
	if (nodedata->clock != get_time()) {
		error_id = ADAM_ERROR_DEFAULT;
		goto END;
	}

    /* State machine */
    switch (nodedata->state) {
		
	case STATE_IDLE:
		if(0 == nodedata->base_power_tx)
		{
			nodedata->base_power_tx = radio_get_power(&c0);
		}
		    /* Next packet to send */
		if (nodedata->txbuf == NULL) {
			nodedata->txbuf = (packet_t *) das_pop_FIFO(nodedata->packets);
			if (nodedata->txbuf == NULL) {
				goto END;
			}
		}

		/* Initial backoff */
		nodedata->BE = macMinBE - 1;
		nodedata->NB = 0;
		nodedata->backoff = macMinDIFSPeriod;
		nodedata->backoff_suspended = 0;
					  
		nodedata->state = STATE_BACKOFF;
		nodedata->state_pending = STATE_BACKOFF;

#ifdef ADAM_NO_SENSING
		nodedata->allowed_high = -1;
		nodedata->allowed_low = -1;
#endif//ADAM_NO_SENSING
		
		/* Backoff */
		nodedata->clock = get_time();
		dcf_802_11_state_machine(c,NULL);
		goto END;
        
    case STATE_BACKOFF:
	PRINT_MAC("STATE_BACKOFF: nodedata->backoff=%"PRId64"\n", nodedata->backoff);
        /* If the backoff is over, set to 0 */
        if ((nodedata->backoff > 0) && (nodedata->backoff < aUnitBackoffPeriod)) {
            nodedata->backoff = 0;
            nodedata->clock = get_time();
            dcf_802_11_state_machine(c,NULL);
            goto END;
        }
        
	if(NULL == nodedata->txbuf)
	{
		goto END;
	}
	data_header = (struct _sic_802_11_data_header *) (nodedata->txbuf->data + sizeof(struct _sic_802_11_header));
	priority = nodedata->txbuf->type;
	//low channel power blocks low priority; high channel power blocks high priority
	channel_state = adam_check_channel_busy(c);
        /* Backoff or channel busy*/
        if (nodedata->backoff > 0 || 0 != channel_state) {
#ifdef ADAM_NO_SENSING
		if ((get_time() < nodedata->nav) || (0 != channel_state))
#else// ADAM_NO_SENSING
		if ((get_time() < nodedata->nav) || (0 != channel_state))
#endif// ADAM_NO_SENSING
		{ 
                if (nodedata->backoff_suspended == 0) {
                    /* Suspend backoff and add difs */
                    nodedata->backoff_suspended = 1;
                    nodedata->backoff = nodedata->backoff + macMinDIFSPeriod;
                }			
		} else {
                /* Decrease backoff */
                nodedata->backoff_suspended = 0;
                nodedata->backoff = nodedata->backoff - aUnitBackoffPeriod;
		}
            
            /* Set next event to backoff */
            nodedata->clock = get_time() + aUnitBackoffPeriod;
            scheduler_add_callback(nodedata->clock, c, dcf_802_11_state_machine, NULL);
            goto END;
        }
        
        /* Broadcast or unicast */
        header = (struct _sic_802_11_header *) nodedata->txbuf->data;
        if (header->dst == BROADCAST_ADDR) {
            nodedata->state = STATE_BROADCAST;
        } else {
            /* RTS or Data */
		if (nodedata->txbuf->size < nodedata->rts_threshold) 
		{
			nodedata->state = STATE_DATA;
		} 
		else 
		{
			nodedata->state = STATE_RTS;
		}
        }
        nodedata->state_pending = STATE_IDLE;
        nodedata->dst = header->dst;
        
        /* Next state */
        nodedata->clock = get_time();  
        dcf_802_11_state_machine(c,NULL);
        goto END;
        
        
    case STATE_RTS:
        /* Build RTS */
        packet = packet_create(c, sizeof(struct _sic_802_11_header) + sizeof(struct _sic_802_11_rts_header), -1);
        header = (struct _sic_802_11_header *) packet->data;
        header->dst = nodedata->dst;
        header->src = c->node;
        header->type = RTS_TYPE; 
        rts_header = (struct _sic_802_11_rts_header *) (packet->data + sizeof(struct _sic_802_11_header));
        rts_header->size = nodedata->txbuf->size;
        rts_header->nav = macMinSIFSPeriod + (sizeof(struct _sic_802_11_header) + sizeof(struct _sic_802_11_cts_header)) * radio_get_Tb(&c0) * 8 + macMinSIFSPeriod + nodedata->txbuf->size * 8 * radio_get_Tb(&c0) + macMinSIFSPeriod + (sizeof(struct _sic_802_11_header) + sizeof(struct _sic_802_11_ack_header)) * 8 * radio_get_Tb(&c0);				
        timeout = (sizeof(struct _sic_802_11_header) + sizeof(struct _sic_802_11_rts_header)) * 8 * radio_get_Tb(&c0) + macMinSIFSPeriod + (sizeof(struct _sic_802_11_header) + sizeof(struct _sic_802_11_cts_header)) * 8 * radio_get_Tb(&c0) + SPEED_LIGHT; 			

#ifdef ADAM_NO_SENSING
	PRINT_MAC("STATE_RTS: packet->id=%d, nodedata->txbuf->type=%d\n", packet->id, nodedata->txbuf->type);
	// high priority rts
	if(1 == nodedata->txbuf->type)
	{
		rts_header->priority_type = 1;
	}
	// low priority rts
	else
	{
		rts_header->priority_type = 0;
	}
#endif//ADAM_NO_SENSING
	// recover power
	radio_set_power(&c0, 1);
	/* Send RTS */
	TX(&c0, packet); 
        
        /* Wait for timeout or CTS */
        nodedata->state = STATE_TIMEOUT;
        nodedata->clock = get_time() + timeout;
        scheduler_add_callback(nodedata->clock, c, dcf_802_11_state_machine, NULL);			
        goto END;
        
        
    case STATE_TIMEOUT:			
		PRINT_MAC("STATE_TIMEOUT: nodedata->NB=%d\n", nodedata->NB);
#ifdef ADAM_NO_SENSING
		nodedata->power_type_data = 0;
#endif// ADAM_NO_SENSING
        if ((++nodedata->NB) >= entitydata->maxCSMARetries) {
		/* Transmit retry limit reached */
		packet_dealloc(nodedata->txbuf);            
		nodedata->txbuf = NULL;
			
		/* Return to idle */
		nodedata->state = STATE_IDLE;
		nodedata->clock = get_time();
		dcf_802_11_state_machine(c,NULL);
		goto END;
        }
			
// <-RF00000000-AdamXu-2018/09/10-mac without carrier sensing.
#if 1//ndef ADAM_NO_SENSING
        /* Update backoff */
        if ((++nodedata->BE) > macMaxBE) {
            nodedata->BE = macMaxBE;
        }
		
        nodedata->backoff = get_random_double() 
            * (pow(2, nodedata->BE) - 1) 
            * aUnitBackoffPeriod 
            + macMinDIFSPeriod;
#else //ADAM_NO_SENSING
        /* Update backoff */
	if(NULL != nodedata->txbuf)
	{
		if(1 == nodedata->txbuf->type)
		{
			if ((++nodedata->BE) > macMaxBE) 
			{
				nodedata->BE = macMaxBE;
			}
		}
		else
		{
			if ((++nodedata->BE) > macMaxBE)
			{
				nodedata->BE = macMaxBE;
			}
		}
	}
	nodedata->backoff = ((int)(get_random_double() * (pow(2, nodedata->BE) - 1))) * aUnitBackoffPeriod;
#endif//ADAM_NO_SENSING
// ->RF00000000-AdamXu
	PRINT_MAC("STATE_TIMEOUT: nodedata->BE=%d, nodedata->backoff=%"PRId64"\n", nodedata->BE, nodedata->backoff);
        nodedata->backoff_suspended = 0;
        nodedata->state = STATE_BACKOFF;
        nodedata->state_pending = STATE_BACKOFF;				
        
        /* Backoff */
        nodedata->clock = get_time();  
        dcf_802_11_state_machine(c,NULL);
        goto END;
			
        
    case STATE_CTS:
		/* Build CTS */
		packet = packet_create(c, sizeof(struct _sic_802_11_header) + sizeof(struct _sic_802_11_cts_header), -1);
		header= (struct _sic_802_11_header *) packet->data;
		header->dst = nodedata->dst;
		header->src = c->node;
		header->type = CTS_TYPE; 
		cts_header = (struct _sic_802_11_cts_header *) (packet->data + sizeof(struct _sic_802_11_header));
		cts_header->nav = macMinSIFSPeriod + nodedata->size * 8 * radio_get_Tb(&c0) + macMinSIFSPeriod + (sizeof(struct _sic_802_11_header) + sizeof(struct _sic_802_11_ack_header)) * 8 * radio_get_Tb(&c0); 						
		timeout = (sizeof(struct _sic_802_11_header) + sizeof(struct _sic_802_11_cts_header)) * 8 * radio_get_Tb(&c0) + macMinSIFSPeriod + nodedata->size * 8 * radio_get_Tb(&c0) + SPEED_LIGHT;

		// recover power
		radio_set_power(&c0, 1);

		/* Send CTS */
		TX(&c0, packet); 

		/* Wait for timeout or DATA */
		nodedata->state = STATE_CTS_TIMEOUT;
		nodedata->clock = get_time() + timeout;
		scheduler_add_callback(nodedata->clock, c, dcf_802_11_state_machine, NULL);
		goto END;
        
        
    case STATE_CTS_TIMEOUT:
        /* Return to pending state */
        nodedata->state = nodedata->state_pending;
        if (nodedata->state != STATE_IDLE) {
            header = (struct _sic_802_11_header *) nodedata->txbuf->data;
            nodedata->dst = header->dst;
        }
        nodedata->clock = get_time();
        dcf_802_11_state_machine(c,NULL);
        goto END;
			
        
    case STATE_DATA:
		/* Build data packet */	
		packet = packet_clone(nodedata->txbuf);
		header = (struct _sic_802_11_header *) packet->data;
		data_header = (struct _sic_802_11_data_header *) (packet->data + sizeof(struct _sic_802_11_header));
		data_header->nav = macMinSIFSPeriod + (sizeof(struct _sic_802_11_header) + sizeof(struct _sic_802_11_ack_header)) * 8 * radio_get_Tb(&c0);
#ifdef ADAM_NO_SENSING
		timeout = packet->size * 8 * radio_get_Tb(&c0) + macMinSIFSPeriod + SPEED_LIGHT;
#else// ADAM_NO_SENSING
		timeout = packet->size * 8 * radio_get_Tb(&c0) + macMinSIFSPeriod + (sizeof(struct _sic_802_11_header) + sizeof(struct _sic_802_11_ack_header)) * 8 * radio_get_Tb(&c0) + SPEED_LIGHT;
#endif// ADAM_NO_SENSING

		// adjust power for high priority
		//nodedata->base_power_tx = radio_get_power(&c0);
#ifdef ADAM_NO_SENSING
		if(2 == nodedata->power_type_data)
#else
		if(1 == packet->type && 1 == adam_check_channel_busy(c))
#endif//ADAM_NO_SENSING
		{
			//radio_set_power(&c0, ADAM_HIGH_POWER_DBM_GAIN+nodedata->base_power_tx);
			radio_set_power(&c0, 0-ADAM_HIGH_POWER_RATIO);
		}
		else
		{
			// recover power
			radio_set_power(&c0, 1);
		}

#ifdef ADAM_NO_SENSING
		nodedata->power_type_data = 0;
#endif// ADAM_NO_SENSING
		PRINT_MAC("STATE_DATA radio_get_power=%f, packet->id=%d\n", radio_get_power(&c0), packet->id);
		PRINT_MAC("s_sent_mac=%d\n", ++s_sent_mac);
		//PRINT_RESULT("STATE_DATA radio_get_power=%f\n", radio_get_power(&c0));
		/* Send data */
		TX(&c0, packet);
		
		// recover power
		//radio_set_power(&c0, nodedata->base_power_tx);

#ifdef ADAM_NO_SENSING
		/* No ACK */
		nodedata->state = STATE_BROAD_DONE;
#else// ADAM_NO_SENSING
		/* Wait for timeout or ACK */
		nodedata->state = STATE_TIMEOUT;
#endif//ADAM_NO_SENSING
		nodedata->clock = get_time() + timeout;
		scheduler_add_callback(nodedata->clock, c, dcf_802_11_state_machine, NULL);
		goto END;
		
    case STATE_BROADCAST:
		/* Build data packet */	
		packet = packet_clone(nodedata->txbuf);
		data_header = (struct _sic_802_11_data_header *) (packet->data + sizeof(struct _sic_802_11_header));
		timeout = packet->size * 8 * radio_get_Tb(&c0) + macMinSIFSPeriod;

		// adjust power for high priority
		//nodedata->base_power_tx = radio_get_power(&c0);
// <-RF00000000-AdamXu-2018/09/10-mac without carrier sensing.
#ifdef ADAM_NO_SENSING
		if(0)
#else
		if(1 == packet->type && 1 == adam_check_channel_busy(c))
#endif//ADAM_NO_SENSING
// ->RF00000000-AdamXu
		{
			radio_set_power(&c0, 0-ADAM_HIGH_POWER_RATIO);
		}
		else
		{
			// recover power
			radio_set_power(&c0, 1);
		}
		
		//PRINT_MAC("STATE_BROADCAST radio_get_power=%f, packet->id=%d\n", radio_get_power(&c0), packet->id);
		/* Send data */
		TX(&c0, packet);
		
		// recover power
		//radio_set_power(&c0, nodedata->base_power_tx);

		/* Wait for timeout or ACK */
		nodedata->state = STATE_BROAD_DONE;
		nodedata->clock = get_time() + timeout;
		scheduler_add_callback(nodedata->clock, c, dcf_802_11_state_machine, NULL);
		goto END;
        
    case STATE_ACK:
		/* Build ack packet */	
		packet = packet_create(c, sizeof(struct _sic_802_11_header) + sizeof(struct _sic_802_11_ack_header), -1);
		header = (struct _sic_802_11_header *) packet->data;
		header->type = ACK_TYPE; 
		header->src = c->node;
		header->dst = nodedata->dst;
		timeout =  packet->size * 8 * radio_get_Tb(&c0) + macMinSIFSPeriod;

		// recover power
		radio_set_power(&c0, 1);

		/* Send ack */
		TX(&c0, packet);

		/* Wait for end of transmission */
		nodedata->state = STATE_DONE;
		nodedata->clock = get_time() + timeout;
		scheduler_add_callback(nodedata->clock, c, dcf_802_11_state_machine, NULL);
		goto END;
        
    case STATE_DONE:
        /* Return to pending state */
        nodedata->state = nodedata->state_pending;
        if (nodedata->state != STATE_IDLE) {
            header = (struct _sic_802_11_header *) nodedata->txbuf->data;
            nodedata->dst = header->dst;
        }
        nodedata->clock = get_time();
        dcf_802_11_state_machine(c,NULL);
        goto END;
        
        
    case STATE_BROAD_DONE:
		/* Destroy txbuf */
		packet_dealloc(nodedata->txbuf);
		nodedata->txbuf = NULL;
			
		/* Back to idle state*/
		nodedata->state = STATE_IDLE;
		nodedata->clock = get_time();			
		dcf_802_11_state_machine(c,NULL);
		goto END;

#ifdef ADAM_NO_SENSING
	// sink nodes broadcast cts for contentions
	case STATE_CONTENTION_BEGIN:
		/* Build contention begin packets: similar to cts */
		packet = packet_create(c, sizeof(struct _sic_802_11_header) + sizeof(struct _sic_802_11_cts_header), -1);
		header = (struct _sic_802_11_header *) packet->data;
		header->dst = BROADCAST_ADDR;
		header->src = c->node;
		header->type = CONTENTION_BEGIN_TYPE; 
		cts_header = (struct _sic_802_11_cts_header *) (packet->data + sizeof(struct _sic_802_11_header));
		cts_header->node_allowed = nodedata->dst;
		PRINT_MAC("STATE_CONTENTION_BEGIN nodedata->sink_state=%d, nodedata->dst=%d\n", nodedata->sink_state, nodedata->dst);
		nodedata->dst = -1;
		// CTS for low priority RTS, begin high contention
		if(-1 == nodedata->sink_state)
		{
			nodedata->allowed_low = cts_header->node_allowed;
			// Timeout
			timeout = CTS_TIME + macMinSIFSPeriod + RTS_TIME+ macMinSIFSPeriod + SPEED_LIGHT +pow(2, MAX_CONTENTION_WINDOW_LOW-(int)(MAX_CONTENTION_WINDOW_LOW*(1-nodedata->priority_ratio)))  * MIN_CONTENTION_BACKOFF_PERIOD;
			cts_header->priority_type = 2;
		}
		// CTS for high priority RTS, begin low contention
		else if(-2 == nodedata->sink_state)
		{
			nodedata->allowed_high = cts_header->node_allowed;
			// Timeout
			timeout = CTS_TIME + macMinSIFSPeriod + RTS_TIME+ macMinSIFSPeriod + SPEED_LIGHT + pow(2, MAX_CONTENTION_WINDOW_LOW) * MIN_CONTENTION_BACKOFF_PERIOD;
			cts_header->priority_type = 1;
		}
		// no situation fit
		else
		{
			nodedata->sink_state = 0;
			error_id = 2;
			goto END;
		}
		nodedata->sink_state = 0;
		/* Wait for Data request */
		nodedata->state = STATE_CONTENTION_WAITING_DATA;
		nodedata->dst = -1;
		
		// recover power
		radio_set_power(&c0, 1);
		
		/* Send CTS */
		TX(&c0, packet); 

		nodedata->clock = get_time() + timeout;
		PRINT_MAC("timeout=%"PRId64"\n", timeout);
		PRINT_MAC("nodedata->clock=%"PRId64"\n", nodedata->clock);
		scheduler_add_callback(nodedata->clock, c, dcf_802_11_state_machine, NULL);
		goto END;

	case STATE_CONTENTION_WAITING_DATA:
		/* Build Data request */
		packet = packet_create(c, sizeof(struct _sic_802_11_header) + sizeof(struct _sic_802_11_cts_header), -1);
		header= (struct _sic_802_11_header *) packet->data;
		header->dst = BROADCAST_ADDR;
		header->src = c->node;
		header->type = CONTENTION_END_TYPE; 
		cts_header = (struct _sic_802_11_cts_header *) (packet->data + sizeof(struct _sic_802_11_header));
		cts_header->nav = macMinSIFSPeriod + nodedata->size * 8 * radio_get_Tb(&c0) + macMinSIFSPeriod + (sizeof(struct _sic_802_11_header) + sizeof(struct _sic_802_11_ack_header)) * 8 * radio_get_Tb(&c0);
		cts_header->node_allowed = nodedata->dst;
		PRINT_MAC("STATE_CONTENTION_WAITING_DATA nodedata->sink_state=%d, nodedata->dst=%d\n", nodedata->sink_state, nodedata->dst);
		nodedata->dst = -1;
		timeout = (sizeof(struct _sic_802_11_header) + sizeof(struct _sic_802_11_cts_header)) * 8 * radio_get_Tb(&c0) + macMinSIFSPeriod + nodedata->size * 8 * radio_get_Tb(&c0) + SPEED_LIGHT;
		// CTS for high priority RTS
		if(-1 == nodedata->sink_state)
		{
			nodedata->allowed_high = cts_header->node_allowed;
			cts_header->priority_type = 1;
		}
		// CTS for low priority RTS
		else if(-2 == nodedata->sink_state)
		{
			nodedata->allowed_low = cts_header->node_allowed;
			cts_header->priority_type = 2;
		}
		// no situation fit
		else
		{
			cts_header->priority_type = 0;
			//nodedata->sink_state = 0;
			//error_id = 2;
			//goto END;
		}
		nodedata->sink_state = 0;

		// recover power
		radio_set_power(&c0, 1);
		
		/* Send Data request */
		TX(&c0, packet); 

		/* Wait for timeout or DATA */
		nodedata->state = STATE_CTS_TIMEOUT;
		nodedata->clock = get_time() + timeout;
		scheduler_add_callback(nodedata->clock, c, dcf_802_11_state_machine, NULL);
		goto END;

	case STATE_CONTENTION:
		/* Build Contention packets */
		packet = packet_create(c, sizeof(struct _sic_802_11_header) + sizeof(struct _sic_802_11_rts_header), -1);
		header = (struct _sic_802_11_header *) packet->data;
		header->dst = nodedata->dst;
		header->src = c->node;
		header->type = CONTENTION_TYPE; 
		rts_header = (struct _sic_802_11_rts_header *) (packet->data + sizeof(struct _sic_802_11_header));
		rts_header->size = nodedata->txbuf->size;
		rts_header->nav = macMinSIFSPeriod + (sizeof(struct _sic_802_11_header) + sizeof(struct _sic_802_11_cts_header)) * radio_get_Tb(&c0) * 8 + macMinSIFSPeriod + nodedata->txbuf->size * 8 * radio_get_Tb(&c0) + macMinSIFSPeriod + (sizeof(struct _sic_802_11_header) + sizeof(struct _sic_802_11_ack_header)) * 8 * radio_get_Tb(&c0);
		timeout = RTS_TIME+ macMinSIFSPeriod + CTS_TIME + macMinSIFSPeriod + SPEED_LIGHT + packet->size * 8 * radio_get_Tb(&c0);

		// high priority
		if(-1 == nodedata->source_state)
		{
			rts_header->priority_type = 1;
		}
		// low priority
		else if(-2 == nodedata->source_state)
		{
			rts_header->priority_type = 0;
		}
		// other
		else
		{
			nodedata->source_state = 0;
			error_id = 1;
			goto END;
		}
		nodedata->source_state = 0;

		// recover power
		radio_set_power(&c0, 1);
		
		/* Send Contention */
		TX(&c0, packet); 

		// contention does not count in
		if(0 < nodedata->NB)
		{
			nodedata->NB--;
		}
		if(0 < nodedata->BE)
		{
			nodedata->BE--;
		}
		
		/* Wait for timeout or Contention end */
		nodedata->state = STATE_TIMEOUT;
		nodedata->clock = get_time() + timeout;
		scheduler_add_callback(nodedata->clock, c, dcf_802_11_state_machine, NULL);			
		goto END;
#endif//ADAM_NO_SENSING

		
    default:
        goto END;
    }

END:
	if(ADAM_ERROR_NO_ERROR != error_id)
	{
		PRINT_MAC("E: error_id=%d\n", error_id);
	}
	return error_id;
}


/* ************************************************** */
/* ************************************************** */
void tx(call_t *c, packet_t *packet) {
	struct nodedata *nodedata = get_node_private_data(c);
	PRINT_MAC("B: packet->id=%d, c->node=%d\n", packet->id, c->node);

	das_insert(nodedata->packets, (void*)packet);

    if (nodedata->state == STATE_IDLE) {
        nodedata->clock = get_time();  
        dcf_802_11_state_machine(c,NULL);
    } 
}


/* ************************************************** */
/* ************************************************** */
void rx(call_t *c, packet_t *packet) {
	struct nodedata *nodedata = get_node_private_data(c);
	struct _sic_802_11_header *header = (struct _sic_802_11_header *) packet->data;
	struct _sic_802_11_rts_header *rts_header;
	struct _sic_802_11_cts_header *cts_header;
	struct _sic_802_11_data_header *data_header;
	// struct _sic_802_11_ack_header *ack_header;
	array_t *up = get_entity_bindings_up(c);
	int i = up->size;
// <-RF00000000-AdamXu-2018/09/10-mac without carrier sensing.
#ifdef ADAM_NO_SENSING
	uint64_t timeout;
#endif//ADAM_NO_SENSING
// ->RF00000000-AdamXu
	int error_id = 0;
	call_t c0 = {get_entity_bindings_down(c)->elts[0], c->node, c->entity};

	PRINT_MAC("B: c->node=%d, packet->id=%d, packet->type=%d\n", c->node, packet->id, packet->type);
	PRINT_MAC("header->type=%d, header->dst=%d\n", header->type, header->dst);
	if(nodedata->HighThreshold_mw < 0)
	{
		nodedata->HighThreshold_mw = (1+ADAM_HIGH_PRIOTITY_RATIO)*packet->rxmW*dBm2mW(radio_get_power(&c0))/dBm2mW(packet->txdBm);
		//PRINT_RESULT("nodedata->HighThreshold_mw=%f\n", nodedata->HighThreshold_mw);
	}
	
    switch (header->type) {
		
    case RTS_TYPE:
        /* Receive RTS*/
        rts_header = (struct _sic_802_11_rts_header *) (packet->data + sizeof(struct _sic_802_11_header));
			
        if (header->dst != c->node) {
            /* Packet not for us */
            if (nodedata->nav < (get_time() + rts_header->nav)) {
                nodedata->nav = get_time() + rts_header->nav;
            }
            packet_dealloc(packet);
		error_id = 1;
		goto END;
        }
        
#if 1//ndef ADAM_NO_SENSING
        if ((nodedata->state != STATE_IDLE) 
            && (nodedata->state != STATE_BACKOFF))
#else// ADAM_NO_SENSING
        if ((STATE_CONTENTION_WAITING_HIGH !=  nodedata->state) && (STATE_CONTENTION_WAITING_LOW !=  nodedata->state))
#endif//ADAM_NO_SENSING
	{
		/* If not expecting rts, do nothing */
		packet_dealloc(packet);
		error_id = 2;
		goto END;
	}

        /* Record RTS info */
        nodedata->dst = header->src;
        nodedata->size = rts_header->size;
#ifdef ADAM_NO_SENSING
	nodedata->allowed_high = -1;
	nodedata->allowed_low = -1;
	// received high RTS, begin low Contention
	if(1 == rts_header->priority_type)
	{
	        nodedata->sink_state = -2;
	}
	// received low RTS, begin high Contention
	else
	{
	        nodedata->sink_state = -1;
	}
#endif// ADAM_NO_SENSING

        packet_dealloc(packet);
			
        /* Send CTS */
        if (nodedata->state == STATE_BACKOFF) {
            nodedata->state_pending = nodedata->state;
        } else {
            nodedata->state_pending = STATE_IDLE;
        }
#ifndef ADAM_NO_SENSING
        nodedata->state = STATE_CTS;
#else// ADAM_NO_SENSING
	nodedata->state = STATE_CONTENTION_BEGIN;
#endif// ADAM_NO_SENSING
        nodedata->clock = get_time() + macMinSIFSPeriod;
        scheduler_add_callback(nodedata->clock, c, dcf_802_11_state_machine, NULL);
        break;
			
    case CTS_TYPE:
        /* Receive CTS */
        cts_header = (struct _sic_802_11_cts_header *) (packet->data + sizeof(struct _sic_802_11_header));
					
        if (header->dst != c->node) {
            /* Packet not for us */
            if (nodedata->nav < (get_time() + cts_header->nav)) {
                nodedata->nav = get_time() + cts_header->nav;
            }
            packet_dealloc(packet);
		error_id = 1;
		goto END;
        }

        if (nodedata->state != STATE_TIMEOUT) {
            /* If not expecting cts, do nothing */
            packet_dealloc(packet);
		error_id = 2;
		goto END;
        }

        if (nodedata->dst != header->src) {
            /* Expecting cts, but not from this node */
            packet_dealloc(packet);
		error_id = 3;
		goto END;
        }

        /* Record CTS info */
        packet_dealloc(packet);
			
        /* Send DATA */
        nodedata->state = STATE_DATA;
        nodedata->clock = get_time() + macMinSIFSPeriod;
        scheduler_add_callback(nodedata->clock, c, dcf_802_11_state_machine, NULL);
        break;
			
    case DATA_TYPE:
        /* Received DATA */
        data_header = (struct _sic_802_11_data_header *) (packet->data + sizeof(struct _sic_802_11_header));
			
        if (header->dst != c->node) {
            /* Packet not for us */
            if (nodedata->nav < (get_time() + data_header->nav)) {
                nodedata->nav = get_time() + data_header->nav;
            }
            packet_dealloc(packet);
		error_id = 1;
		goto END;
        }
				
        if ((nodedata->state != STATE_IDLE) 
            && (nodedata->state != STATE_BACKOFF) 
            && (nodedata->state != STATE_CTS_TIMEOUT)) {
            /* If not expecting data, do nothing */
            packet_dealloc(packet);
		error_id = 2;
		goto END;
        }

#ifdef ADAM_NO_SENSING
        if ((nodedata->state == STATE_CTS_TIMEOUT) && ((nodedata->allowed_high != header->src) && (nodedata->allowed_low != header->src)))
#else// ADAM_NO_SENSING
        if ((nodedata->state == STATE_CTS_TIMEOUT) && (nodedata->dst != header->src)) 
#endif// ADAM_NO_SENSING
		{
            /* Expecting data, but not from this node */
            packet_dealloc(packet);
		error_id = 3;
		goto END;
        }

		PRINT_MAC("s_received_mac=%d, s_sent_mac=%d\n", ++s_received_mac, s_sent_mac);

#ifndef ADAM_NO_SENSING
        /* Send ACK */
        if (nodedata->state == STATE_BACKOFF) {
            nodedata->state_pending = nodedata->state;
        } else {
            nodedata->state_pending = STATE_IDLE;
        }
        nodedata->dst = header->src;
        nodedata->state = STATE_ACK;
        nodedata->clock = get_time() + macMinSIFSPeriod;
        scheduler_add_callback(nodedata->clock, c, dcf_802_11_state_machine, NULL);
#endif// ADAM_NO_SENSING

        /* forward to upper layer */
        while (i--) {
            call_t c_up = {up->elts[i], c->node, c->entity};
            packet_t *packet_up;
            
            if (i > 0) {
                packet_up = packet_clone(packet);         
            } else {
                packet_up = packet;
            }
            RX(&c_up, packet_up);
        }
        break;
			

    case BROADCAST_TYPE:
        /* Receive RTS*/
        data_header = (struct _sic_802_11_data_header *) (packet->data + sizeof(struct _sic_802_11_header));
        
        if (header->dst != BROADCAST_ADDR) {
            /* Packet not for us */
            packet_dealloc(packet);
		error_id = 1;
		goto END;
        }
        
        /* forward to upper layer */
        while (i--) {
            call_t c_up = {up->elts[i], c->node, c->entity};
            packet_t *packet_up;
	
            if (i > 0) {
                packet_up = packet_clone(packet);         
            } else {
                packet_up = packet;
            }
            RX(&c_up, packet_up);
        }
        break;

    case ACK_TYPE:
        /* Received ACK */
        // ack_header = (struct _sic_802_11_ack_header *) (packet->data + sizeof(struct _sic_802_11_header));
        
        if (header->dst != c->node) {
            /* Packet not for us */
            packet_dealloc(packet);
		error_id = 1;
		goto END;
        }
        
        if (nodedata->state != STATE_TIMEOUT) {
            /* If not expecting ack, do nothing */
            packet_dealloc(packet);
		error_id = 2;
		goto END;
        }
				
        if (nodedata->dst != header->src) {
            /* Expecting ack, but not from this node */
            packet_dealloc(packet);
		error_id = 3;
		goto END;
        }
				
        /* Destroy txbuf */
        packet_dealloc(packet);
        packet_dealloc(nodedata->txbuf);
        nodedata->txbuf = NULL;      
			
        /* Back to idle state*/
        nodedata->state = STATE_IDLE;
        nodedata->clock = get_time();			
      
        dcf_802_11_state_machine(c,NULL);
        break;
        
#ifdef ADAM_NO_SENSING
	case CONTENTION_BEGIN_TYPE:
	        /* Receive CTS */
	        cts_header = (struct _sic_802_11_cts_header *) (packet->data + sizeof(struct _sic_802_11_header));			
		if ((STATE_IDLE != nodedata->state) && (STATE_TIMEOUT!= nodedata->state) && (STATE_BACKOFF != nodedata->state))
		{
			/* If not expecting request, do nothing */
			packet_dealloc(packet);
			error_id = 2;
			goto END;
		}
		if(NULL == nodedata->txbuf)
		{
			/* If no packet, do nothing */
			packet_dealloc(packet);
			error_id = 3;
			goto END;
		}
		if(header->src != nodedata->dst)
		{
			/* If not request from destination, do nothing */
			packet_dealloc(packet);
			error_id = 4;
			goto END;
		}
		// win RTS, wait for contention end
		if(c->node == cts_header->node_allowed)
		{
			// high
			if(1 == cts_header->priority_type)
			{
				nodedata->power_type_data = 2;
				// wait for low
				timeout = CTS_TIME + macMinSIFSPeriod + RTS_TIME+ macMinSIFSPeriod + SPEED_LIGHT +pow(2, MAX_CONTENTION_WINDOW_LOW)  * MIN_CONTENTION_BACKOFF_PERIOD;
			}
			// low
			else
			{
				nodedata->power_type_data = 1;
				// wait for high
				timeout = CTS_TIME + macMinSIFSPeriod + RTS_TIME+ macMinSIFSPeriod + SPEED_LIGHT +pow(2, MAX_CONTENTION_WINDOW_LOW-(int)(MAX_CONTENTION_WINDOW_LOW*(1-nodedata->priority_ratio)))  * MIN_CONTENTION_BACKOFF_PERIOD;
			}
			timeout += nodedata->txbuf->size*8*radio_get_Tb(&c0);
			nodedata->state = STATE_TIMEOUT;
			nodedata->clock = get_time() + timeout;
			PRINT_MAC("nodedata->clock=%"PRId64"\n", nodedata->clock);
			PRINT_MAC("timeout=%"PRId64"\n", timeout);
			scheduler_add_callback(nodedata->clock, c, dcf_802_11_state_machine, NULL);
			goto END;
		}
		// high power contention begin
		if(2 == cts_header->priority_type)
		{
			// has high priority packet
			if(1 == nodedata->txbuf->type)
			{
				nodedata->source_state = -1;
			}
			else
			{
				packet_dealloc(packet);
				// wait for period end
				timeout = CTS_TIME + macMinSIFSPeriod + RTS_TIME+ macMinSIFSPeriod + SPEED_LIGHT +pow(2, MAX_CONTENTION_WINDOW_LOW-(int)(MAX_CONTENTION_WINDOW_LOW*(1-nodedata->priority_ratio)))  * MIN_CONTENTION_BACKOFF_PERIOD + nodedata->txbuf->size*8*radio_get_Tb(&c0);
				nodedata->clock = get_time() + timeout;
				scheduler_add_callback(nodedata->clock, c, dcf_802_11_state_machine, NULL);
				error_id = 4;
				goto END;
			}
		}
		// low power contention begin
		else
		{
			// low priority packets and high priority packets
			nodedata->source_state = -2;
		}
		
		//high power contention random backoff contention
		if(2 == cts_header->priority_type)
		{
			timeout = ((int)(get_random_double() * (pow(2, MAX_CONTENTION_WINDOW_LOW-(int)(MAX_CONTENTION_WINDOW_LOW*(1-nodedata->priority_ratio))) - 1))) * MIN_CONTENTION_BACKOFF_PERIOD;
		}
		else
		{
			timeout = ((int)(get_random_double() * (pow(2, MAX_CONTENTION_WINDOW_LOW) - 1))) * MIN_CONTENTION_BACKOFF_PERIOD;
		}
		
		packet_dealloc(packet);
		nodedata->state_pending = nodedata->state;
		nodedata->state = STATE_CONTENTION;
		nodedata->clock = get_time() + timeout;
		scheduler_add_callback(nodedata->clock, c, dcf_802_11_state_machine, NULL);
		break;

	case CONTENTION_END_TYPE:
	        /* Receive CTS */
	        cts_header = (struct _sic_802_11_cts_header *) (packet->data + sizeof(struct _sic_802_11_header));
		if ((STATE_IDLE != nodedata->state) && (STATE_TIMEOUT!= nodedata->state) && (STATE_BACKOFF != nodedata->state))
		{
			/* If not expecting request, do nothing */
			packet_dealloc(packet);
			error_id = 2;
			goto END;
		}
		if(NULL == nodedata->txbuf)
		{
			/* If no packet, do nothing */
			packet_dealloc(packet);
			error_id = 3;
			goto END;
		}
		if(header->src != nodedata->dst)
		{
			/* If not request from destination, do nothing */
			packet_dealloc(packet);
			error_id = 4;
			goto END;
		}
		// win contention, prepare for transmission
		if(c->node == cts_header->node_allowed)
		{
			// high
			if(1 == cts_header->priority_type)
			{
				nodedata->power_type_data = 2;
			}
			//low
			else if(2 == cts_header->priority_type)
			{
				nodedata->power_type_data = 1;
			}
		}
		if(0 != nodedata->power_type_data)
		{
			nodedata->state = STATE_DATA;
			nodedata->clock = get_time() + macMinSIFSPeriod;
			scheduler_add_callback(nodedata->clock, c, dcf_802_11_state_machine, NULL);
		}
		else
		{
			timeout = nodedata->txbuf->size * 8 * radio_get_Tb(&c0) + macMinSIFSPeriod +  SPEED_LIGHT;
			if(STATE_CONTENTION == nodedata->state)
			{
				nodedata->state = nodedata->state_pending;
			}
			nodedata->clock = get_time() + timeout;
			scheduler_add_callback(nodedata->clock, c, dcf_802_11_state_machine, NULL);
		}
		packet_dealloc(packet);
		break;
		
    case CONTENTION_TYPE:
	/* Receive Contention*/
	rts_header = (struct _sic_802_11_rts_header *) (packet->data + sizeof(struct _sic_802_11_header));

	if (header->dst != c->node)
	{
		/* Packet not for us */
		if (nodedata->nav < (get_time() + rts_header->nav))
		{
			nodedata->nav = get_time() + rts_header->nav;
		}
		packet_dealloc(packet);
		error_id = 1;
		goto END;
	}

	if (nodedata->state != STATE_CONTENTION_WAITING_DATA)
	{
		/* If not expecting contention, do nothing */
		packet_dealloc(packet);
		error_id = 2;
		goto END;
	}

	/* Record RTS info */
	nodedata->dst = header->src;
	nodedata->size = rts_header->size;
	if(1 == rts_header->priority_type)
	{
		nodedata->sink_state = -1;
	}
	else
	{
		nodedata->sink_state = -2;
	}

	packet_dealloc(packet);

	/* Send Contention end */
	nodedata->state = STATE_CONTENTION_WAITING_DATA;
	nodedata->clock = get_time() + macMinSIFSPeriod;
	scheduler_add_callback(nodedata->clock, c, dcf_802_11_state_machine, NULL);
	break;
#endif// ADAM_NO_SENSING
    default:
		packet_dealloc(packet);
		error_id = 5;
		goto END;
    }
END:
	if(ADAM_ERROR_NO_ERROR != error_id)
	{
		PRINT_MAC("E: error_id=%d\n", error_id);
	}
}


/* ************************************************** */
/* ************************************************** */
int set_header(call_t *c, packet_t *packet, destination_t *dst) {
	struct _sic_802_11_header *header = (struct _sic_802_11_header *) packet->data;
	struct _sic_802_11_data_header *dheader = (struct _sic_802_11_data_header *) (packet->data + sizeof(struct _sic_802_11_header));

	PRINT_MAC("B\n");
	if ((header->dst = dst->id) == BROADCAST_ADDR) {
		header->type =BROADCAST_TYPE;
	} else {
		header->type = DATA_TYPE;
	}
	header->src = c->node;
	dheader->size = packet->size;
	// add priority here
	//dheader->priority = (get_random_integer()%ADAM_HIGH_PRIOTITY_RATIO == 0)?1:0;

	return 0;
}

int get_header_size(call_t *c) {
	return (sizeof(struct _sic_802_11_header) + sizeof(struct _sic_802_11_data_header));
}

int get_header_real_size(call_t *c) {
	return (sizeof(struct _sic_802_11_header) + sizeof(struct _sic_802_11_data_header));
}


/* ************************************************** */
/* ************************************************** */
mac_methods_t methods = {rx, 
                         tx,
                         set_header, 
                         get_header_size,
                         get_header_real_size};


    
    
