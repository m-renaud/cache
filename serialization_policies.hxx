#ifndef SERIALIZATION_POLICIES_HXX__
#define SERIALIZATION_POLICIES_HXX__

/** This file contains serialization policies to be used in the Cache object.
 *
 *  The options available are:
 *    - OstreamOverload - Call operator <<(std::ostream&, T const)
 *    - Cereal - Use the Cereal serialization library.
 *
 *  Each policy must define the following mumber functions:
 *    - void Serialize(std::ostream& os, T const& v, std::string const& format)
 *    - void Deserialize(std::istream& is, T& v, std::string const& format)
 */

#include <fstream>


namespace mrr {
namespace serialization {
namespace policies {


template <typename T>
class OstreamOverload
{
public:
	void Serialize(std::ofstream& os, T const& v, std::string const& format)
	{
		os << v;
	}

	void Deserialize(std::ifstream& is, T& v, std::string const& format)
	{
		is >> v;
	}
};


} // namespace policies
} // namespace serialization
} // namespace mrr


#endif // #ifndef SERIALIZATION_POLICIES_HXX__
