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
	nodeid_t node;
	double rxdBm;
}sic_signal_t;

// <-RF00000000-AdamXu-2018/05/07-use qsort incremently on double numbers.
// base: array for sort
// len: length of base
// return: error code
int adam_Qsort_Inc_Double(double* base, int len);
// ->RF00000000-AdamXu

#endif //__radio_public__
