#pragma once
#include <thread>
#include <event.h>
#include <event2/http.h>
#include <event2/listener.h>
#include <event2/thread.h>
#include "httplistener.h"
#include "log.h"
#ifndef WIN32
#include <arpa/inet.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#endif
LibeventWorker::LibeventWorker(HttpServerListener* pListener, evutil_socket_t nfd):m_pService(pListener), m_nfd(nfd)
{

}

LibeventWorker::~LibeventWorker()
{
	stop();
}

int  LibeventWorker::start()
{
	if (m_pBase == nullptr)
	{
		m_pBase = event_base_new();
	}
	if (m_pHttp == nullptr)
	{
		m_pHttp = evhttp_new(m_pBase);
	}
	evhttp_set_gencb(m_pHttp, httpRequestHandle, m_pService);
	evhttp_accept_socket(m_pHttp, m_nfd);
	std::thread t = std::thread([](event_base *base) {event_base_loop(base, EVLOOP_NO_EXIT_ON_EMPTY);}, m_pBase);
	t.detach();
	return 0;
}

int  LibeventWorker::stop()
{
	if (m_pBase)
	{
		event_base_loopbreak(m_pBase);
		event_base_free(m_pBase);
		m_pBase = nullptr;
	}
	if (m_pHttp)
	{
		evhttp_free(m_pHttp);
		m_pHttp = nullptr;
	}
	return 0;
}

void  LibeventWorker::httpRequestHandle(struct evhttp_request* request, void* arg)
{
	HttpServerListener * pService = static_cast<HttpServerListener *>(arg);
	const char * uri = evhttp_request_uri(request);
	char * body = (char *)EVBUFFER_DATA(request->input_buffer);
	unsigned int len = evbuffer_get_length(request->input_buffer);
	if (body && len > 0) body[len] = '\0';
	pService->onHttpRequest(CHttpRequest(request->type, request), uri, body, len);
	//else LERROR("unknwon http request");		
}

////////////////////////////////////////////////////////////////////////////////////////////////////////

HttpListener::HttpListener(HttpServerListener* pListener, int num):m_pService(pListener)
{
#ifdef WIN32
	WSAData wsaData;
	WSAStartup(MAKEWORD(2, 0), &wsaData);
	evthread_use_windows_threads();
	struct event_config* cfg = event_config_new();
	event_config_set_flag(cfg, EVENT_BASE_FLAG_STARTUP_IOCP);
#else
	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
		return;
	evthread_use_pthreads();    //unix…œ…Ë÷√
#endif
	int capacity = 1;
	while (capacity < num)
		capacity <<= 1;
	m_nCount = capacity;
}

HttpListener::~HttpListener()
{
	stop();
}

int HttpListener::start(const char* pHost, int nPort)
{
	event_init();
	if (bindSocket(pHost, nPort) < 0) return -1;
	for (int i = 0; i < m_nCount; i++)
	{
		LibeventWorker * pWorker = new LibeventWorker(m_pService, m_nfd);
		m_vecWorker.push_back(pWorker);
		pWorker->start();
	}
	return 0;
}

int HttpListener::bindSocket(const char* pHost, int nPort)
{
	m_nfd = socket(AF_INET, SOCK_STREAM, 0);
	if (m_nfd < 0) return -1;

	int one = 1;
	setsockopt(m_nfd, SOL_SOCKET, SO_REUSEADDR, (char *)&one, sizeof(int));

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
#if defined(WIN32)
	addr.sin_addr.S_un.S_addr = inet_addr(pHost);
#else
	addr.sin_addr.s_addr = inet_addr(pHost);
#endif
	addr.sin_port = htons(nPort);

	if (bind(m_nfd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
	{
		LERROR("bind socket error:{}", m_nfd);
		return -1;
	}
	if (listen(m_nfd, 1024) < 0)
	{
		LERROR("listen error:{}", m_nfd);
		return -1;
	}
#if defined(WIN32)
	unsigned long ul = 1;
	int ret = ioctlsocket(m_nfd, FIONBIO, (unsigned long *)&ul);
#else
	int flags;
	if ((flags = fcntl(m_nfd, F_GETFL, 0)) < 0
		|| fcntl(m_nfd, F_SETFL, flags | O_NONBLOCK) < 0)
		return -1;
#endif	
	return 0;
}

int HttpListener::stop()
{
	while (m_vecWorker.size()>0)
	{
		LibeventWorker * pWorker = m_vecWorker.back();
		pWorker->stop();
		delete pWorker;
		m_vecWorker.pop_back();
	}
	if (m_pService) m_pService = nullptr;
#if defined(WIN32)
	if (m_nfd > 0) closesocket(m_nfd);
	WSACleanup();
#endif
	return 0;
}

int  HttpListener::reply(const CHttpRequest &request, const int &code, const char *reason, const char *msg)
{
	struct evbuffer* evbuf = evbuffer_new();
	if (!evbuf)
	{
		LERROR("create evbuffer failed!");
		return - 1;
	}
	evhttp_request *req = static_cast<evhttp_request *>(request.request);
	evhttp_add_header(req->output_headers, "Content-Type", "application/json; charset=UTF-8");
	evbuffer_add_printf(evbuf, msg);
	evhttp_send_reply(req, code, reason, evbuf);
	evbuffer_free(evbuf);
	return 0;
}