/**
 *  \file   sic.c
 *  \brief  Successive interference cancellation radio model
 *  \author Adam Xu
 *  \date   2018
 **/
#include <include/modelutils.h>
#include <sic.h>


/* ************************************************** */
/* ************************************************** */
model_t model =  {
    "Sic radio interface",
    "Adam Xu",
    "0.1",
    MODELTYPE_RADIO, 
    {NULL, 0}
};


/* ************************************************** */
/* ************************************************** */

struct nodedata {
	uint64_t Ts;
	double power;
	int channel;
	entityid_t modulation;
	double mindBm;
	int sleep;  
	double sic_threshold;
	int tx_busy;
	int rx_busy;
	double rxdBm;
	sic_signal_t* sic_signal_time_first;
	sic_signal_t* sic_signal_power_first;
};


/* ************************************************** */
/* ************************************************** */
void cs_init(call_t *c);

// <-RF00000000-AdamXu-2018/05/29-declare for test.
//double get_power(call_t *c);
// ->RF00000000-AdamXu

/* ************************************************** */
/* ************************************************** */
int init(call_t *c, void *params) {
    return 0;
}

int destroy(call_t *c) {
    return 0;
}


/* ************************************************** */
/* ************************************************** */
int setnode(call_t *c, void *params) {
	struct nodedata *nodedata = malloc(sizeof(struct nodedata));
	param_t *param;

	/* default values */
	nodedata->Ts = 91;
	nodedata->channel = 0;
	nodedata->power = 0;
	nodedata->modulation = -1;
	nodedata->mindBm = -92;
	nodedata->sleep = 0;

	nodedata->sic_signal_time_first = NULL;
	nodedata->sic_signal_power_first = NULL;

    /* get parameters */
    das_init_traverse(params);
    while ((param = (param_t *) das_traverse(params)) != NULL) {
        if (!strcmp(param->key, "sensibility")) {
            if (get_param_double(param->value, &(nodedata->mindBm))) {
                goto error;
            }
        }
        if (!strcmp(param->key, "T_s")) {
            if (get_param_time(param->value, &(nodedata->Ts))) {
                goto error;
            }
        }
        if (!strcmp(param->key, "channel")) {
            if (get_param_integer(param->value, &(nodedata->channel))) {
                goto error;
            }
        }
        if (!strcmp(param->key, "dBm")) {
            if (get_param_double(param->value, &(nodedata->power))) {
                goto error;
            }
        }
        if (!strcmp(param->key, "modulation")) {
            if (get_param_entity(param->value, &(nodedata->modulation))) {
                goto error;
            }
        }
    }

	
	set_node_private_data(c, nodedata);
	//PRINT_RADIO("RADIO c->node=%d\n", c->node);
	//PRINT_RADIO("dBm: nodedata->power=%f\n", nodedata->power);
	//PRINT_RADIO("dBm: get_power=%f\n", get_power(c));
	return 0;

 error:
    free(nodedata);
    return -1;
}

int unsetnode(call_t *c) {
	struct nodedata *nodedata = get_node_private_data(c);
	sic_signal_t* p_sic_current = nodedata->sic_signal_time_first;
	sic_signal_t* p_sic_temp = NULL;
	while(NULL != p_sic_current)
	{
		// not latest item
		if(NULL != p_sic_current->signal_next_endtime)
		{
			p_sic_current->signal_next_endtime->signal_pre_endtime = NULL;
		}
		// not highest item
		if(NULL != p_sic_current->signal_higher_power)
		{
			p_sic_current->signal_higher_power->signal_lower_power = p_sic_current->signal_lower_power;
		}
		else // is highest item, change header
		{
			nodedata->sic_signal_power_first = p_sic_current->signal_lower_power;
		}
		// not lowest item
		if(NULL != p_sic_current->signal_lower_power)
		{
			p_sic_current->signal_lower_power->signal_higher_power = p_sic_current->signal_higher_power;
		}
		nodedata->sic_signal_time_first = p_sic_current->signal_next_endtime;
		p_sic_temp = p_sic_current;
		p_sic_current = p_sic_current->signal_next_endtime;
		PRINT_RADIO("NULL==p_sic_current(%d)\n", NULL==p_sic_current);
		PRINT_RADIO("free(%d) B\n", p_sic_temp->id);
		free(p_sic_temp);
	}
	free(get_node_private_data(c));
	return 0;
}


/* ************************************************** */
/* ************************************************** */
int bootstrap(call_t *c) {
    struct nodedata *nodedata = get_node_private_data(c);
    nodedata->tx_busy = -1;
    nodedata->rx_busy = -1;
    nodedata->rxdBm = MIN_DBM;
    return 0;
}

int ioctl(call_t *c, int option, void *in, void **out) {
    return 0;
}


/* ************************************************** */
/* ************************************************** */
void cs_init(call_t *c) {
    struct nodedata *nodedata = get_node_private_data(c);
    /* log */
    if (nodedata->tx_busy != -1) {
        PRINT_REPLAY("radio-tx1 %"PRId64", c->node=%d\n", get_time(), c->node);
    }
    if (nodedata->rx_busy != -1) {
        PRINT_REPLAY("radio-rx1 %"PRId64", c->node=%d\n", get_time(), c->node);
    }
    /* init cs */
    nodedata->tx_busy = -1;
    nodedata->rx_busy = -1;
    nodedata->rxdBm = MIN_DBM;
}


/* ************************************************** */
/* ************************************************** */
void tx(call_t *c, packet_t *packet) {
	struct nodedata *nodedata = get_node_private_data(c);
	array_t *down = get_entity_bindings_down(c);
	int i = down->size;
    
    /* radio sleep */
	if (nodedata->sleep) {
		packet_dealloc(packet);
		goto END;
	}

	/* radio activity */
	cs_init(c);
	nodedata->tx_busy = packet->id;

	/* log tx */
	PRINT_REPLAY("radio-tx0 %"PRId64", c->node=%d 50\n", get_time(), c->node);

	/* transmit to antenna */
	while (i--) {
		packet_t *packet_down;

		if (i > 0) {
			packet_down = packet_clone(packet);         
		} else {
			packet_down = packet;
		}
		c->from = down->elts[i];

		PRINT_RADIO("packet_down->type=%d, radio_get_power=%f\n", packet_down->type, radio_get_power(c));
		//if(1 == packet_down->type)
		//{
			//radio_set_power(c, ADAM_HIGH_POWER_DBM_GAIN+base_power_tx);
			//PRINT_RADIO("radio_get_power=%f\n", radio_get_power(c));
		//}

		MEDIA_TX(c, packet_down);
		
		//if(1 == packet_down->type)
		//{
			//radio_set_power(c, base_power_tx);
		//}
	}

END:
    return;
}

void tx_end(call_t *c, packet_t *packet) {
	struct nodedata *nodedata = get_node_private_data(c);
	PRINT_RADIO("B: packet->id=%d, c->node=%d\n", packet->id, c->node);
	PRINT_RADIO("nodedata->tx_busy=%d\n", nodedata->tx_busy);

	/* consume energy */
	battery_consume_tx(c, packet->duration, nodedata->power);

	/* check wether the reception has killed us */
	if (!is_node_alive(c->node)) {
		goto END;
	}

	/* log tx */
	if (nodedata->tx_busy == packet->id) {
		PRINT_REPLAY("radio-tx1 %"PRId64", c->node=%d\n", get_time(), c->node);
		nodedata->tx_busy = -1;
	}
END:
	return;
}


/* ************************************************** */
/* ************************************************** */
void rx(call_t *c, packet_t *packet) {
	struct nodedata *nodedata = get_node_private_data(c);
	array_t *up = get_entity_bindings_up(c);
	int i = up->size;
	int error_id = 0;

	PRINT_RADIO("B: packet->id=%d, c->node=%d\n", packet->id, c->node);

	/* radio sleep */
	if (nodedata->sleep) {
		packet_dealloc(packet);
		error_id = 1;
		goto END;
	}

	/* half-duplex */
	if (nodedata->tx_busy != -1) {
		packet_dealloc(packet);
		error_id = 2;
		goto END;
	}
        
    /* handle carrier sense */
	adam_Update_Candidate(c);
	if (adam_Is_Packet_Decodable(c, packet->id, MEDIA_GET_WHITE_NOISE(c, packet->channel), DEFAULT_SIC_THRESHOLD)) {
		nodedata->rx_busy = -1;
		nodedata->rxdBm   = MIN_DBM;
		/* log rx */
		PRINT_REPLAY("radio-rx1 %"PRId64", c->node=%d\n", get_time(), c->node);
		/* consume energy */
		battery_consume_rx(c, packet->duration);
	} else {
		packet_dealloc(packet);
		error_id = 3;
		goto END;
	}

	/* check wether the reception has killed us */
	if (!is_node_alive(c->node)) {
		packet_dealloc(packet);
		error_id = 4;
		goto END;
	}

	/* drop packet depending on the FER */
	if (get_random_double() < packet->PER) {
		PRINT_RADIO("packet->PER=%f\n", packet->PER);
		packet_dealloc(packet);
		error_id = 5;
		goto END;
	}    

    /* forward to upper layers */
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

END:
	PRINT_RADIO("E: error_id=%d\n", error_id);
	return;
}

void cs(call_t *c, packet_t *packet) {
	struct nodedata *nodedata = get_node_private_data(c);
	sic_signal_t* sic_signal = NULL;
	int error_id = 0;
// <-RF00000000-AdamXu-2018/04/25-add log for radio
	PRINT_RADIO("B: packet->id=%d, c->node=%d\n", packet->id, c->node);
	PRINT_RADIO("nodedata->rxdBm=%f, packet->rxdBm=%f\n", nodedata->rxdBm, packet->rxdBm);
// ->RF00000000-AdamXu

    /* radio sleep */
	if (nodedata->sleep) {
		error_id = 1;
		goto END;
	}

	/* half-duplex */
	if (nodedata->tx_busy != -1) {
		error_id = 2;
		goto END;
	}

	/* check sensibility */
	if (packet->rxdBm < nodedata->mindBm) {
		error_id = 3;
		goto END;
	}

	/* check channel */
	if (nodedata->channel != packet->channel) {
		error_id = 4;
		goto END;
	}

	/* check Ts */
	if (nodedata->Ts != (packet->Tb*radio_get_modulation_bit_per_symbol(c))) {
		error_id = 5;
		goto END;
	}

	/* check channel */
	if (packet->modulation != nodedata->modulation) {
		error_id = 6;
		goto END;
	}

	/* capture effect */
	if (packet->rxdBm > nodedata->rxdBm) {
		//nodedata->rxdBm = packet->rxdBm;
		sic_signal = malloc(sizeof(sic_signal_t));
		if(NULL == sic_signal)
		{
			PRINT_RADIO("ERROR! MALLOC FAIL!\n");
			goto END;
		}
		sic_signal->id = packet->id;
		sic_signal->rxdBm = packet->rxdBm;
		sic_signal->clock1 = packet->clock1;
		sic_signal->signal_pre_endtime = NULL;
		sic_signal->signal_next_endtime = NULL;
		sic_signal->signal_higher_power = NULL;
		sic_signal->signal_lower_power = NULL;
		PRINT_RADIO("malloc(%d), packet->duration=%"PRId64", packet->real_size=%d\n", sic_signal->id, packet->duration, packet->real_size);
		adam_Insert_SIgnal2Candidate_Time(c, sic_signal);
		adam_Insert_SIgnal2Candidate_Power(c, sic_signal);
		
		nodedata->rx_busy = packet->id;
		/* log cs */
		PRINT_REPLAY("radio-rx0 %"PRId64", c->node=%d\n", get_time(), c->node);
	}
	
END:
	if(0 != error_id)
	{
		PRINT_RADIO("E: error_id=%d\n", error_id);
	}
	return;
}


/* ************************************************** */
/* ************************************************** */
double get_noise(call_t *c) {
    struct nodedata *nodedata = get_node_private_data(c);
    entityid_t *down = get_entity_links_down(c);

    c->from = down[0];
    return MEDIA_GET_NOISE(c, nodedata->channel);
}

double get_cs(call_t *c) {
	struct nodedata *nodedata = get_node_private_data(c);
	PRINT_RADIO("nodedata->rxdBm=%f\n", nodedata->rxdBm);
	return nodedata->rxdBm;
}

double get_power(call_t *c) {
	struct nodedata *nodedata = get_node_private_data(c);
	PRINT_RADIO("nodedata->power=%f\n", nodedata->power);
	return nodedata->power;
}

void set_power(call_t *c, double power) {
	struct nodedata *nodedata = get_node_private_data(c);
	nodedata->power = power;
	PRINT_RADIO("power=%f\n", power);
}

int get_channel(call_t *c) {
    struct nodedata *nodedata = get_node_private_data(c);
    return nodedata->channel;
}

void set_channel(call_t *c, int channel) {
    struct nodedata *nodedata = get_node_private_data(c);
    cs_init(c);
    nodedata->channel = channel;
}

entityid_t get_modulation(call_t *c) {
    struct nodedata *nodedata = get_node_private_data(c);
    return nodedata->modulation;
}

void set_modulation(call_t *c, entityid_t modulation) {
    struct nodedata *nodedata = get_node_private_data(c);
    cs_init(c);
    nodedata->modulation = modulation;
}

/* edit by Christophe Savigny <christophe.savigny@insa-lyon.fr> */
uint64_t get_Tb(call_t *c) {
    struct nodedata *nodedata = get_node_private_data(c);
    return (nodedata->Ts/modulation_bit_per_symbol(nodedata->modulation));
}

void set_Ts(call_t *c, uint64_t Ts) {
    struct nodedata *nodedata = get_node_private_data(c);
    cs_init(c);
    nodedata->Ts = Ts;
}

uint64_t get_Ts(call_t *c) {
    struct nodedata *nodedata = get_node_private_data(c);
    return nodedata->Ts;
}
/* end of edition */

double get_sensibility(call_t *c) {
    struct nodedata *nodedata = get_node_private_data(c);
    return nodedata->mindBm;
}

void set_sensibility(call_t *c, double sensibility) {
    struct nodedata *nodedata = get_node_private_data(c);
    cs_init(c);
    nodedata->mindBm = sensibility;
}


/* ************************************************** */
/* ************************************************** */
static void sleep(call_t *c) {
    struct nodedata *nodedata = get_node_private_data(c);
    cs_init(c);
    nodedata->sleep = 1;
}

void wakeup(call_t *c) {
    struct nodedata *nodedata = get_node_private_data(c);
    cs_init(c);
    nodedata->sleep = 0;
}


/* ************************************************** */
/* ************************************************** */
/* edit by Christophe Savigny <christophe.savigny@insa-lyon.fr> */
int get_modulation_bit_per_symbol(call_t *c){
    struct nodedata *nodedata = get_node_private_data(c);
    return modulation_bit_per_symbol(nodedata->modulation);
}
/* end of edition */


/* ************************************************** */
/* ************************************************** */
int get_header_size(call_t *c) {
    return 0;
}

int get_header_real_size(call_t *c) {
    return 0;
}

int set_header(call_t *c, packet_t *packet, destination_t *dst) {
    return 0;
}


/* ************************************************** */
/* ************************************************** */
double adam_Get_IN_MW(call_t *c, double base_noise_mw)
{
	struct nodedata *nodedata = get_node_private_data(c);
	double sum_interf_noise_mw = base_noise_mw;
	sic_signal_t* p_sic_current = NULL;
	
	// get total interference and noise
	for(p_sic_current = nodedata->sic_signal_power_first; NULL != p_sic_current; p_sic_current = p_sic_current->signal_lower_power)
	{
		sum_interf_noise_mw += dBm2mW(p_sic_current->rxdBm);
	}

	return sum_interf_noise_mw;
}

int adam_Is_Packet_Decodable(call_t *c, packetid_t id, double base_noise_mw, double sic_threshold)
{
	struct nodedata *nodedata = get_node_private_data(c);
	int is_decodable = 0;
	int signal_count = 0;
	double sum_interf_noise_mw = base_noise_mw;
	sic_signal_t* p_sic_current = NULL;

	PRINT_RADIO("B: c->node=%d, id=%d, base_noise=%f\n", c->node, id, base_noise_mw);
	PRINT_RADIO("nodedata->sic_signal_power_first==NULL?%d\n", NULL == nodedata->sic_signal_power_first);
	PRINT_RADIO("get_time=%"PRId64"\n", get_time());
	// get total interference and noise
	for(p_sic_current = nodedata->sic_signal_power_first; NULL != p_sic_current; p_sic_current = p_sic_current->signal_lower_power)
	{
		//PRINT_RADIO("NULL == p_sic_current?%d\n", NULL == p_sic_current);
		PRINT_RADIO("p_sic_current->id=%d\n", p_sic_current->id);
		PRINT_RADIO("p_sic_current->rxmW=%f\n", dBm2mW(p_sic_current->rxdBm));
		sum_interf_noise_mw += dBm2mW(p_sic_current->rxdBm);
		signal_count++;
		PRINT_RADIO("sum_interf_noise1=%f, signal_count=%d\n", sum_interf_noise_mw, signal_count);
	}
	// judge items one by one
	for(p_sic_current = nodedata->sic_signal_power_first; NULL != p_sic_current; p_sic_current = p_sic_current->signal_lower_power)
	{
		sum_interf_noise_mw -= dBm2mW(p_sic_current->rxdBm);
		PRINT_RADIO("sum_interf_noise2=%f, p_sic_current->rxmW=%f, p_sic_current->id=%d\n", sum_interf_noise_mw, dBm2mW(p_sic_current->rxdBm), p_sic_current->id);
		if(dBm2mW(p_sic_current->rxdBm) >= sum_interf_noise_mw*sic_threshold) //meet SINR threshold, continue
		{
			if(id == p_sic_current->id)
			{
				is_decodable = 1;
				break;
			}
		}
		else //cannot meet SINR threshold, quit
		{
			break;
		}
	}

	PRINT_RADIO("E: is_decodable=%d\n", is_decodable);
	return is_decodable;
}

int adam_Insert_SIgnal2Candidate_Time(call_t *c, sic_signal_t* sic_signal)
{
	struct nodedata *nodedata = get_node_private_data(c);
	adam_error_code_t error_id = ADAM_ERROR_NO_ERROR;
	sic_signal_t* p_sic_current = NULL;
	
	if(NULL == sic_signal)
	{
		error_id = ADAM_ERROR_UNEXPECTED_INPUT;
		goto END;
	}
	PRINT_RADIO("get_time=%"PRId64"\n", get_time());
	PRINT_RADIO("c->node=%d, sic_signal->id=%d\n", c->node, sic_signal->id);
	PRINT_RADIO("sic_signal->clock1=%"PRId64"\n", sic_signal->clock1);
	
	// no item
	if(NULL == nodedata->sic_signal_time_first)
	{
		nodedata->sic_signal_time_first = sic_signal;
		goto END;
	}
	// earlier than the first item
	PRINT_RADIO("nodedata->sic_signal_time_first->clock1=%"PRId64", nodedata->sic_signal_time_first->id=%d\n", nodedata->sic_signal_time_first->clock1, nodedata->sic_signal_time_first->id);
//	if(nodedata->sic_signal_time_first->clock1 > sic_signal->clock1)
//	{
//		sic_signal->signal_next_endtime = nodedata->sic_signal_time_first;
//		nodedata->sic_signal_time_first->signal_pre_endtime = sic_signal;
//		nodedata->sic_signal_time_first = sic_signal;
//	}
//	else
//	{
		for(p_sic_current = nodedata->sic_signal_time_first; NULL != p_sic_current; p_sic_current = p_sic_current->signal_next_endtime)
		{
			PRINT_RADIO("p_sic_current->clock1=%"PRId64", p_sic_current->id=%d\n", p_sic_current->clock1, p_sic_current->id);
			if(p_sic_current->clock1 > sic_signal->clock1)
			{
				sic_signal->signal_next_endtime = p_sic_current;
				sic_signal->signal_pre_endtime = p_sic_current->signal_pre_endtime;
				if(NULL != p_sic_current->signal_pre_endtime)
				{
					p_sic_current->signal_pre_endtime->signal_next_endtime = sic_signal;
				}
				else
				{
					nodedata->sic_signal_time_first = sic_signal;
				}
				p_sic_current->signal_pre_endtime = sic_signal;
				break;
			}
			// later than the last item
			if(NULL == p_sic_current->signal_next_endtime)
			{
				sic_signal->signal_pre_endtime = p_sic_current;
				p_sic_current->signal_next_endtime = sic_signal;
				break;
			}
//		}
	}
	if(NULL != p_sic_current->signal_next_endtime)
	{
		PRINT_RADIO("p_sic_current->signal_next_endtime->id=%d\n", p_sic_current->signal_next_endtime->id);
	}
	if(NULL != p_sic_current->signal_pre_endtime)
	{
		PRINT_RADIO("p_sic_current->signal_pre_endtime->id=%d\n", p_sic_current->signal_pre_endtime->id);
	}
	if(NULL != sic_signal->signal_next_endtime)
	{
		PRINT_RADIO("sic_signal->signal_next_endtime->id=%d\n", sic_signal->signal_next_endtime->id);
	}
	if(NULL != sic_signal->signal_pre_endtime)
	{
		PRINT_RADIO("sic_signal->signal_pre_endtime->id=%d\n", sic_signal->signal_pre_endtime->id);
	}
END:
	if(ADAM_ERROR_NO_ERROR != error_id)
	{
		PRINT_RADIO("E: error_id=%d\n", error_id);
	}
	return error_id;
}

int adam_Insert_SIgnal2Candidate_Power(call_t *c, sic_signal_t* sic_signal)
{
	struct nodedata *nodedata = get_node_private_data(c);
	adam_error_code_t error_id = ADAM_ERROR_NO_ERROR;
	sic_signal_t* p_sic_current = NULL;

	if(NULL == sic_signal)
	{
		error_id = ADAM_ERROR_UNEXPECTED_INPUT;
		goto END;
	}
	PRINT_RADIO("c->node=%d, sic_signal->id=%d\n", c->node, sic_signal->id);
	
	// no item
	if(NULL == nodedata->sic_signal_power_first)
	{
		nodedata->sic_signal_power_first = sic_signal;
		goto END;
	}
	// higher than the first item
	PRINT_RADIO("nodedata->sic_signal_power_first->rxdBm=%f\n", nodedata->sic_signal_power_first->rxdBm);
	PRINT_RADIO("sic_signal->rxdBm=%f\n", sic_signal->rxdBm);
//	if(nodedata->sic_signal_power_first->rxdBm < sic_signal->rxdBm)
//	{
//		sic_signal->signal_lower_power = nodedata->sic_signal_power_first;
//		nodedata->sic_signal_power_first->signal_higher_power = sic_signal;
//		nodedata->sic_signal_power_first = sic_signal;
//	}
//	else
//	{
		for(p_sic_current = nodedata->sic_signal_power_first; NULL != p_sic_current; p_sic_current = p_sic_current->signal_lower_power)
		{
			PRINT_RADIO("p_sic_current->rxdBm=%f, sic_signal->rxdBm=%f\n", p_sic_current->rxdBm, sic_signal->rxdBm);
			if(p_sic_current->rxdBm < sic_signal->rxdBm)
			{
				sic_signal->signal_lower_power = p_sic_current;
				sic_signal->signal_higher_power = p_sic_current->signal_higher_power;
				if(NULL != p_sic_current->signal_higher_power)
				{
					p_sic_current->signal_higher_power->signal_lower_power = sic_signal;
				}
				else
				{
					nodedata->sic_signal_power_first = sic_signal;
				}
				p_sic_current->signal_higher_power = sic_signal;
				break;
			}
			// lower than the last item
			if(NULL == p_sic_current->signal_lower_power)
			{
				sic_signal->signal_higher_power = p_sic_current;
				p_sic_current->signal_lower_power = sic_signal;
				break;
			}
		}
//	}
	if(NULL != p_sic_current->signal_lower_power)
	{
		PRINT_RADIO("p_sic_current->signal_lower_power->id=%d\n", p_sic_current->signal_lower_power->id);
	}
	if(NULL != p_sic_current->signal_higher_power)
	{
		PRINT_RADIO("p_sic_current->signal_higher_power->id=%d\n", p_sic_current->signal_higher_power->id);
	}
	if(NULL != sic_signal->signal_lower_power)
	{
		PRINT_RADIO("sic_signal->signal_lower_power->id=%d\n", sic_signal->signal_lower_power->id);
	}
	if(NULL != sic_signal->signal_higher_power)
	{
		PRINT_RADIO("sic_signal->signal_higher_power->id=%d\n", sic_signal->signal_higher_power->id);
	}
END:
	if(ADAM_ERROR_NO_ERROR != error_id)
	{
		PRINT_RADIO("E: error_id=%d\n", error_id);
	}
	return error_id;
}

int adam_Update_Candidate(call_t *c)
{
	struct nodedata *nodedata = get_node_private_data(c);
	adam_error_code_t error_id = ADAM_ERROR_NO_ERROR;
	uint64_t time = get_time();
	sic_signal_t* p_sic_current = nodedata->sic_signal_time_first;
	sic_signal_t* p_sic_temp = NULL;
	PRINT_RADIO("B: c->node=%d\n", c->node);
	PRINT_RADIO("get_time=%"PRId64"\n", time);

	while(NULL != p_sic_current)
	{
		PRINT_RADIO("p_sic_current->clock1=%"PRId64", p_sic_current->id=%d\n", p_sic_current->clock1, p_sic_current->id);
		if(time > p_sic_current->clock1)
		{
			// not latest item
			if(NULL != p_sic_current->signal_next_endtime)
			{
				PRINT_RADIO("p_sic_current->signal_next_endtime->id=%d\n", p_sic_current->signal_next_endtime->id);
				p_sic_current->signal_next_endtime->signal_pre_endtime = NULL;
			}
			// not highest item
			if(NULL != p_sic_current->signal_higher_power)
			{
				PRINT_RADIO("p_sic_current->signal_higher_power->id=%d\n", p_sic_current->signal_higher_power->id);
				p_sic_current->signal_higher_power->signal_lower_power = p_sic_current->signal_lower_power;
			}
			else // is highest item, change header
			{
				PRINT_RADIO("nodedata->sic_signal_power_first->id=%d\n", nodedata->sic_signal_power_first->id);
				nodedata->sic_signal_power_first = p_sic_current->signal_lower_power;
			}
			// not lowest item
			if(NULL != p_sic_current->signal_lower_power)
			{
				PRINT_RADIO("p_sic_current->signal_lower_power->id=%d\n", p_sic_current->signal_lower_power->id);
				p_sic_current->signal_lower_power->signal_higher_power = p_sic_current->signal_higher_power;
			}
			nodedata->sic_signal_time_first = p_sic_current->signal_next_endtime;
			p_sic_temp = p_sic_current;
			p_sic_current = p_sic_current->signal_next_endtime;
			PRINT_RADIO("free(%d)\n", p_sic_temp->id);
			free(p_sic_temp);
		}
		else // all outdated items have been removed, up to date now
		{
			break;
		}
	}
	
	PRINT_RADIO("E: error_id=%d\n", error_id);
	return error_id;
}


/* ************************************************** */
/* ************************************************** */
radio_methods_t methods = {rx, 
                           tx, 
                           set_header,
                           get_header_size,
                           get_header_real_size,
                           tx_end, 
                           cs,
                           get_noise,
                           get_cs,
                           get_power,
                           set_power,
                           get_channel,
                           set_channel,
                           get_modulation, 
                           set_modulation, 
                           get_Tb, 
                           get_Ts, 
                           set_Ts, 
                           get_sensibility,
                           set_sensibility, 
                           sleep, 
                           wakeup,
                           get_modulation_bit_per_symbol};
