#include <cache.hxx>

#include <iostream>
#include <string>

using mrr::logging::policies::NoLogging;

int main()
{
	mrr::Cache<int, std::string> c;
	c.setFilenameFunction([](int i) {	return std::string("data/") + std::to_string(i) + "/value.txt";	});

	int idx;
	std::string val;

	c.insert(std::make_pair(1, "one"));

	std::cout << "Enter idx, value pairs to save in the cache:\n";
	while (std::cin >> idx && idx != -1)
	{
		std::cin >> val;
		c.insert(std::make_pair(idx, val));
	}

	std::cout << "Enter indexes to update and new value:\n";
	while (std::cin >> idx >> val)
	{
		auto iter = c.find(idx);
		if (iter != c.end())
		{
			std::cout << "Old value: " << *iter << std::endl;

			// This is not safe in a multi-threaded environment!!!
			*iter = val;
		}
		else
		{
			std::cout << "That index does not exist.\n";
		}
	}

	// Write any changes to disk. The only changes that are written immediately are create() and
	// remove() operations.
	c.save();
}
