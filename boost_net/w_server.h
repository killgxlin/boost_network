
#ifndef W_SERVER_H
#define W_SERVER_H

// ----------------------------------------------------------
struct client_t;
typedef client_t* pclient_t;

// ----------------------------------------------------------
typedef std::function<void (pclient_t)> logcallback_t;
struct loghandler_t {
	logcallback_t auth;
	logcallback_t logon;
	logcallback_t logoff;
};

// ----------------------------------------------------------
struct network_t;
struct acceptor_t {
	asio::ip::tcp::acceptor acceptor;
	boost::atomic<bool> accepting;
	loghandler_t loghandler;
	network_t* pnetwork;
	pclient_t client;

	acceptor_t(asio::io_service &svc_, 
		const char* ip_, 
		const uint16_t port_, 
		const loghandler_t &loghandler_, 
		network_t* pnetwork_)
		:acceptor(svc_, asio::ip::tcp::endpoint(asio::ip::address::from_string(ip_), port_), true), accepting(false), loghandler(loghandler_), pnetwork(pnetwork_) {}

	// worker
	void do_accept();
	void real_accept();
	void handle_accept(const boost::system::error_code &ec_);

	// main
	void accept();
};
typedef acceptor_t* pacceptor_t;

// ----------------------------------------------------------
typedef std::vector<uint8_t> msg_t;
typedef boost::shared_ptr<msg_t> spmsg_t;
typedef lockfree::spsc_queue<spmsg_t> msg_queue_t;
// ----------------------------------------------------------
struct client_t {
	asio::io_service::strand strand;
	asio::ip::tcp::socket socket;
	pacceptor_t pacceptor;
	boost::atomic<bool> connected;

	client_t(asio::io_service &service_)
		:socket(service_), strand(service_), timer(service_), recv_queue(1024), send_queue(1024) 
	{
		do_reset();
	}

	void do_reset() {
		spmsg_t dummy;
		
		pacceptor = NULL;
		connected.store(false);

		while (!recv_queue.empty()) recv_queue.pop(dummy);
		recv_msg_len = 0;
		recv_msg.reset();
		
		while (!send_queue.empty()) send_queue.pop(dummy);
		send_msg.reset();
		sending.store(false);
		
		authorized.store(false);

		disconnecting.store(false);
	}

	void do_start(pacceptor_t pacceptor_);

	uint32_t recv_msg_len;
	spmsg_t recv_msg;
	msg_queue_t recv_queue;
	void do_recv();
	void handle_recv_head(const boost::system::error_code& ec, size_t recv_num_);
	void handle_recv_body(const boost::system::error_code& ec, size_t recv_num_);

	spmsg_t send_msg;
	msg_queue_t send_queue;
	boost::atomic<bool> sending;
	void send();
	void do_send();
	void real_send();
	void handle_send(const boost::system::error_code& ec, size_t recv_num_);

	asio::deadline_timer timer; // 如果没有auth_result就不会释放
	boost::atomic<bool> authorized;
	void auth_result(bool ok_) {
		if (!connected.load())
			return;

		strand.post(boost::bind(&client_t::do_auth_result, this, ok_));	
	}
	void do_auth_result(bool ok_);
	void handle_auth_timeout(const boost::system::error_code &ec_);

	boost::atomic<bool> disconnecting;
	void disconnect();
	void do_disconnect();
	void real_disconnect();
};

// ----------------------------------------------------------
struct network_t {
	std::vector<pclient_t> client;
	lockfree::stack<pclient_t> free;
	boost::thread_group worker;
	asio::io_service service;
	boost::shared_ptr<asio::io_service::work> pwork;
	
	std::map<uint32_t, pacceptor_t> acceptor;
	uint32_t next_acceptorid;

	network_t():next_acceptorid(0), free(10000){}

	// main
	void auth_result(pclient_t client_, bool ok_) {
		client_->auth_result(ok_);
	}

	void disconnect(pclient_t client_) {
		client_->disconnect();
	}

	// main
	spmsg_t recv(pclient_t client_) {
		if (!client_->connected.load())
			return spmsg_t(NULL);

		spmsg_t msg;
		client_->recv_queue.pop(msg);
		return msg;
	}

	bool send(pclient_t client_, spmsg_t msg_) {
		if (!client_->connected.load())
			return false;

		return client_->send_queue.push(msg_);
	}

	spmsg_t alloc_msg(pclient_t client_, size_t size_) {
		return boost::make_shared<msg_t>(sizeof(uint32_t) + size_);
	}

	void dealloc_msg(pclient_t client_, spmsg_t msg_) {
		msg_.reset();
	}

	void active_send() {
		for (auto i=0; i<client.size(); ++i) {
			client[i]->send();				
		}
	}

	// main
	void stop_acceptor(uint32_t acceptorid_);
	uint32_t start_acceptor(
		const char* ip_,
		const uint16_t port_, 
		logcallback_t auth_,
		logcallback_t logon_,
		logcallback_t logoff_
		);

	// main
	bool init(size_t max_client_num_, size_t worker_num_);
	void destroy();
};

#endif