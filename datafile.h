#ifndef __datafile_h
#define __datafile_h

#include <string>

#include "ring.h"
#include "query.h"

class Datafile {

private:
	Ring<Query>			queries;

public:
	void				read(const std::string& filename);
	const Query&		next();

};

#endif // __datafile_h
