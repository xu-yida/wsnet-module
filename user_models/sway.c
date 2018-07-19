/**
 *  \file   sway.c
 *  \brief  Sway mobility
 *  \author Adam Xu
 *  \date   2018
 **/
#include <include/modelutils.h>


/* ************************************************** */
/* ************************************************** */
model_t model =  {
    "Sway mobility",
    "Adam Xu",
    "0.1",
    MODELTYPE_MOBILITY, 
    {NULL, 0}
};


/* ************************************************** */
/* ************************************************** */
struct entitydata {
	double max_speed;
};

struct nodedata {
	uint64_t lupdate;
	double speed;
	angle_t angle;
	position_t base_position;
	double range_x;
	double range_y;
	double range_z;
};


/* ************************************************** */
/* ************************************************** */
int init(call_t *c, void *params) {
    struct entitydata *entitydata = malloc(sizeof(struct entitydata));
    param_t *param;

    /* default values */
    entitydata->max_speed = 30;

    /* get parameters */
    das_init_traverse(params);
    while ((param = (param_t *) das_traverse(params)) != NULL) {
        if (!strcmp(param->key, "max-speed")) {
            if (get_param_double(param->value, &(entitydata->max_speed))) {
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
    struct entitydata *entitydata = get_entity_private_data(c);
    param_t *param;
    
    /* default values */
    get_node_position(c->node)->x = get_random_x_position();
    get_node_position(c->node)->y = get_random_y_position();
    get_node_position(c->node)->z = get_random_z_position();
    nodedata->speed = get_random_double_range(0, entitydata->max_speed);
    nodedata->angle.xy = get_random_double_range(0, 2 * M_PI);
    nodedata->angle.z = get_random_double_range(0, 2 * M_PI);

    /* get parameters */
    das_init_traverse(params);
    while ((param = (param_t *) das_traverse(params)) != NULL) {
        if (!strcmp(param->key, "x")) {
            if (get_param_x_position(param->value, &(get_node_position(c->node)->x))) {
                goto error;
            }
        }
        if (!strcmp(param->key, "y")) {
            if (get_param_y_position(param->value, &(get_node_position(c->node)->y))) {
                goto error;
            }
        }
        if (!strcmp(param->key, "z")) {
            if (get_param_z_position(param->value, &(get_node_position(c->node)->z))) {
                goto error;
            }
        }
        if (!strcmp(param->key, "speed")) {
            if (get_param_double_range(param->value, &(nodedata->speed), 0, entitydata->max_speed)) {
                goto error;
            }
        }
        if (!strcmp(param->key, "angle-xy")) {
            if (get_param_double_range(param->value, &(nodedata->angle.xy), 0, 2*M_PI)) {
                goto error;
            }
        }
        if (!strcmp(param->key, "angle-z")) {
            if (get_param_double_range(param->value, &(nodedata->angle.z), 0, 2*M_PI)) {
                goto error;
            }
        }
        if (!strcmp(param->key, "rang-x")) {
            if (get_param_double(param->value, &(nodedata->range_x))) {
                goto error;
            }
        }
        if (!strcmp(param->key, "rang-y")) {
            if (get_param_double(param->value, &(nodedata->range_y))) {
                goto error;
            }
        }
        if (!strcmp(param->key, "rang-z")) {
            if (get_param_double(param->value, &(nodedata->range_z))) {
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
    free(get_node_private_data(c));
    return 0;
}


/* ************************************************** */
/* ************************************************** */
int bootstrap(call_t *c) {
    struct nodedata *nodedata = get_node_private_data(c);

    PRINT_REPLAY("mobility %"PRId64" %d %lf %lf %lf\n", get_time(), c->node, 
               get_node_position(c->node)->x, get_node_position(c->node)->y, 
               get_node_position(c->node)->z);
    nodedata->lupdate = get_time();

    return 0;
}

int ioctl(call_t *c, int option, void *in, void **out) {
    return 0;
}


/* ************************************************** */
/* ************************************************** */
void update_position(call_t *c) {
    struct nodedata *nodedata = get_node_private_data(c);
    position_t *pos = get_node_position(c->node);
    position_t *area = get_topology_area();
    double x_max = MIN(pos->x+nodedata->range_x, area->x);
    double x_min = MAX(pos->x-nodedata->range_x, 0);
    double y_max = MIN(pos->y+nodedata->range_y, area->y);
    double y_min = MAX(pos->y-nodedata->range_y, 0);
    double z_max = MIN(pos->z+nodedata->range_z, area->z);
    double z_min = MAX(pos->z-nodedata->range_z, 0);
        
    while  (nodedata->lupdate < get_time()) {
        uint64_t t_0, t_1 = 0, t_2 = 0, t_3 = 0, t_4 = 0, t_5 = 0, t_6 = 0;
        int bounce = 0;
        
        if (cos(nodedata->angle.xy)*cos(nodedata->angle.z) != 0)
            t_1 = ((x_min - pos->x) / (nodedata->speed * cos(nodedata->angle.xy)*cos(nodedata->angle.z))) * 1000000000;
        if (cos(nodedata->angle.xy)*cos(nodedata->angle.z) != 0)
            t_2 = ((x_max - pos->x) / (nodedata->speed * cos(nodedata->angle.xy)*cos(nodedata->angle.z))) * 1000000000;
        if (sin(nodedata->angle.xy)*cos(nodedata->angle.z) != 0)
            t_3 = ((y_min - pos->y) / (nodedata->speed * sin(nodedata->angle.xy)*cos(nodedata->angle.z))) * 1000000000;
        if (sin(nodedata->angle.xy)*cos(nodedata->angle.z) != 0)
            t_4 = ((y_max - pos->y) / (nodedata->speed * sin(nodedata->angle.xy)*cos(nodedata->angle.z))) * 1000000000;
        if (sin(nodedata->angle.z) != 0)
            t_5 = ((z_min - pos->z) / (nodedata->speed * sin(nodedata->angle.z))) * 1000000000;
        if (sin(nodedata->angle.z) != 0)
            t_6 = ((z_max - pos->z) / (nodedata->speed * sin(nodedata->angle.z))) * 1000000000;

        t_0 = get_time() - nodedata->lupdate;
        if ((t_1 > 0) && (t_1 < t_0)) {
            t_0 = t_1;
            bounce = 1;
        }
        if ((t_2 > 0) && (t_2 < t_0)) {
            t_0 = t_2;
            bounce = 2;
        }
        if ((t_3 > 0) && (t_3 < t_0)) {
            t_0 = t_3;
            bounce = 3;
        }
        if ((t_4 > 0) && (t_4 < t_0)) {
            t_0 = t_4;
            bounce = 4;
        }
        if ((t_5 > 0) && (t_5 < t_0)) {
            t_0 = t_5;
            bounce = 5;
        }
        if ((t_6 > 0) && (t_6 < t_0)) {
            t_0 = t_6;
            bounce = 6;
        }

        pos->x = (nodedata->speed * cos(nodedata->angle.xy)*cos(nodedata->angle.z)*t_0 / 1000000000 + pos->x);
        pos->y = (nodedata->speed * sin(nodedata->angle.xy)*cos(nodedata->angle.z)*t_0 / 1000000000 + pos->y);
        pos->z = (nodedata->speed * sin(nodedata->angle.z)*t_0 / 1000000000 + pos->z);

        if (pos->x > x_max)
            pos->x = x_max;
        if (pos->y > y_max)
            pos->y = y_max;
        if (pos->z > z_max)
            pos->z = z_max;
        if (pos->x < x_min)
            pos->x = x_min;
        if (pos->y < y_min)
            pos->y = y_min;
        if (pos->z < z_min)
            pos->z = z_min;
    
        /* if t_0 is g_time - last_update we have to leave  the loop.
         * we have to check this because of the imprecision on the double addition */
         
        nodedata->lupdate += t_0;
        if (nodedata->lupdate > get_time()) {
            nodedata->lupdate = get_time();
        }

        if (bounce == 1 || bounce == 2) {
            nodedata->angle.xy = M_PI - nodedata->angle.xy;
        }                 
        if (bounce == 3 || bounce == 4) {
            nodedata->angle.xy = 0 - nodedata->angle.xy;
        }                 
        if (bounce == 5 || bounce == 6) {
            nodedata->angle.z = 0 - nodedata->angle.z;
        }                 
        if (nodedata->angle.xy < 0) {
            nodedata->angle.xy += 2*M_PI;
        }
        if (nodedata->angle.xy >= 2*M_PI) {
            nodedata->angle.xy -= 2*M_PI;  
        }      
        
        PRINT_REPLAY("mobility %"PRId64" %d %lf %lf %lf\n", nodedata->lupdate, c->node, get_node_position(c->node)->x, get_node_position(c->node)->y, get_node_position(c->node)->z);
    }

    return;
}


/* ************************************************** */
/* ************************************************** */
mobility_methods_t methods = {update_position};
