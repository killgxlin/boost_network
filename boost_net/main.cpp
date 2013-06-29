#include "deps.h"
#include "w_server.h"

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
	gn.init(5000, 4);
	uint32_t acceptor1 = gn.start_acceptor("192.168.1.113", 999, 
		[](pclient_t need_auth_){
			pauthing_t to_auth = boost::make_shared<authing_t>();
			to_auth->client = need_auth_;
			to_auth->try_num = 0;
			g_need_auth_queue.push(to_auth);
			printf("client %u need auth\n", need_auth_);
		}, 
		[](pclient_t logon_){
			boost::lock_guard<boost::mutex> guard(lock_online);
			online.insert(logon_);

			printf("client %u logon_\n", logon_);
		},
		[](pclient_t logoff_){
			boost::lock_guard<boost::mutex> guard(lock_online);
			online.erase(logoff_);
			printf("client %u logoff_\n", logoff_);
		}
	);

	while (true) {
		{
			pauthing_t to_auth;
			if (g_need_auth_queue.pop(to_auth)) {
				spmsg_t first = gn.recv(to_auth->client);
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

					gn.dealloc_msg(to_auth->client, first);
					gn.auth_result(to_auth->client, ok);
				} else {
					g_need_auth_queue.push(to_auth);
					// to_auth->try_num++;
					// if (to_auth->try_num > 100) {
					// 	gn.auth_result(to_auth->client, false);
					// } else if (!g_need_auth_queue.push(to_auth)) {
					// 	gn.auth_result(to_auth->client, false);
					// }
				}
			}
		}
		{
			boost::lock_guard<boost::mutex> guard(lock_online);
			for(auto itr = online.begin();
				itr != online.end();
				++itr) {

				spmsg_t recved = gn.recv(*itr);
				if (recved) {
					gn.send(*itr, recved);
				}
			}
		}
//		{
//			boost::lock_guard<boost::mutex> guard(lock_online);
//			for(auto itr = online.begin();
//				itr != online.end();
//				++itr) {
//
//				size_t len = rand() % 100;
//				spmsg_t sended = gn.alloc_msg(*itr, len);
//
//				if (sended) {
//					*(size_t*)sended->data() = len;
//					for (int i=0; i<len; ++i) {
//						sended->data()[i+4] = i;
//					}
//					gn.send(*itr, sended);
//				}
//			}
//		}

		gn.active_send();
		boost::this_thread::sleep(posix_time::millisec(50));
	}

	gn.stop_acceptor(acceptor1);
	gn.destroy();
}
//------------------------------------------------------------------
int main() {
	test_server();
}