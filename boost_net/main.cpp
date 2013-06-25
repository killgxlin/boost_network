#define _WIN32_WINNT _WIN32_WINNT_WIN7

#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <boost/asio.hpp>


#include <vector>
#include <list>
#include <map>
#include <functional>
#include <stdint.h>

namespace asio = boost::asio;
namespace lockfree = boost::lockfree;
namespace this_thread = boost::this_thread;
namespace posix_time = boost::posix_time;

typedef std::vector<uint8_t> msg_t;
typedef boost::shared_ptr<msg_t> pmsg_t;


struct client_t {
	typedef lockfree::spsc_queue<pmsg_t> msg_queue;

	asio::io_service &_ios;
	asio::io_service::strand _strand;
	asio::ip::tcp::socket _socket;

	msg_queue _send_queue;
	msg_queue _recv_queue;

	void send(pmsg_t msg_) {}
	pmsg_t recv() { return pmsg_t(); }

	void return_recved(pmsg_t msg_) {}
	pmsg_t take_sended() { return pmsg_t(); }

	void close();
};
typedef boost::shared_ptr<client_t> pclient_t;

struct network_t {
	struct auth_result_t {
		pclient_t client;
		bool result;
	};
	asio::io_service _ios;

	struct acceptor_t {
		asio::ip::tcp::acceptor acceptor;

	};
	std::map<uint32_t, acceptor_t*> _acceptor_vec;
	std::list<pclient_t> _client_authing_vec;
	std::vector<pclient_t> _client_authed_vec;

	lockfree::spsc_queue<auth_result_t> _auth_result_queue;
	void auth_result(pclient_t client_, bool result_){}

	network_t():_auth_result_queue(1024){}

	void active_send() {}
	typedef std::function<void(pclient_t)> auth_cb;
	uint32_t start_acceptor(const char* ip_, const uint16_t port_, auth_cb auth_cb_, auth_cb auth_ok_cb_) { return -1;  }
	void stop_acceptor(uint32_t acceptor_) {}

	void init() {}
	void destroy() {}
};


network_t gn;

struct authing_t {
	pclient_t client;
	int32_t try_num;
};
typedef boost::shared_ptr<authing_t> pauthing_t;
lockfree::spsc_queue<pauthing_t> g_need_auth_queue(1024);


boost::mutex lock_online;
std::list<pclient_t> online;


void test_server() {
	gn.init();
	uint32_t acceptor1 = gn.start_acceptor("127.0.0.1", 999, 
		[](pclient_t need_auth_){
			pauthing_t to_auth;
			to_auth->client = need_auth_;
			to_auth->try_num = 0;
			g_need_auth_queue.push(to_auth);
		}, 
		[](pclient_t auth_ok_){
			boost::lock_guard<boost::mutex> guard(lock_online);
			online.push_back(auth_ok_);
		}
	);

	while (true) {
		{
			pauthing_t to_auth;
			if (g_need_auth_queue.pop(to_auth)) {
				pmsg_t first = to_auth->client->recv();
				if (first) {
					to_auth->client->return_recved(first);
					gn.auth_result(to_auth->client, true);
				} else {
					to_auth->try_num++;
					if (to_auth->try_num > 10) {
						gn.auth_result(to_auth->client, false);
					} else if (!g_need_auth_queue.push(to_auth)) {
						gn.auth_result(to_auth->client, false);
					}
				}
			}
		}
		{
			boost::lock_guard<boost::mutex> guard(lock_online);
			for(auto itr = online.begin();
				itr != online.end();
				++itr) {

				pmsg_t recved = (*itr)->recv();
				if (recved) {
					(*itr)->return_recved(recved);
				}
			}
		}
		{
			boost::lock_guard<boost::mutex> guard(lock_online);
			for(auto itr = online.begin();
				itr != online.end();
				++itr) {

				pmsg_t sended = (*itr)->take_sended();
				if (sended) {
					(*itr)->send(sended);
				}
			}
		}

		gn.active_send();
	}

	gn.stop_acceptor(acceptor1);
	gn.destroy();
}
//------------------------------------------------------------------
struct server_t {
};
typedef boost::shared_ptr<server_t> pserver_t;

// 连接，发验证消息，几率断连接
struct robot_t {

	void init(const char* ip_, const uint16_t port_) {}
	void destroy() {}

	bool is_connected() { return false; }
	void try_connect() {}
	void disconnect() {}

};

void test_client() {
	robot_t bots[100];
	for (int i=0; i<100; ++i) {
		bots[i].init("127.0.0.1", 999);
	}

	while (true) {
		for (int i=0; i<100; ++i) {
			if (!bots[i].is_connected()) {
				bots[i].try_connect();
			} else {
				if (rand() % 6 == 0) 
					bots[i].disconnect();
			}
		}
		this_thread::sleep(posix_time::millisec(100));
	}

	for (int i=0; i<100; ++i) {
		bots[i].destroy();
	}
}

int main() {
	test_server();
	test_client();

}