#include <iostream>
#include <stdexcept>

#include "datafile.h"

int main(int argc, char *argv[])
{

	try {
		Datafile	df;
		df.read_txt("queryfile-example-current");
		df.write_raw("queryfile-example-current.raw");
	} catch (std::runtime_error& e) {
		std::cerr << "error: " << e.what() << std::endl;
	}
}
