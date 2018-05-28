#include <iostream>
#include "WebHTTP.h"

#include "priv.h"

int main()
{
    WebHTTP http_ws;

    http_ws.PostBlkClear();
    http_ws.PostBlkAddLine("id", "1");
    http_ws.PostBlkAddLine("name", "BigBoy");

    unsigned outBufSize;

    //int con_id = http_ws.Request(true , WebHTTP::ws_method_post, "httpbin.org", 443, "/post", NULL, &outBufSize);
    //Async request
    int con_id1 = http_ws.Connect(true, WebHTTP::ws_method_post, "httpbin.org", 443, "/post", NULL);

    //Sync request
    int con_id2 = http_ws.Request(false, WebHTTP::ws_method_get, "httpbin.org", 80, "/get?id=1&name=BigBoy", NULL,
	    &outBufSize);
    if (con_id2 < 0) {
	MSG_ERR("Request 2");
    } else {

	if (outBufSize > 0) {
	    const char *outbuf = http_ws.GetBuf(con_id2);
	    MSG_DBG(1, ">>%s", outbuf);
	} else {
	    MSG_DBG(1, "Server no return data");
	}
	http_ws.FreeConn(con_id2);
    }

    //return to first request
    if (con_id1 < 0) {
	MSG_ERR("Request 1");
    } else {

	int stat;
	while (http_ws.IsAlive()) {
	    stat = http_ws.GetStat(con_id1);
	    if (WS_StatHasErrors(stat)) {
		MSG_ERR("Connection:%d", stat);
		break;
	    }
	    if (WSTypeTstOR(stat, WebHTTP::ws_stat_closed_fl | WebHTTP::ws_stat_completed_client_fl)) {
		if (outBufSize) {
		    const char *outbuf = http_ws.GetBuf(con_id1);
		    if (outbuf) {
			MSG_DBG(1, ">>%s", outbuf);
		    } else {
			MSG_DBG(1, "Server no return data");
		    }

		}
		break;
	    }
	    usleep(10000);
	}
	http_ws.FreeConn(con_id1);
    }

}
