#pragma once
#include <vector>
#include <event2/util.h>
#include <evhttp.h>
#include "sockets.h"


class HttpListener;
class LibeventWorker
{
public:
	explicit LibeventWorker(HttpServerListener* pListener, evutil_socket_t nfd);
	~LibeventWorker();

	int  start();
	int  stop();
	static void httpRequestHandle(struct evhttp_request* request, void* arg);
protected:
	evutil_socket_t			m_nfd;
	event_base			  * m_pBase = nullptr;
	evhttp				  * m_pHttp = nullptr;
	HttpServerListener    * m_pService = nullptr;
};


class HttpListener: public HttpServerInterface
{
public:
	explicit HttpListener(HttpServerListener* pListener, int num = 4);
	~HttpListener();

	int start(const char* pHost, int nPort = 8080) override;
	int stop() override;
	int reply(const CHttpRequest &request, const int &code, const char *reason, const char *msg) override;

protected:
	int bindSocket(const char* pHost, int nPort = 8080);

private:
	int								  m_nCount;				// 工作线程数
	evutil_socket_t					  m_nfd;
	HttpServerListener			    * m_pService;
	std::vector<LibeventWorker*>	  m_vecWorker;
};