#ifndef __ethtool_pp_h
#define __ethtool_pp_h

#include <vector>
#include <string>

#include <net/if.h>
#include <linux/ethtool.h>

class Ethtool {

public:
	typedef std::vector<std::string> stringset_t;
	typedef std::vector<__u64> stats_t;

private:
	int			fd;
	ifreq			ifr;
	ssize_t			stats_size;

private:
	void			ioctl(void *data);

public:
				Ethtool(const std::string& ifname);
				~Ethtool();

public:
	size_t			stringset_size(ethtool_stringset ss);
	stringset_t		stringset(ethtool_stringset);
	stats_t			stats();

};

#endif // __ethtool_pp_h
