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
		boost::thread thr(boost::bind(&asio::io_service::run, &_svc));
		thr.swap(boost::move(_worker));
	}
	void destroy() {
		_work.reset();
		_worker.join();
	}

	bool is_connected() {
		return _connected;
	}

	void try_connect() {
		if (_connecting || _connected || _closing)
			return;

		_connected = false;
		_connecting = true;
		_closing = false;

		_socket.async_connect(_ep, [this](const boost::system::error_code &ec_){
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

	bool sending;
	void do_send() {
		if (sending) 
			return;
		sending = true;

		pmsg_t sended;
		if (_send_queue.pop(sended)) {
			asio::async_write(_socket, asio::buffer(*sended), boost::bind(&client_t::handle_sended, this, asio::placeholders::error, asio::placeholders::bytes_transferred, sended));
		}
		
	}
	void handle_sended(const boost::system::error_code &ec_, std::size_t bytes_transferred, pmsg_t msg_) {
		pmsg_t sended;
		if (_send_queue.pop(sended)) {
			asio::async_write(_socket, asio::buffer(*sended), boost::bind(&client_t::handle_sended, this, asio::placeholders::error, asio::placeholders::bytes_transferred, sended));
		} else {
			sending = false;
		}
	}

	void do_recv() {
		pmsg_t recved(new msg_t(4));
		asio::async_read(_socket, asio::buffer(*recved), boost::bind(&client_t::handle_recv_head, this, asio::placeholders::error, asio::placeholders::bytes_transferred, recved));
	}
	void handle_recv_head(const boost::system::error_code &ec_, std::size_t bytes_transferred_, pmsg_t msg_) {
		if (!ec_) {
			std::cout<<ec_.message()<<std::endl;
			return;
		}
		uint32_t size = uint32_t(msg_->data());
		msg_->reserve(size);

		asio::async_read(_socket, asio::buffer(*msg_), boost::bind(&client_t::handle_recv_body, this, asio::placeholders::error, asio::placeholders::bytes_transferred, msg_));
	}
	void handle_recv_body(const boost::system::error_code &ec_, std::size_t bytes_transferred_, pmsg_t msg_) {
		if (!ec_) {
			std::cout<<ec_.message()<<std::endl;
			return;
		}
		if (_recv_queue.push(msg_))
			do_recv();
	}
	
	pmsg_t get_sended(uint32_t len_) { return pmsg_t(0); }

	bool send(pmsg_t msg_) {
		if (_closing)
			return false;

		if (!_send_queue.push(msg_)) 
			return false;

		_svc.post(boost::bind(&client_t::do_send, this));
		return true;
	}

	pmsg_t recv() {
		pmsg_t recved;
		_recv_queue.pop(recved);
		return recved;
	}
	void return_recved(pmsg_t msg_) {}
};

int main() {
	client_t cli;
	cli.init("127.0.0.1", 999);

	do {
		cli.try_connect();
		boost::this_thread::sleep(posix_time::millisec(10));
	} while (!cli.is_connected());

	pmsg_t sended = cli.get_sended(100);
	for (int i=0; i<100; ++i)
		sended->data()[i] = i;
	cli.send(sended);

	while (cli.is_connected()) {
		pmsg_t recved = cli.recv();
		if (recved) {
			if (rand() % 2)
				cli.return_recved(recved);
			else
				cli.send(recved);
		}
		if (rand() % 6 == 0)
			cli.disconnect();
	}

	cli.destroy();

	return 0;
}