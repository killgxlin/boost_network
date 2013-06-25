#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <vector>
#include <iostream>

namespace asio = boost::asio;

int main() {
	boost::system::error_code ec;

	asio::ip::address addr;
	addr.from_string("0.0.0.0");


	asio::io_service svc;
	asio::ip::tcp::acceptor acceptor(svc, asio::ip::tcp::endpoint(addr, 999), ec);
	if (ec) {
		std::cout<<ec.message()<<std::endl;
		return 1;
	}

	asio::ip::tcp::socket peer(svc);


	acceptor.accept(peer);
	
	std::vector<char> recv(100);
	
	int len = peer.read_some(asio::buffer(recv));
	std::cout.write(recv.data(), len);
	boost::this_thread::sleep(boost::posix_time::seconds(100));

	return 0;
}