/*
 * WebHTTP.h
 *
 *  Created on: 21 мая 2018 г.
 *      Author: olegvedi@gmail.com
 */

#ifndef WEBHTTP_H_
#define WEBHTTP_H_

#include <libwebsockets.h>
#include <string>
#include <vector>

#include "priv.h"

#define WSTypeNum(x) (1 << (x))
#define WSErrorsFlBg 8
#define WS_StatHasErrors(stat)	((stat) & (0xFF << WSErrorsFlBg))
#define WSTypeTstAND(var, fl) (((var) & (fl)) == (fl))
#define WSTypeTstOR(var, fl) ((var) & (fl))
#define WSTypeAddFlag(stat, fl) {stat |= (int)fl;}

class WebHTTP {
    typedef struct {
	std::string name;
	std::string data;
    } post_blk_t;
public:
    typedef enum {
	ws_stat_none = -1,
	ws_stat_free = 0,
	ws_stat_connecting_fl = WSTypeNum(0),
	ws_stat_connected_fl = WSTypeNum(1),
	ws_stat_closed_fl = WSTypeNum(2),
	ws_stat_completed_client_fl = WSTypeNum(3),

//	ws_stat_errors,
	ws_stat_error_conn_fl = WSTypeNum(WSErrorsFlBg),
    } ws_stat_t;

    typedef enum {
	ws_method_none = 0,
	ws_method_get,
	ws_method_post,
    } ws_method_t;

    WebHTTP();
    virtual ~WebHTTP();
    void Free();

    int Connect(bool ssl, ws_method_t method, const char *server, unsigned port, const char *service, const char *auth =
	    NULL);
    int DisConnect(unsigned con_id);
    int Request(bool ssl, ws_method_t method, const char *server, unsigned port, const char *service, const char *auth =
	    NULL, unsigned *outBufSize = NULL);

    void PostBlkClear()
    {
	postBlocks.clear();
    }
    void PostBlkAddLine(const char *name, const char *data)
    {
	post_blk_t post_blk;

	post_blk.data = data;
	post_blk.name = name;
	postBlocks.push_back(post_blk);
    }

    int FreeConn(unsigned con_id)
    {
	if (con_id >= connectionsList.size()) {
	    MSG_ERR("Index out of range");
	    return -1;
	}
	connectionsList[con_id].status = ws_stat_free;
	connectionsList[con_id].rsvBuf.clear();
	connectionsList[con_id].Authorization.clear();
	return 0;
    }

    const char *GetBuf(unsigned con_id)
    {
	if (con_id >= connectionsList.size()) {
	    MSG_ERR("Index out of range");
	    return NULL;
	}
	if (connectionsList[con_id].rsvBuf.size() == 0)
	    return NULL;
	return connectionsList[con_id].rsvBuf.c_str();
    }
    int GetStat(unsigned con_id)
    {
	if (con_id >= connectionsList.size()) {
	    MSG_ERR("Index out of range");
	    return -1;
	}
	return connectionsList[con_id].status;
    }
    inline bool IsAlive()
    {
	return context != NULL;
    }

private:
    typedef struct {
	char boundary[32];
	unsigned body_part;
	struct lws *client_wsi;
	struct lws_client_connect_info cl_con_info;
	int status;
	ws_method_t cur_method;
	std::string rsvBuf;
	std::string Authorization;
	unsigned id;
    } ws_cb_con_t;

    static const struct lws_protocols protocols[2];
    struct lws_context *context;
    struct lws_context_creation_info con_cr_info;

    std::vector<ws_cb_con_t> connectionsList;
    std::vector<post_blk_t> postBlocks;

    pthread_t main_thread;

    unsigned calcContentSize(char *boundName);
    friend void *mainThreadHandle(void *data);
    friend int callback_dumb(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len);
};

#endif /* WEBHTTP_H_ */
