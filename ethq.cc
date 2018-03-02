#include <iostream>
#include <stdexcept>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <vector>
#include <map>
#include <algorithm>

#include <ncurses/ncurses.h>
#include <ncurses/cursesapp.h>
#include <ncurses/cursesp.h>

#include "ethtool++.h"
#include "util.h"

typedef union {
	uint64_t		counts[4];
	struct {
		uint64_t	tx_packets;
		uint64_t	rx_packets;
		uint64_t	tx_bytes;
		uint64_t	rx_bytes;
	} q;
} queuestats_t;

typedef std::vector<queuestats_t>		stats_list_t;

// index to queue table, offset to value within
typedef std::pair<size_t, uint8_t>		queue_entry_t;

// string entry number -> queue_entry_t
typedef std::map<size_t, queue_entry_t>		queue_map_t;

class EthQApp : public NCursesApplication {

private:
	Ethtool*		ethtool;
	queue_map_t		qmap;
	size_t			qcount;
	stats_list_t		prev;
	stats_list_t		delta;
	queuestats_t		total;

private:
	NCursesPanel*		panel;
	void			redraw();

private:
	std::vector<std::string> get_string_names();
	void			build_queue_map(const Ethtool::stringset_t& names);
	stats_list_t		get_stats();
	void			get_deltas();

public:
				EthQApp();
				~EthQApp();
	int			run();

};

void EthQApp::build_queue_map(const Ethtool::stringset_t& names)
{
	for (size_t i = 0, n = names.size(); i < n; ++i) {

		auto& name = names[i];

		// match "tx-2.tx_packets"
		auto tx = name.find("tx-") == 0;
		auto rx = name.find("rx-") == 0;
		if (!(rx || tx)) continue;

		auto dot = name.find('.', 3);
		if (dot == std::string::npos) continue;

		// everything between '-' and '.'
		size_t eon;
		auto tmp = name.substr(3, dot - 3);
		unsigned int queue = std::stoi(tmp, &eon);
		if (eon < tmp.length()) continue;

		// look for stuff after the underscore
		auto under = name.find('_', dot + 3);
		if (under == std::string::npos) continue;
		auto type = name.substr(under + 1);

		// calculate offset into the four entry structure
		uint8_t offset = 0;
		if (rx) {
			offset += 1;
		}
		if (type == "bytes") {
			offset += 2;
		}

		// and populate it
		qmap[i] = queue_entry_t { queue, offset };

		// count the number of queues
		if (queue >= qcount) {
			qcount = queue + 1;
		}
	}
}

stats_list_t EthQApp::get_stats()
{
	stats_list_t results(qcount);

	auto raw = ethtool->stats();

	for (const auto pair: qmap) {
		auto id = pair.first;
		auto& entry = pair.second;
		auto queue = entry.first;
		auto offset = entry.second;

		results[queue].counts[offset] = raw[id];
	}

	return results;
}

void EthQApp::get_deltas()
{
	stats_list_t stats = get_stats();

	for (size_t j = 0; j < 4; ++j) {
		total.counts[j] = 0;
	}

	for (size_t i = 0; i < qcount; ++i) {
		for (size_t j = 0; j < 4; ++j) {
			int64_t d = stats[i].counts[j] - prev[i].counts[j];
			delta[i].counts[j] = d;
			total.counts[j] += d;
		}
	}

	std::swap(stats, prev);
}

EthQApp::EthQApp() : NCursesApplication(), qcount(0)
{
	ethtool = new Ethtool("enp5s0f1");
	build_queue_map(ethtool->stringset(ETH_SS_STATS));

	if (qcount == 0) {
		throw std::runtime_error("No queues found");
	}

	delta.reserve(qcount);
}

EthQApp::~EthQApp()
{
	delete ethtool;
}

void EthQApp::redraw()
{
	static auto bar = "------------";
	const int col = 4;
	int row = 1;

	panel->move(row++, col);
	panel->printw("%5s %12s %12s %12s %12s", "Queue", "TX packets", "RX packets", "TX bytes", "RX bytes");

	panel->move(row++, col);
	panel->printw("%5s %12s %12s %12s %12s", "-----", bar, bar, bar, bar);

	for (size_t i = 0; i < qcount; ++i) {
		auto& q = delta[i].counts;
		panel->move(row++, col);
		panel->printw("%5ld %12ld %12ld %12ld %12ld", i, q[0], q[1], q[2], q[3]);
	}

	panel->move(row++, col);
	panel->printw("%5s %12s %12s %12s %12s", "-----", bar, bar, bar, bar);

	auto& q = total.counts;
	panel->move(row++, col);
	panel->printw("%5s %12ld %12ld %12ld %12ld", "Total", q[0], q[1], q[2], q[3]);

	panel->refresh();
}

int EthQApp::run()
{
	prev = get_stats();

	curs_set(0);
	panel = new NCursesPanel();

	timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	t.tv_nsec = 0;
	while (true) {
		t.tv_sec += 1;
		clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &t, nullptr);
		get_deltas();
		redraw();
	}
}

int main(int argc, char *argv[])
{
	setlocale(LC_ALL, "");
	int res;

	try {
		EthQApp app;
		app.handleArgs(argc, argv);
		res = app();
		endwin();
	} catch (const NCursesException *e) {
		endwin();
		std::cerr << e->message << std::endl;
		res = e->errorno;
	} catch (const NCursesException &e) {
		endwin();
		std::cerr << e.message << std::endl;
		res = e.errorno;
	} catch (const std::exception& e) {
		std::cerr << "error: " << e.what() << std::endl;
		res = EXIT_FAILURE;
	}

	return res;
}
