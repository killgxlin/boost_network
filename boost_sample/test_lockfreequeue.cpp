#include <boost/lockfree/spsc_queue.hpp>
#include <boost/lockfree/queue.hpp>
#include <boost/smart_ptr.hpp>

struct client_t {
};

boost::lockfree::queue<client_t> queue(1024);

int test_lockfreequeue() {

	return 0;
}