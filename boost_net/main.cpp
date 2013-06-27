#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/lockfree/stack.hpp>
#include <boost/asio.hpp>
#include <boost/atomic.hpp>

#include <set>
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
	pmsg_t take_sended(size_t len_) { return boost::make_shared<msg_t>(len_+4); }

	void start();
	client_t(asio::io_service &svc_):_send_queue(1024), _recv_queue(1024), _ios(svc_), _strand(svc_), _socket(svc_) {}
};

typedef client_t* pclient_t;

struct network_t {
	struct auth_result_t {
		pclient_t client;
		bool result;
	};
	asio::io_service _ios;

	typedef lockfree::queue<pclient_t> client_stack;
	typedef lockfree::stack<pclient_t> client_queue;
	struct acceptor_t {
		acceptor_t(asio::io_service &svc_, asio::ip::tcp::endpoint &ep_):acceptor(svc_, ep_){}
		asio::ip::tcp::acceptor acceptor;
		boost::atomic<bool> accepting;
	};
	typedef boost::shared_ptr<acceptor_t> pacceptor_t;
	std::map<uint32_t, pacceptor_t> _acceptor_map;
	std::list<pclient_t> _authing_list;
	boost::mutex _authing_list_lock;
	client_stack _free_stack;
	client_queue _destroy_list;
	std::vector<pclient_t> _client;

	lockfree::spsc_queue<auth_result_t> _auth_result_queue;
	void auth_result(pclient_t client_, bool result_){}



	network_t():_auth_result_queue(5000){}

	void disconnect_client(pclient_t client_) {
		client_->_socket.cancel();
		client_->_socket.shutdown(asio::ip::tcp::socket::shutdown_both);
		client_->_socket.close();
		_destroy_list.push(client_);
	}

	// nts
	typedef std::function<void(pclient_t)> auth_cb;
	uint32_t start_acceptor(const char* ip_, const uint16_t port_, auth_cb auth_cb_, auth_cb auth_ok_cb_, auth_cb off_line_cb_) { 
		pacceptor_t acceptor(new acceptor_t(
			_ios, asio::ip::tcp::endpoint(asio::ip::address::from_string(ip_), port_)));
		acceptor->accepting.store(false);
		_start_accept(acceptor);

		return -1;
	}
	void _start_accept(pacceptor_t acceptor_) {
		static bool f = false;
		if (!acceptor_->accepting.compare_exchange_weak(f, true))
			return;

		pclient_t client;
		if (!_free_stack.pop(client)) {
			acceptor_->accepting.store(false);
			return;
		}

		acceptor_->acceptor.async_accept(
			client->_socket, 
			boost::bind(
				&network_t::handle_accept, 
				this,
				asio::placeholders::error, 
				client, 
				acceptor_));
		
			
	}
	void handle_accept(const boost::system::error_code &ec_, pclient_t client_, pacceptor_t acceptor_){
		acceptor_->accepting.store(false);

		if (!ec_) {
			_free_stack.push(client_);
			return;
		}
		client_->start();
		_start_accept(acceptor_);
	}

	// nts
	void stop_acceptor(uint32_t acceptor_) {
		auto acceptor = _acceptor_map[acceptor_];
		acceptor->acceptor.cancel();
		_acceptor_map.erase(acceptor_);
	}

	void active_send() {
		
	}


	boost::thread _send_thread;
	boost::thread _destroy_thread;
	boost::thread_group _worker_thread;
	boost::shared_ptr<asio::io_service::work> _work;

	void init(size_t max_num_, size_t thread_num_) {
		_authing_list.clear();
		
		_client.resize(max_num_);
		for (int i=0; i<_client.size(); ++i) {
			auto client = pclient_t(new client_t(_ios));
			_client[i] = client;
			_free_stack.push(client);
		}

		for (auto itr = _client.rbegin();
			itr != _client.rend();
			++itr)
			_free_stack.push(*itr);

		_work.reset(new asio::io_service::work(_ios));

		for (int i=0; i<thread_num_; ++i) {
			_worker_thread.create_thread(boost::bind(&asio::io_service::run, &_ios));
		}
	}

	void destroy() {
		_work.reset();

		_worker_thread.join_all();

		while (!_client.empty()) {
			delete _client.back();
			_client.pop_back();
		}

		pclient_t dumy;
		while (!_free_stack.empty())	{
			_free_stack.pop(dumy);
		}
		while (!_destroy_list.empty()) {
			_destroy_list.pop(dumy);
		}
		//dumy.reset();

		_authing_list.clear();
	}
};


network_t gn;

struct authing_t {
	pclient_t client;
	int32_t try_num;
};
typedef boost::shared_ptr<authing_t> pauthing_t;
lockfree::spsc_queue<pauthing_t> g_need_auth_queue(1024);


boost::mutex lock_online;
std::set<pclient_t> online;


void test_server() {
	gn.init(100, 2);
	uint32_t acceptor1 = gn.start_acceptor("127.0.0.1", 999, 
		[](pclient_t need_auth_){
			pauthing_t to_auth;
			to_auth->client = need_auth_;
			to_auth->try_num = 0;
			g_need_auth_queue.push(to_auth);
		}, 
		[](pclient_t auth_ok_){
			boost::lock_guard<boost::mutex> guard(lock_online);
			online.insert(auth_ok_);
		},
		[](pclient_t disconnected_){
			boost::lock_guard<boost::mutex> guard(lock_online);
			online.erase(disconnected_);
		}
	);

	while (true) {
		{
			pauthing_t to_auth;
			if (g_need_auth_queue.pop(to_auth)) {
				pmsg_t first = to_auth->client->recv();
				if (first) {
					bool ok = true;
					if (*(uint32_t*)first->data() == 3) {
						for (int i=4; i<4+3; ++i) {
							if (first->data()[i] != i-4) {
								ok = false;
							}
						}
					} else {
						ok = false;
					}

					to_auth->client->return_recved(first);
					gn.auth_result(to_auth->client, ok);
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
					(*itr)->send(recved);
				}
			}
		}
		{
			boost::lock_guard<boost::mutex> guard(lock_online);
			for(auto itr = online.begin();
				itr != online.end();
				++itr) {

				size_t len = rand() % 100;
				pmsg_t sended = (*itr)->take_sended(len);
				if (sended) {
					*(size_t*)sended->data() = len;
					for (int i=0; i<len; ++i) {
						sended->data()[i+4] = i;
					}
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
int main() {
	test_server();
}