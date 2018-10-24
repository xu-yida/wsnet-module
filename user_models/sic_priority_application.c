/**
 *  \file   sic_priority_application.c
 *  \brief  sic application
 *  \author adam xu
 *  \date   2018
 **/
#include <stdio.h>

#include <include/modelutils.h>

#include <sic.h>


/* ************************************************** */
/* ************************************************** */
model_t model =  {
    "SIC application",
    "Adam Xu",
    "0.1",
    MODELTYPE_APPLICATION, 
    {NULL, 0}
};


/* ************************************************** */
/* ************************************************** */
struct _sic_private {
	uint64_t start;
	uint64_t period;
	int size;
	double priority_ratio;
	nodeid_t destination;
	position_t position;
	int overhead;
};

/* ************************************************** */
/* ************************************************** */
#ifdef ADAM_TEST
int g_num_t = 0;
int g_num_t_priority0 = 0;
int g_num_t_priority1 = 0;
#endif//ADAM_TEST

/* ************************************************** */
/* ************************************************** */
int callmeback(call_t *c, void *args);
void tx(call_t *c);


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
    struct _sic_private *nodedata = malloc(sizeof(struct _sic_private));
    param_t *param;
        
    /* default values */
    nodedata->destination = get_random_node(c->node);
    nodedata->position.x = get_random_x_position();
    nodedata->position.y = get_random_y_position();
    nodedata->position.z = get_random_z_position();
    nodedata->start = 0;
    nodedata->period = 1000000000;
    nodedata->size = 1000;
    nodedata->priority_ratio = ADAM_HIGH_PRIOTITY_RATIO;

    /* get parameters */
    das_init_traverse(params);
    while ((param = (param_t *) das_traverse(params)) != NULL) {

        if (!strcmp(param->key, "destination")) {
            if (get_param_nodeid(param->value, &(nodedata->destination), c->node)) {
                goto error;
            }
        }
        if (!strcmp(param->key, "destination-x")) {
            if (get_param_x_position(param->value, &(nodedata->position.x))) {
                goto error;
            }
        }
        if (!strcmp(param->key, "destination-y")) {
            if (get_param_y_position(param->value, &(nodedata->position.y))) {
                goto error;
            }
        }
        if (!strcmp(param->key, "destination-z")) {
            if (get_param_z_position(param->value, &(nodedata->position.z))) {
                goto error;
            }
        }
        if (!strcmp(param->key, "start")) {
            if (get_param_time(param->value, &(nodedata->start))) {
                goto error;
            }
        }
        if (!strcmp(param->key, "period")) {
            if (get_param_time(param->value, &(nodedata->period))) {
                goto error;
            }
        }
        if (!strcmp(param->key, "size")) {
            if (get_param_integer(param->value, &(nodedata->size))) {
                goto error;
            }
        }
        if (!strcmp(param->key, "priority_ratio")) {
            if (get_param_double(param->value, &(nodedata->priority_ratio))) {
                goto error;
            }
        }
    }
    
    set_node_private_data(c, nodedata);
    return 0;
  
 error:
    free(nodedata);
    return -1;
}

int unsetnode(call_t *c) {
    struct _sic_private *nodedata = get_node_private_data(c);
	
#ifdef ADAM_TEST
	//PRINT_RESULT("%d packets transmitted\n", g_num_t);
	//PRINT_RESULT("%d prioriy0 packets transmitted, %d prioriy1 packets transmitted\n", g_num_t_priority0, g_num_t_priority1);
	PRINT_RESULT("%d, %d, %d, ", g_num_t, g_num_t_priority0, g_num_t_priority1);
#endif//ADAM_TEST

    free(nodedata);
    
    return 0;
}


/* ************************************************** */
/* ************************************************** */
int bootstrap(call_t *c) {
	struct _sic_private *nodedata = get_node_private_data(c);
	array_t *down = get_entity_bindings_down(c);
	call_t c0 = {down->elts[0], c->node, c->entity};
	// <-RF00000000-AdamXu-2018/07/06-test sic.
#if 1//def ADAM_TEST
	uint64_t start = get_time() + nodedata->start + nodedata->period;
#else
	uint64_t start = get_time() + nodedata->start + get_random_double() * nodedata->period;
#endif
	// ->RF00000000-AdamXu

	/* get overhead */
	nodedata->overhead = GET_HEADER_SIZE(&c0);

	/* eventually schedule callback */
	scheduler_add_callback(start, c, callmeback, NULL);

	return 0;
}

int ioctl(call_t *c, int option, void *in, void **out) {
    return 0;
}


/* ************************************************** */
/* ************************************************** */
int callmeback(call_t *c, void *args) {
    struct _sic_private *nodedata = get_node_private_data(c);

    scheduler_add_callback(get_time() + nodedata->period, c, callmeback, NULL);
    tx(c);

    return 0;
}


/* ************************************************** */
/* ************************************************** */
void tx(call_t *c) {
	struct _sic_private *nodedata = get_node_private_data(c);
	array_t *down = get_entity_bindings_down(c);
	packet_t *packet = packet_create(c, nodedata->size + nodedata->overhead, -1);
	call_t c0 = {down->elts[0], c->node, c->entity};
	destination_t destination = {nodedata->destination, 
	                             {nodedata->position.x, 
	                              nodedata->position.y, 
	                              nodedata->position.z}};

#ifndef ADAM_RESULT_LOG
	printf("[SIC APP] node %d transmitted a data packet at %"PRId64": desination id=%d  \n", c->node, get_time(), destination.id);
#endif//ADAM_RESULT_LOG
	PRINT_APPLICATION("B: packet->id=%d, c->node=%d, destination.id=%d\n", packet->id, c->node, destination.id);
	PRINT_APPLICATION("get_time()=%"PRId64"\n", get_time());
	// add priority here
	if(1 > nodedata->priority_ratio)
	{
		packet->type = (get_random_double()<nodedata->priority_ratio)?1:0;
	}
	else if(0 <= nodedata->priority_ratio)
	{
		packet->type = 0;
	}
	else
	{
		packet->type = 1;
	}
	PRINT_APPLICATION("packet->type=%d\n", packet->type);
#ifdef ADAM_TEST
	if(0 == packet->type)
	{
		g_num_t_priority0++;
	}
	else if(1 == packet->type)
	{
		g_num_t_priority1++;
	}
	g_num_t++;
#endif//ADAM_TEST
	if (SET_HEADER(&c0, packet, &destination) == -1) {
		packet_dealloc(packet);
		return;
	}

	TX(&c0, packet);
}


/* ************************************************** */
/* ************************************************** */
void rx(call_t *c, packet_t *packet) {  
#ifndef ADAM_RESULT_LOG
	printf("[SIC APP] node %d received a data packet at %"PRId64": rxdBm=%lf \n", c->node, get_time(), packet->rxdBm);
#endif//ADAM_RESULT_LOG
	packet_dealloc(packet);
}


/* ************************************************** */
/* ************************************************** */
application_methods_t methods = {rx};
