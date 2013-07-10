#include "deps.h"
#include "w_server.h"

void client_t::do_start(pacceptor_t pacceptor_) {
	pacceptor = pacceptor_;
	connected.store(true);

	do_recv();

	if (timer.expires_from_now(posix_time::millisec(1000)))
		timer.async_wait(strand.wrap([this](const boost::system::error_code &ec_){
			if (ec_)
				return;

			do_disconnect();
		}));

	pacceptor->loghandler.auth(this);
}

void client_t::do_recv() {
	asio::async_read(socket, asio::buffer(&recv_msg_len, sizeof(recv_msg_len)),
		strand.wrap([this](const boost::system::error_code &ec_, size_t recv_num_){
			if (ec_) {
				do_disconnect();
				return;
			}

			recv_msg = boost::make_shared<msg_t>(sizeof(recv_msg_len) + recv_msg_len);
			*(uint32_t*)recv_msg->data() = recv_msg_len;
			
			asio::async_read(socket, asio::buffer(recv_msg->data()+sizeof(recv_msg_len), recv_msg_len), strand.wrap([this](const boost::system::error_code &ec_, size_t recv_num_){
				if (ec_) {
					std::cout<<ec_.message()<<std::endl;
					do_disconnect();
					return;
				}

				if (!recv_queue.push(recv_msg)) {
					do_disconnect();
					return;
				} 

				do_recv();	
			}));
		})
	);
}


void client_t::send() {
	if (!connected.load())
		return;

	if (send_queue.empty())
		return;

	bool f = false;
	if (!sending.compare_exchange_strong(f, true))
		return;

	strand.post([this](){real_send();});
}

void client_t::do_send() {
	bool f = false;
	if (!sending.compare_exchange_strong(f, true))
		return;

	real_send();
}

void client_t::real_send() {
	if (!send_queue.pop(send_msg)) {
		sending.store(false);
		return;
	}

	asio::async_write(socket, asio::buffer(*send_msg), 
		strand.wrap([this](const boost::system::error_code& ec_, size_t recv_num_){
			if (ec_) {
				sending.store(false);
				do_disconnect();
				return;
			}

			real_send();
		}
	));
}

void client_t::do_disconnect() {
	if (!connected.load())
		return;

	bool f = false;
	if (!disconnecting.compare_exchange_strong(f, true))
		return;

	real_disconnect();
}

void client_t::disconnect() {
	if (!connected.load())
		return;

	bool f = false;
	if (!disconnecting.compare_exchange_strong(f, true))
		return;

	strand.post([this](){real_disconnect();});
}

void client_t::real_disconnect() {
	if (authorized)
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
void client_t::auth_result(bool ok_) {
	if (!connected.load())
		return;

	strand.post([this, ok_](){
		if (!connected.load())
			return;

		authorized = ok_;

		timer.cancel();
		if (!ok_) {
			do_disconnect();
			return;
		}

		pacceptor->loghandler.logon(this);
	});	
}

// main
void acceptor_t::accept() {
	bool f = false;
	if (!accepting.compare_exchange_strong(f, true))
		return;

	acceptor.get_io_service().post([this](){real_accept();});
}

void acceptor_t::do_accept() {
	bool f = false;
	if (!accepting.compare_exchange_strong(f, true))
		return;

	real_accept();
}

void acceptor_t::real_accept() {
	client = NULL;
	if (pnetwork->free.pop(client))
		acceptor.async_accept(client->socket, [this](const boost::system::error_code &ec_) {
			if (!ec_) {
				client->do_start(this);
			} else {
				pnetwork->free.push(client);
			}

			if (asio::error::operation_aborted != ec_)
				real_accept();
			else
				accepting.store(false);
		});
	else
		accepting.store(false);
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
		worker.create_thread([this](){service.run();});
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