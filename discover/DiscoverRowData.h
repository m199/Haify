#pragma once

#include <string>
#include <vector>

// Parallel columns: display values, navigation URIs and navigation titles.
// Every mapped row has equally sized vectors in the destination tab's order.
// The receiver creates/owns BRows; these values own no views or transport state.
struct DiscoverRowData {
	std::vector<std::string> vals;
	std::vector<std::string> uris;
	std::vector<std::string> ttls;
	bool writable = true;
	bool owned = false;
};
