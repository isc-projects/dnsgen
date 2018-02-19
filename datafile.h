#ifndef __datafile_h
#define __datafile_h

#include <string>

#include "ring.h"
#include "query.h"

class Datafile {

private:
	Ring<Query>			queries;

public:
	void				read_txt(const std::string& filename);
	void				read_raw(const std::string& filename);
	void				write_raw(const std::string& filename);

public:

	const Query&		next() {
		return queries.next();
	};
};

#endif // __datafile_h
