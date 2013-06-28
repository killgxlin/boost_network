#include <boost/smart_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/shared_ptr.hpp>
#include <cassert>

class Y: public boost::enable_shared_from_this<Y>
{
public:

    boost::shared_ptr<Y> f()
    {
        return shared_from_this();
    }
};


int test_smartptr() {
    boost::shared_ptr<Y> p(new Y);
	// or boost::shared_ptr<Y> p = boost::make_shared<Y>();
    boost::shared_ptr<Y> q = p->f();
    assert(p == q);
    assert(!(p < q || q < p)); // p and q must share ownership

	return 0;
}