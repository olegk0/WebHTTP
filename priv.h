#ifndef __PRIV_H_
#define __PRIV_H_

#include <stdio.h>


//#define DEBUG 1

#define MSG_ERR(format, ...) fprintf(stderr, "(%s): Error - " format "\n",__PRETTY_FUNCTION__ ,##__VA_ARGS__)

#ifdef DEBUG
#define DBG_LVL_DEF 2
#define MSG_DBG(dl, format, ...) do{if(dl <= DBG_LVL_DEF ) fprintf(stderr, "(%s):" format "\n", __PRETTY_FUNCTION__, ##__VA_ARGS__);}while(0)
#else
#define MSG_DBG(dl, format, ...)
#endif

#endif
