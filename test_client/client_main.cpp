#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/thread.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <boost/atomic.hpp>

#include <string>
#include <vector>

namespace asio = boost::asio;
namespace posix_time = boost::posix_time;
namespace lockfree = boost::lockfree;

typedef std::vector<uint8_t> msg_t;
typedef boost::shared_ptr<msg_t> pmsg_t;
typedef boost::shared_ptr<asio::io_service::work> pwork_t;
typedef lockfree::spsc_queue<pmsg_t> msg_queue_t;

struct client_t {
	asio::io_service _svc;
	asio::ip::tcp::socket _socket;
	asio::ip::tcp::endpoint _ep;
	boost::atomic<bool>  _connecting;
	boost::atomic<bool> _connected;
	boost::atomic<bool> _closing;
	boost::thread _worker;
	pwork_t _work;
	msg_queue_t _send_queue;
	msg_queue_t _recv_queue;

	client_t():_svc(), _socket(_svc), _send_queue(1024), _recv_queue(1024){}

	void init(const char* ip_, const uint16_t port_) {
		_connected = false;
		_connecting = false;
		_closing = false;

		_ep.address(asio::ip::address::from_string(ip_));
		_ep.port(port_);

		_work = pwork_t(new asio::io_service::work(_svc));
		_worker.swap(boost::move(boost::thread(boost::bind(&asio::io_service::run, &_svc))));
	}

	void destroy() {
		_work.reset();
		_worker.join();

		_send_queue.reset();
		_recv_queue.reset();
	}

	bool is_connected() {
		return !_closing && _connected;
	}

	void try_connect() {
		if (_connecting || _connected || _closing)
			return;

		_connected = false;
		_connecting = true;
		_closing = false;

		_socket.async_connect(_ep, [this](const boost::system::error_code &ec_){
			printf("connected\n");
			_connecting = false;
			if (!ec_) {
				_connected = false;
			} else {
				_connected = true;
				_svc.post(boost::bind(&client_t::do_recv, this));
			}
		});
	}

	void disconnect() {
		_closing = true;
	}

	pmsg_t sended;
	void do_send() {
		if (sended)
			return;

		if (_send_queue.pop(sended)) {
			asio::async_write(_socket, asio::buffer(*sended), boost::bind(&client_t::handle_sended, this, asio::placeholders::error, asio::placeholders::bytes_transferred));
		} else if (_closing) {
			_socket.shutdown(asio::ip::tcp::socket::shutdown_both);
			_socket.close();

			_send_queue.reset();
			_recv_queue.reset();

			_closing = false;
		}
		
	}
	void handle_sended(const boost::system::error_code &ec_, std::size_t bytes_transferred) {
		if (ec_) {
			std::cout<<ec_.message()<<std::endl;
			return;
		}
		printf("sended\n");
		if (_send_queue.pop(sended)) {
			asio::async_write(_socket, asio::buffer(*sended), boost::bind(&client_t::handle_sended, this, asio::placeholders::error, asio::placeholders::bytes_transferred));
		}
	}

	void do_recv() {
		pmsg_t recved(new msg_t(4));
		asio::async_read(_socket, asio::buffer(*recved), boost::bind(&client_t::handle_recv_head, this, asio::placeholders::error, asio::placeholders::bytes_transferred, recved));
	}
	void handle_recv_head(const boost::system::error_code &ec_, std::size_t bytes_transferred_, pmsg_t msg_) {
		if (ec_) {
			std::cout<<ec_.message()<<std::endl;
			return;
		}
		uint32_t size = uint32_t(msg_->data());
		msg_->reserve(size);
		printf("recv head\n");
		asio::async_read(_socket, asio::buffer(*msg_), boost::bind(&client_t::handle_recv_body, this, asio::placeholders::error, asio::placeholders::bytes_transferred, msg_));
	}
	void handle_recv_body(const boost::system::error_code &ec_, std::size_t bytes_transferred_, pmsg_t msg_) {
		if (ec_) {
			std::cout<<ec_.message()<<std::endl;
			return;
		}
		printf("recv body\n");
		if (_recv_queue.push(msg_)) {
			do_recv();
		} else {
			disconnect();
		}
	}

	pmsg_t get_sended(uint32_t len_) { return pmsg_t(new msg_t(len_+4)); }

	bool send(pmsg_t msg_) {
		if (!is_connected())
			return false;

		if (!_send_queue.push(msg_)) 
			return false;

		if (!sended)
			_svc.post(boost::bind(&client_t::do_send, this));

		return true;
	}

	pmsg_t recv() {
		if (!is_connected())
			return pmsg_t(NULL);

		pmsg_t recved;
		_recv_queue.pop(recved);
		return recved;
	}
	void return_recved(pmsg_t msg_) {}
};

#include <stdio.h>

int thread_client() {
	client_t cli;
	cli.init("127.0.0.1", 999);

__start:

	do {
		cli.try_connect();
		boost::this_thread::sleep(posix_time::millisec(10));
		printf("connecting\n");
	} while (!cli.is_connected());
	
	{
		pmsg_t sended = cli.get_sended(3);
		for (int i=4; i<4+3; ++i)
			sended->data()[i] = i-4;
		*(uint32_t*)sended->data() = 3;
		cli.send(sended);
	}
	printf("first msg\n");

	while (cli.is_connected()) {
		pmsg_t recved = cli.recv();
		if (recved) {
			if (rand() % 2)
				cli.return_recved(recved);
		}

		if (rand() % 2 == 0) {
			size_t len = rand() % 100;
			pmsg_t sended = cli.get_sended(len);
			*(uint32_t*)sended->data() = len;
			for (int i=4; i<4+len; ++i)
				sended->data()[i] = i-4;
			cli.send(sended);
		}

		if (rand() % 100 == 0) {
			cli.disconnect();
			printf("disconnected\n");
		}

		boost::this_thread::sleep(posix_time::millisec(100));
	}

	goto __start;

	cli.destroy();

	return 0;
}

int main() {
	boost::thread_group client;
	for (int i=0; i<10; ++i)
		client.create_thread(thread_client);

	client.join_all();
}