/**
 *  \file   sic.c
 *  \brief  Successive interference cancellation radio model
 *  \author Adam Xu
 *  \date   2018
 **/
#include <include/modelutils.h>


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
	int sic_iteration_limit;
	int sic_iteration_current;
	int sic_remainder;
    int tx_busy;
	// sic_iteration_limit decide size of rx_busy
    int* rx_busy;
    double rxdBm;
};


/* ************************************************** */
/* ************************************************** */
void cs_init(call_t *c);


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
	
    nodedata->sic_iteration_limit = MIN_SIC_ITERATION;
    nodedata->sic_remainder = MIN_SIC_REMAIN;
    nodedata->sic_iteration_current = 0;

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
// <-RF00000000-AdamXu-2018/05/02-SIC malloc and free
        PRINT_RADIO("SIC setnode: malloc\n");
// ->RF00000000-AdamXu
	nodedata->rx_busy = malloc(nodedata->sic_iteration_limit*sizeof(int));
	if(NULL == nodedata->rx_busy)
	{
// <-RF00000000-AdamXu-2018/05/02-SIC malloc and free
	        PRINT_RADIO("ERROR! SIC setnode: malloc\n");
// ->RF00000000-AdamXu
		goto error;
	}
    return 0;

 error:
    free(nodedata);
    return -1;
}

int unsetnode(call_t *c) {
    free(get_node_private_data(c));
// <-RF00000000-AdamXu-2018/05/02-SIC malloc and free
        PRINT_RADIO("SIC unsetnode: sic_free\n");
// ->RF00000000-AdamXu
	free(nodedata->rx_busy);
    return 0;
}


/* ************************************************** */
/* ************************************************** */
int bootstrap(call_t *c) {
    struct nodedata *nodedata = get_node_private_data(c);
    nodedata->tx_busy = -1;
    nodedata->rx_busy = NULL;
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
        PRINT_REPLAY("radio-tx1 %"PRId64" %d\n", get_time(), c->node);
    }
    if (nodedata->rx_busy != NULL) {
        PRINT_REPLAY("radio-rx1 %"PRId64" %d\n", get_time(), c->node);
    }
    /* init cs */
    nodedata->tx_busy = -1;
	memset(nodedata->rx_busy, -1, nodedata->sic_iteration_limit*sizeof(int));
// <-RF00000000-AdamXu-2018/04/30-add log for sic
	{
		int count = 0;
		while(count < nodedata->sic_iteration_limit)
		{
			PRINT_RADIO("SIC cs_init: rx_busy[%d]=%d\n", count, rx_busy[count]);
			count++;
		}
	}
// ->RF00000000-AdamXu
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
        return;
    }

    /* radio activity */
    cs_init(c);
    nodedata->tx_busy = packet->id;

    /* log tx */
    PRINT_REPLAY("radio-tx0 %"PRId64" %d 50\n", get_time(), c->node);

    /* transmit to antenna */
    while (i--) {
        packet_t *packet_down;
        
        if (i > 0) {
            packet_down = packet_clone(packet);         
        } else {
            packet_down = packet;
        }
        c->from = down->elts[i];
        MEDIA_TX(c, packet_down);
    }

    return;
}

void tx_end(call_t *c, packet_t *packet) {
    struct nodedata *nodedata = get_node_private_data(c);
    
    /* consume energy */
    battery_consume_tx(c, packet->duration, nodedata->power);
    
    /* check wether the reception has killed us */
    if (!is_node_alive(c->node)) {
        return;
    }

    /* log tx */
    if (nodedata->tx_busy == packet->id) {
        PRINT_REPLAY("radio-tx1 %"PRId64" %d\n", get_time(), c->node);
        nodedata->tx_busy = -1;
    }

    return;
}


/* ************************************************** */
/* ************************************************** */
void rx(call_t *c, packet_t *packet) {
    struct nodedata *nodedata = get_node_private_data(c);
    array_t *up = get_entity_bindings_up(c);
    int i = up->size;

    /* radio sleep */
    if (nodedata->sleep) {
        packet_dealloc(packet);
        return;
    }

    /* half-duplex */
    if (nodedata->tx_busy != -1) {
        packet_dealloc(packet);
        return;
    }
    
    /* handle carrier sense */
    	{
    		int count = 0;
		int flag_find = 0;
		while(count < nodedata->sic_iteration_current)
		{
			if (nodedata->rx_busy[count] == packet->id) {
			    nodedata->rx_busy[count] = -1;
			    nodedata->rxdBm   = MIN_DBM;
				nodedata->sic_iteration_current--;
				if(nodedata->sic_iteration_current < 0)
				{
					// <-RF00000000-AdamXu-2018/04/30-add log for sic
					PRINT_RADIO("ERROR! SIC rx: sic_iteration_current=%d\n", nodedata->sic_iteration_current);
					// ->RF00000000-AdamXu
					nodedata->sic_iteration_current = 0;
				}
				flag_find++;
			    /* log rx */
			    PRINT_REPLAY("radio-rx1 %"PRId64" %d\n", get_time(), c->node);
			    /* consume energy */
			    battery_consume_rx(c, packet->duration);
			}
			count++;
		}
		PRINT_RADIO("SIC rx: flag_find=%d\n", flag_find);
		if(0 == flag_find)
		{
		    packet_dealloc(packet);
		    return;
		}
    	}

    /* check wether the reception has killed us */
    if (!is_node_alive(c->node)) {
        packet_dealloc(packet);
        return;
    }

    /* drop packet depending on the FER */
    if (get_random_double() < packet->PER) {
        packet_dealloc(packet);
        return;
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

    return;
}

void cs(call_t *c, packet_t *packet) {
    struct nodedata *nodedata = get_node_private_data(c);
// <-RF00000000-AdamXu-2018/04/25-add log for radio
        PRINT_RADIO("SIC B: radio-rx0 %"PRId64" %d, packet->rxdBm=%f, nodedata->rxdBm=%f, packet->id=%d\n", get_time(), c->node, packet->rxdBm, nodedata->rxdBm, packet->id);
// ->RF00000000-AdamXu

    /* radio sleep */
    if (nodedata->sleep) {
        return;
    }

    /* half-duplex */
    if (nodedata->tx_busy != -1) {
        return;
    }

    /* check sensibility */
    if (packet->rxdBm < nodedata->mindBm) {
        return;
    }

    /* check channel */
    if (nodedata->channel != packet->channel) {
        return;
    }

    /* check Ts */
    if (nodedata->Ts != (packet->Tb*radio_get_modulation_bit_per_symbol(c))) {
        return;
    }

    /* check channel */
    if (packet->modulation != nodedata->modulation) {
        return;
    }

    /* capture effect */
    if (packet->rxdBm > nodedata->rxdBm && nodedata->sic_iteration_current <= nodedata->sic_iteration_limit) {
        nodedata->rxdBm += packet->rxdBm;
	{
		int count = 0;
		while(count < nodedata->sic_iteration_limit)
		{
			if(-1 == nodedata->rx_busy[count])
			{
				nodedata->rx_busy[count] = packet->id;
				nodedata->sic_iteration_current++;
			}
			PRINT_RADIO("SIC cs: nodedata->rx_busy[%d]=%d\n", count, nodedata->rx_busy[count]);
			count++;
		}
	}
// <-RF00000000-AdamXu-2018/04/25-add log for radio
        PRINT_RADIO("SIC E: radio-rx0 %"PRId64" %d, packet->rxdBm=%f, nodedata->rxdBm=%f, packet->id=%d\n", get_time(), c->node, packet->rxdBm, nodedata->rxdBm, packet->id);
// ->RF00000000-AdamXu
        /* log cs */
        PRINT_REPLAY("radio-rx0 %"PRId64" %d\n", get_time(), c->node);
        return;
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
    return nodedata->rxdBm;
}

double get_power(call_t *c) {
    struct nodedata *nodedata = get_node_private_data(c);
    return nodedata->power;
}

void set_power(call_t *c, double power) {
    struct nodedata *nodedata = get_node_private_data(c);
    nodedata->power = power;
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
