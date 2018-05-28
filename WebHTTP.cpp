/*
 * WebHTTP.cpp
 *
 *  Created on: 21 мая 2018 г.
 *      Author: olegvedi@gmail.com
 */

#include "WebHTTP.h"
#include <iostream>
#include <cstring>

using namespace std;

void *mainThreadHandle(void *data);
int callback_dumb(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t length);

const struct lws_protocols WebHTTP::protocols[] = {
	{ NULL, callback_dumb, 0, 0, }, {
		NULL, NULL, 0, 0 } };

static WebHTTP *websock;

WebHTTP::WebHTTP()
{
    context = NULL;
    websock = this;
    memset(&con_cr_info, 0, sizeof(struct lws_context_creation_info)); /* otherwise uninitialized garbage */
    int logs = LLL_USER | LLL_ERR | LLL_WARN | LLL_NOTICE
    /* for LLL_ verbosity above NOTICE to be built into lws, lws
     * must have been configured with -DCMAKE_BUILD_TYPE=DEBUG
     * instead of =RELEASE */
    /* | LLL_INFO *//* | LLL_PARSER *//* | LLL_HEADER */
    /* | LLL_EXT *//* | LLL_CLIENT *//* | LLL_LATENCY */
    /* | LLL_DEBUG */;
    lws_set_log_level(logs, NULL);
    con_cr_info.port = CONTEXT_PORT_NO_LISTEN; /* we do not run any server */
    con_cr_info.protocols = protocols;
    con_cr_info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
//	    con_cr_info.client_ssl_ca_filepath = ssl_ca_file; // "./libwebsockets.org.cer";
    context = lws_create_context(&con_cr_info);
    if (!context) {
	MSG_ERR("Ctx create failed");
	throw 1;
    }
    main_thread = 0;
    int ret = pthread_create(&main_thread, NULL, &mainThreadHandle, this);      //run main thread
    if (ret) {
	MSG_ERR("main thread do not start");
	lws_context_destroy(context);
	context = NULL;
	throw 2;
    }
}

WebHTTP::~WebHTTP()
{
    Free();
}

void WebHTTP::Free()
{
    context = NULL;
    if (main_thread) {
	pthread_join(main_thread, NULL);
	main_thread = 0;
    }
}

void *mainThreadHandle(void *data)
{
    WebHTTP *ws = (WebHTTP *) data;
    struct lws_context *context = ws->context;

    while (ws->context) {
	int n = lws_service(context, 1000);
	if (n < 0) {
	    ws->context = NULL;
	    MSG_ERR("lws_service:%d", n);
	    break;
	}
    }

    lws_context_destroy(context);
    return NULL;
}

int WebHTTP::DisConnect(unsigned con_id)
{
//TODO
    return 0;
}

int WebHTTP::Request(bool ssl, ws_method_t method, const char *server, unsigned port, const char *service,
	const char *auth, unsigned *outBufSize)
{
    int con_id = Connect(ssl, method, server, port, service, auth);
    if (con_id < 0) {
	MSG_ERR("Connect");
	return con_id;
    }

    int ws_stat;
    int ret = -10;
    while (IsAlive()) {
	ws_stat = GetStat(con_id);
	if (WS_StatHasErrors(ws_stat)) {
	    MSG_ERR("Connection:%d", ws_stat);
	    ret = -9;
	    break;
	}
	if (WSTypeTstOR(ws_stat, WebHTTP::ws_stat_closed_fl | WebHTTP::ws_stat_completed_client_fl)) {
	    if (outBufSize) {
		*outBufSize = connectionsList[con_id].rsvBuf.size();
	    }
	    return con_id;
	}
	usleep(10000);
    }

    FreeConn(con_id);
    return ret;
}

int WebHTTP::Connect(bool ssl, ws_method_t method, const char *server, unsigned port, const char *service,
	const char *auth)
{

    if (!context) {
	MSG_ERR("Ctx is not initialized");
	return -1;
    }

    int ret = 0;
    unsigned con_id = 0;
    for (; con_id < connectionsList.size(); con_id++) {
	if (connectionsList[con_id].status == ws_stat_free) { //free slot
	    break;
	}
    }

    if (con_id == connectionsList.size()) { // no free slots
	ws_cb_con_t cct;
	cct.status = ws_stat_free;
	connectionsList.push_back(cct);
    }
    struct lws_client_connect_info *cl_con_info = &connectionsList[con_id].cl_con_info;

    try {
	memset(cl_con_info, 0, sizeof(struct lws_client_connect_info)); /* otherwise uninitialized garbage */

	if (ssl) {
	    cl_con_info->ssl_connection = LCCSCF_USE_SSL;
	    cl_con_info->ssl_connection |= LCCSCF_ALLOW_SELFSIGNED; //TODO
	}

	cl_con_info->address = server;
	cl_con_info->port = port;
	cl_con_info->context = context;
	cl_con_info->host = cl_con_info->address;
	cl_con_info->origin = cl_con_info->address;

	cl_con_info->protocol = protocols[0].name; /* "dumb-increment-protocol" */

	if (service)
	    cl_con_info->path = service;
	else
	    cl_con_info->path = "/";
	switch (method) {
	case ws_method_get:
	    cl_con_info->method = "GET";
	    break;
	case ws_method_post:
	    cl_con_info->method = "POST";
	    break;
	default:
	    throw 5;
	}
	//cl_con_info->alpn = "h2";

	connectionsList[con_id].body_part = 0;
	memset(connectionsList[con_id].boundary, 0, sizeof connectionsList[con_id].boundary);
	WSTypeAddFlag(connectionsList[con_id].status, ws_stat_connecting_fl);
	connectionsList[con_id].cur_method = method;
	connectionsList[con_id].rsvBuf.clear();
	connectionsList[con_id].id = con_id;
	if (auth) {
	    connectionsList[con_id].Authorization = auth;
	}

	cl_con_info->pwsi = &connectionsList[con_id].client_wsi;
	cl_con_info->userdata = reinterpret_cast<void *>(con_id + 1);
	lws_client_connect_via_info(cl_con_info);
	ret = con_id;
    } catch (...) {
	ret = -2;
    }

    return ret;
}

unsigned WebHTTP::calcContentSize(char *boundName)
{
    unsigned ret = 0;
    if (postBlocks.size() > 0) {
	for (unsigned i = 0; i < postBlocks.size(); i++) {
	    post_blk_t *post_blk = &postBlocks[i];
	    if (boundName) { //POST
		ret += strlen("--\xd\xaContent-Disposition: form-data; name=\"\"\xd\xa\xd\xa\xd\xa");
		ret += strlen(boundName);
	    }
	    ret += post_blk->name.size();
	    ret += post_blk->data.size();
	    ret += 1; // '=' for GET
	}
	if (boundName) { //POST
	    ret += strlen("----\xd\xa");
	    ret += strlen(boundName);
	} else {
	    ret += postBlocks.size() - 1; // '&' for GET
	}
    }
    return ret;
}

int callback_dumb(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t length)
{
    switch (reason) {
    case LWS_CALLBACK_OPENSSL_LOAD_EXTRA_CLIENT_VERIFY_CERTS:
    case LWS_CALLBACK_OPENSSL_PERFORM_SERVER_CERT_VERIFICATION:
	MSG_DBG(3, "LWS_CALLBACK_OPENSSL_VERIFY");
	break;
    default: {

	WebHTTP::ws_cb_con_t *ws_cb_con = NULL; // (WebHTTP::ws_cb_con_t *) user;
	if (user) {
	    uintptr_t id = reinterpret_cast<intptr_t>(user);
	    if (id > websock->connectionsList.size()) {
		MSG_ERR("Idx out of range:%lu", id);
//	    return -1;
	    } else {
		ws_cb_con = &websock->connectionsList[id - 1];
	    }
	}
	char buf[128];
	unsigned char **up, *uend;
	unsigned r;
	int n;

	switch (reason) {
/*
	case LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH:
	    MSG_DBG(3, "LWS_CALLBACK_CLIENT_FILTER_PRE_ESTABLISH");
	    break;
	case LWS_CALLBACK_PROTOCOL_INIT:
	    MSG_DBG(3, "LWS_CALLBACK_PROTOCOL_INIT");
	    break;
	case LWS_CALLBACK_WSI_CREATE:
	    MSG_DBG(3, "LWS_CALLBACK_WSI_CREATE");
	    break;
	case LWS_CALLBACK_GET_THREAD_ID:
	case LWS_CALLBACK_ADD_POLL_FD:
	case LWS_CALLBACK_DEL_POLL_FD:
	case LWS_CALLBACK_CHANGE_MODE_POLL_FD:
	case LWS_CALLBACK_LOCK_POLL:
	case LWS_CALLBACK_UNLOCK_POLL:
	    //MSG_DBG(3,"POLL");
	    break;
	case LWS_CALLBACK_EVENT_WAIT_CANCELLED:
	    MSG_DBG(3, "LWS_CALLBACK_EVENT_WAIT_CANCELLED");
	    break;
	    // because we are protocols[0] ...
	case LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP:
	    MSG_DBG(3, "LWS_CALLBACK_ESTABLISHED_CLIENT_HTTP");
	    break;
	case LWS_CALLBACK_SERVER_WRITEABLE:
	    MSG_DBG(3, "LWS_CALLBACK_SERVER_WRITEABLE");
	    break;
	case LWS_CALLBACK_RECEIVE:
	    MSG_DBG(3, "LWS_CALLBACK_RECEIVE");
	    break;
	case LWS_CALLBACK_ESTABLISHED:
	    MSG_DBG(3, "LWS_CALLBACK_ESTABLISHED");
	    break;
	case LWS_CALLBACK_CLIENT_ESTABLISHED:
	    MSG_DBG(3, "%s: established\n", __func__);
	    break;
	case LWS_CALLBACK_CLIENT_RECEIVE:
	    MSG_DBG(3, "RX: %s\n", (const char * )in);
	    break;
*/

	case LWS_CALLBACK_CLOSED_CLIENT_HTTP:
	    MSG_DBG(3, "LWS_CALLBACK_CLOSED_CLIENT_HTTP");
	    ws_cb_con->client_wsi = NULL;
	    WSTypeAddFlag(ws_cb_con->status, WebHTTP::ws_stat_closed_fl)
	    break;
	case LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ:
	    MSG_DBG(3, "LWS_CALLBACK_RECEIVE_CLIENT_HTTP_READ  len:%d\n", (int ) length);
	    ws_cb_con->rsvBuf.append((const char *) in, length);
#if 0  /* enable to dump the html */
	    {
		const char *p = (const char *) in;

		while (length--)
		if (*p < 0x7f)
		putchar(*p++);
		else
		putchar('.');
	    }
#endif
	    return 0;
	    break;
	case LWS_CALLBACK_RECEIVE_CLIENT_HTTP: {
	    MSG_DBG(3, "LWS_CALLBACK_RECEIVE_CLIENT_HTTP");

	    char buffer[1024 + LWS_PRE];
	    char *px = buffer + LWS_PRE;
	    int lenx = sizeof(buffer) - LWS_PRE;
	    if (lws_http_client_read(wsi, &px, &lenx) < 0)
		return -1;
	    return 0;
	}
	    break;
	case LWS_CALLBACK_COMPLETED_CLIENT_HTTP:
	    MSG_DBG(3, "LWS_CALLBACK_COMPLETED_CLIENT_HTTP");
	    WSTypeAddFlag(ws_cb_con->status, WebHTTP::ws_stat_completed_client_fl);
	    break;

	case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: //TODO add err test
	    MSG_ERR("CLIENT_CONNECTION_ERROR: %s\n", in ? (char * )in : "(null)");
	    ws_cb_con->client_wsi = NULL;
	    WSTypeAddFlag(ws_cb_con->status, WebHTTP::ws_stat_error_conn_fl)
	    break;

	case LWS_CALLBACK_CLIENT_CLOSED:
	    MSG_DBG(3, "LWS_CALLBACK_CLIENT_CLOSED");
	    ws_cb_con->client_wsi = NULL;
	    WSTypeAddFlag(ws_cb_con->status, WebHTTP::ws_stat_closed_fl)
	    break;
/*
	case LWS_CALLBACK_WSI_DESTROY:
	    MSG_DBG(3, "LWS_CALLBACK_WSI_DESTROY");
	    break;
	case LWS_CALLBACK_PROTOCOL_DESTROY:
	    MSG_DBG(3, "LWS_CALLBACK_PROTOCOL_DESTROY");
	    break;
*/
	    /* ...callbacks related to generating the POST... */
	case LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER:
	    MSG_DBG(3, "LWS_CALLBACK_CLIENT_APPEND_HANDSHAKE_HEADER");
	    WSTypeAddFlag(ws_cb_con->status, WebHTTP::ws_stat_connected_fl)

	    up = (uint8_t **) in;
	    uend = *up + length - 1;

	    if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_ACCEPT, (unsigned char *) "*/*", 3, up, uend))
		return -1;

	    if (ws_cb_con->Authorization.size()) {
		if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_AUTHORIZATION,
			(unsigned char *) ws_cb_con->Authorization.c_str(), ws_cb_con->Authorization.size(), up, uend))
		    return -1;
	    }

	    if (ws_cb_con->cur_method == WebHTTP::ws_method_get) {
		break;
	    }
	    /* generate a random boundary string */
	    lws_get_random(lws_get_context(wsi), &r, sizeof(r));
	    lws_snprintf(ws_cb_con->boundary, sizeof(ws_cb_con->boundary) - 1, "---bnd-cut-here-%08x", r);

	    n = lws_snprintf(buf, sizeof(buf) - 1, "%u", websock->calcContentSize(ws_cb_con->boundary));
	    if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_LENGTH, (unsigned char *) buf, n, up, uend))
		return -1;

	    n = lws_snprintf(buf, sizeof(buf) - 1, "multipart/form-data; boundary=%s", ws_cb_con->boundary);
	    if (lws_add_http_header_by_token(wsi, WSI_TOKEN_HTTP_CONTENT_TYPE, (uint8_t *) buf, n, up, uend))
		return 1;

	    lws_client_http_body_pending(wsi, 1);
	    lws_callback_on_writable(wsi);

	    break;
	case LWS_CALLBACK_CLIENT_HTTP_WRITEABLE: {
	    MSG_DBG(3, "LWS_CALLBACK_CLIENT_HTTP_WRITEABLE");
	    int len = LWS_PRE;
	    std::string sendBuf(len, ' ');
	    n = LWS_WRITE_HTTP;
	    if (ws_cb_con->body_part < websock->postBlocks.size()) { //TODO multiblocks
		WebHTTP::post_blk_t *post_blk = &websock->postBlocks[ws_cb_con->body_part];
		// notice every usage of the boundary starts with --
		sendBuf += "--";
		sendBuf += ws_cb_con->boundary;
		sendBuf += "\xd\xa";
		sendBuf += "Content-Disposition: form-data; name=\"";
		sendBuf += post_blk->name;
		sendBuf += "\"\xd\xa\xd\xa";
		sendBuf += post_blk->data;
		sendBuf += "\xd\xa";
		ws_cb_con->body_part++;
	    } else {
		if (ws_cb_con->body_part == websock->postBlocks.size()) {
		    sendBuf += "--";
		    sendBuf += ws_cb_con->boundary;
		    sendBuf += "--\xd\xa";
		    lws_client_http_body_pending(wsi, 0);
		    // necessary to support H2, it means we will write no more on this stream
		    n = LWS_WRITE_HTTP_FINAL;
		    ws_cb_con->body_part++;
		} else {
		    // We can get extra callbacks here, if nothing to do, then do nothing.
		    return 0;
		}
	    }
	    char *start = (&sendBuf[0]) + len;
	    len = sendBuf.size() - len;
	    //MSG_DBG(1,"len:%d \n>%s\n>%s",len,sendBuf.c_str(),start);
	    if (lws_write(wsi, (uint8_t *) start, len, (enum lws_write_protocol) n) != len)
		return 1;

	    if (n != LWS_WRITE_HTTP_FINAL)
		lws_callback_on_writable(wsi);

	    return 0;
	}

	default:
	    MSG_DBG(3, "**reason%d**\n", reason);
	    break;
	}
    }
    }
    return 0;
}
