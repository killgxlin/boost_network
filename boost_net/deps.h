#ifndef DEPS_H
#define DEPS_H

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
#include <stdio.h>

namespace asio = boost::asio;
namespace lockfree = boost::lockfree;
namespace this_thread = boost::this_thread;
namespace posix_time = boost::posix_time;

#endif