#include "portable_socket.h"

#include <atomic>
#include <thread>
#include <mutex>
#include <list>
#include <memory>
#include <vector>
#include <condition_variable>
#include <string>
#include <chrono>

#include <boost/thread.hpp>

typedef std::vector<uint8_t> msg_t;
typedef std::shared_ptr<msg_t> spmsg_t;

struct msg_queue_t {
	void push(spmsg_t &&msg_) {
		std::lock_guard<std::mutex> guard(lock);
		list.push_back(msg_);
		if (list.size() == 1)
			empty.notify_one();
	}

	bool pop(spmsg_t &msg_, bool wait_ = false) {
		std::unique_lock<std::mutex> ul(lock);
		if (list.empty() && !wait_)
			return false;
		while (list.empty()) {
			empty.wait(ul);
		}
		msg_ = list.front();
		list.pop_front();
		return true;
	}

	void clear() {
		std::lock_guard<std::mutex> guard(lock);
		list.clear();
	}

	std::list<spmsg_t> list;
	std::mutex lock;
	std::condition_variable empty;
};

struct transport_t {
	SOCKET sock;

	std::atomic_bool connecting;
	std::atomic_bool connected;
	
	msg_queue_t send_queue;
	msg_queue_t recv_queue;

	std::thread sender;
	std::thread recver;
	std::thread connector;

	void thread_connect() {
		connected.store(false);

		do {
			struct sockaddr_in addr;
			int      r;
			hostent* h;

			memset((void*)&addr, 0, sizeof(addr));
			addr.sin_addr.s_addr = inet_addr(ip.c_str());
			if(INADDR_NONE == addr.sin_addr.s_addr) {
				h = gethostbyname(ip.c_str());
				if(NULL == h) {
					perror("Could not get host by name");
					break;;
				}
			} else {
				h = gethostbyaddr((const char*)&addr.sin_addr, sizeof(struct sockaddr_in), AF_INET);
				if(NULL == h) {
					perror("Could not get host by address");
					break;;
				}
			}

			sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			if(INVALID_SOCKET == sock) {
				perror("Could not create socket");
				break;
			}

			BOOL bDontLinger = TRUE;
			setsockopt( sock, SOL_SOCKET, SO_DONTLINGER, ( const char* )&bDontLinger, sizeof( BOOL ) );

			addr.sin_family = AF_INET;
			addr.sin_addr   = *((in_addr*)*h->h_addr_list);
			addr.sin_port   = htons(port);

			printf("Connecting... ");
			r = connect(sock, (sockaddr*)&addr, sizeof(struct sockaddr));
			if(SOCKET_ERROR == r) {
				printf("Cannot connect to server%d\n", get_errno());
				break;
			}
			printf("connected.\n");

			connected.store(true);
			connecting.store(false);
			sender.swap(std::thread(std::bind(&transport_t::thread_send, this)));
			recver.swap(std::thread(std::bind(&transport_t::thread_recv, this)));
			return;
		} while (0);

		connecting.store(false);
	}
	void thread_send() {
		while (connected.load()) {
			spmsg_t msg;
			send_queue.pop(msg, true);
			if (!msg)
				continue;

			const char* buffer = (const char*)msg->data();
			uint32_t remain = msg->size();
			uint32_t len = 0;
			uint32_t pos = 0;
			do {
				len = ::send(this->sock, buffer+pos, remain, 0);
				if (len <= 0) {
					printf("send error %d\n", get_errno());
					return;
				}
				pos += len;
				remain -= len;
			} while (remain > 0);
		}
	}

	bool _do_recv(uint8_t* buffer_, uint32_t recv_) {
		uint32_t remain = recv_;
		uint32_t len = 0;
		uint32_t pos = 0;
		do {
			len = ::recv(this->sock, (char*)buffer_, remain, 0);
			if (len <= 0) {
				printf("recv error %d\n", get_errno());
				return false;
			}
			pos += len;
			remain -= len;
		} while (remain > 0);

		return true;
	}

	void thread_recv() {
		while (connected.load()) {
			spmsg_t msg = std::make_shared<msg_t>(sizeof(uint32_t));

			if (!_do_recv(msg->data(), sizeof(uint32_t)))
				return;
			
			uint32_t msg_len = *(uint32_t*)msg->data();
			msg->resize(msg_len + sizeof(uint32_t));
			*(uint32_t*)msg->data() = msg_len;
			
			if (!_do_recv(msg->data() + sizeof(uint32_t), msg_len))
				return;
			
			recv_queue.push(std::move(msg));
		}
	}

	std::string ip;
	uint16_t port;

	bool init(const std::string ip_, const uint16_t port_) {
		ip = ip_;
		port = port_;
		connected = false;
		connecting = false;

		return true;
	}
	void destroy() {
		if (is_connected())
			disconnect();
	}

	bool is_connected() {
		return connected.load();
	}
	void try_connect() {
		bool f = false;
		if (!connecting.compare_exchange_strong(f, true))
			return;
		
		connector.swap(std::thread(std::bind(&transport_t::thread_connect, this)));
		connector.detach();
		return;
	}
	bool is_connecting() {
		return connecting.load();
	}
	void disconnect() {
		connected.store(false);
		closesocket(sock);

		sender.join();
		recver.join();

		send_queue.clear();
		recv_queue.clear();
	}

	bool send(spmsg_t msg_) {
		send_queue.push(std::move(msg_));
		return true;
	}
	spmsg_t recv(bool wait_ = false) {
		spmsg_t msg;
		recv_queue.pop(msg, wait_);
		return msg;
	}
	spmsg_t alloc_msg(uint32_t size_) {
		return std::make_shared<msg_t>(size_ + sizeof(uint32_t));
	}
	void dealloc_msg(spmsg_t msg_) {
		msg_.reset();
	}
};

void fill_msg(spmsg_t msg_, uint32_t size_) {
	*(uint32_t*)msg_->data() = size_;
	for (uint32_t i=0; i<size_; ++i) {
		msg_->data()[sizeof(uint32_t) + i] = i;
	}
	
}

void client_thread() {
	transport_t trans;
	trans.init("localhost", 999);

	do {
		if (!trans.is_connecting()) {
			trans.try_connect();
		} else {
			std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		}
	} while (!trans.is_connected());
	{
		spmsg_t msg = trans.alloc_msg(3);
		fill_msg(msg, 3);
		trans.send(msg);
	}
	{
		spmsg_t msg = trans.alloc_msg(rand() % 100);
		fill_msg(msg, msg->size() - 4);
		trans.send(msg);
	}
	while (trans.is_connected()) {
		spmsg_t msg = trans.recv();
		if (msg) {
			trans.send(msg);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}

	trans.destroy();
}

int main() {
	boost::thread_group client;
	for (int i=0; i<1; ++i)
		client.create_thread(client_thread);

	client.join_all();
	
	return 0;
}