#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int_distribution.hpp>
#include <iostream>

boost::random::mt19937 gen;

int roll_die() {
    boost::random::uniform_int_distribution<> dist(1, 6);
    return dist(gen);
}

int test_random() {
	for (int i=0; i<10; ++i) {
		std::cout<<roll_die()<<std::endl;
	}

	return 0;
}