#include "deps.h"
#include "w_server.h"

void client_t::do_start() {
	connected.store(true);

	if (timer.expires_from_now(posix_time::millisec(1000)))
		timer.async_wait(strand.wrap(boost::bind(&client_t::handle_auth_timeout, this, asio::placeholders::error)));
	do_recv();

	pacceptor->loghandler.auth(this);
}

void client_t::do_recv() {
	asio::async_read(socket, asio::buffer(&recv_msg_len, sizeof(recv_msg_len)), 
		strand.wrap(boost::bind(&client_t::handle_recv_head, this, asio::placeholders::error, asio::placeholders::bytes_transferred)));
}
	
void client_t::handle_recv_head(const boost::system::error_code& ec_, size_t recv_num_) {
	if (ec_) {
		do_disconnect();
		return;
	}

	recv_msg = boost::make_shared<msg_t>(sizeof(recv_msg_len) + recv_msg_len);
	asio::async_read(socket, asio::buffer(recv_msg->data()+sizeof(recv_msg_len), recv_msg_len), 
		strand.wrap(boost::bind(&client_t::handle_recv_body, this, asio::placeholders::error, asio::placeholders::bytes_transferred)));
}
	
void client_t::handle_recv_body(const boost::system::error_code& ec_, size_t recv_num_) {
	if (ec_) {
		do_disconnect();
		return;
	}

	if (!recv_queue.push(recv_msg)) {
		do_disconnect();
		return;
	} 

	do_recv();
}

void client_t::do_send() {
	static bool f = false;
	if (!sending.compare_exchange_strong(f, true))
		return;

	if (!send_queue.pop(send_msg)) 
		return;

	asio::async_write(socket, asio::buffer(*send_msg), 
		strand.wrap(boost::bind(&client_t::handle_send, this, asio::placeholders::error, asio::placeholders::bytes_transferred)));
}

void client_t::handle_send(const boost::system::error_code& ec_, size_t recv_num_) {
	if (ec_) {
		do_disconnect();
		return;
	}

	sending.store(false);

	do_send();
}

void client_t::do_auth_result(bool ok_) {
	if (!connected.load())
		return;

	timer.cancel();
	if (!ok_) {
		do_disconnect();
		return;
	} 

	pacceptor->loghandler.logon(this);
}

void client_t::handle_auth_timeout(const boost::system::error_code &ec_) {
	if (ec_) {
		return;
	}

	do_disconnect();
}

void client_t::do_disconnect() {
	static bool t = true;
	if (!connected.compare_exchange_strong(t, false))
		return;

	pacceptor->loghandler.logoff(this);

	socket.cancel();
	socket.shutdown(asio::ip::tcp::socket::shutdown_both);
	socket.close();

	auto network = pacceptor->pnetwork;
	do_reset();

	if (network->free.push(this)) {
		for(auto itr = network->acceptor.begin();
			itr != network->acceptor.end();
			++itr) {
		
			itr->second->accept();
		}
	}
}

void client_t::disconnect() {
	static bool t = true;
	if (!connected.compare_exchange_strong(t, false))
		return;

	strand.post(boost::bind(&client_t::do_disconnect, this));
}

void acceptor_t::do_accept() {
	static bool f = false;
	if (!accepting.compare_exchange_strong(f, true)) 
		return;

	client = NULL;
	if (pnetwork->free.pop(client))
		acceptor.async_accept(client->socket, boost::bind(&acceptor_t::handle_accept, this, asio::placeholders::error));
}

void acceptor_t::handle_accept(const boost::system::error_code &ec_) {
	if (!ec_) {
		client->pacceptor = this;
		client->do_start();
	} else {
		pnetwork->free.push(client);
	}
	accepting.store(false);

	if (asio::error::operation_aborted != ec_)
		do_accept();
}

// main
void acceptor_t::accept() {
	static bool f = false;
	if (!accepting.compare_exchange_strong(f, true)) 
		return;

	acceptor.get_io_service().post(boost::bind(&acceptor_t::do_accept, this));
}

bool network_t::init(size_t max_client_num_, size_t worker_num_) {
	client.resize(max_client_num_);
	for (int i=max_client_num_-1; i>=0; --i) {
		pclient_t created = new client_t(service);
		client[i] = created;
		free.push(created);
	}

	pwork = boost::make_shared<asio::io_service::work>(service);

	for (auto i=0; i<worker_num_; ++i) {
		worker.create_thread(boost::bind(&asio::io_service::run, &service));
	}

	return true;
}

void network_t::destroy() {

	pwork.reset();

	worker.join_all();
		
	for (int i=0; i<client.size(); ++i) {
		delete client[i];
	}
	client.clear();
}

void network_t::stop_acceptor(uint32_t acceptorid_) {
	auto itr = acceptor.find(acceptorid_);
	if (itr == acceptor.end())
		return;

	delete itr->second;
	acceptor.erase(itr);
}

uint32_t network_t::start_acceptor(
	const char* ip_,
	const uint16_t port_, 
	logcallback_t auth_,
	logcallback_t logon_,
	logcallback_t logoff_
	) {

	loghandler_t handler;
	handler.auth = auth_;
	handler.logon = logon_;
	handler.logoff = logoff_;

	auto id = next_acceptorid++;

	auto pacceptor = new acceptor_t(service, ip_, port_, handler, this);
	acceptor[id] = pacceptor;

	pacceptor->accept();

	return id;
}