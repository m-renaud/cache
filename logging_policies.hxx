#ifndef LOGGING_POLICIES_HXX__
#define LOGGING_POLICIES_HXX__

/** This file contains logging policies to be used in the Cache object.
 *
 *  The options available are:
 *    - StdErr - Log all messages to std::cerr
 *
 *  Each policy must define the following mumber functions:
 *    - std::ostream& LogTrace()
 *    - std::ostream& LogDebug()
 *    - std::ostream& LogInfo()
 *    - std::ostream& LogWarn()
 *    - std::ostream& LogError()
 *    - EndLineType LogEndLine()
 */

#include <iostream>

namespace mrr {
namespace logging {
namespace policies {

using EndLineType = std::ostream& (*)(std::ostream&);

/** Logging policy class that writes all error messages to std::cerr. */
class StdErr
{
public:
	std::ostream& LogTrace()
	{
		return std::cerr;
	}

	std::ostream& LogDebug()
	{
		return std::cerr;
	}

	std::ostream& LogInfo()
	{
		return std::cerr;
	}

	std::ostream& LogWarn()
	{
		return std::cerr;
	}

	std::ostream& LogError()
	{
		return std::cerr;
	}

	EndLineType LogEndLine()
	{
		return std::endl;
	}
};



/** A custome streambuf used by the NoLogging policy class. Discards everything passed to it. */
class NullStreambuf : public std::streambuf
{
public:
	int overflow(int c)
	{
		return c;
	}
};


/** Custom end-line function that performs no operation used by the NoLogging policy. */
std::ostream& NoOpEndl(std::ostream&)
{
}


/** A logging policy class that performs no logging, discarding any input sent to it. */
class NoLogging
{
public:
	NoLogging()
		: null_stream_(&null_buf_)
	{
	}

	std::ostream& LogTrace()
	{
		return null_stream_;
	}

	std::ostream& LogDebug()
	{
		return null_stream_;
	}

	std::ostream& LogInfo()
	{
		return null_stream_;
	}

	std::ostream& LogWarn()
	{
		return null_stream_;
	}

	std::ostream& LogError()
	{
		return null_stream_;
	}

	EndLineType LogEndLine()
	{
		return NoOpEndl;
	}

private:
	NullStreambuf null_buf_;
	std::ostream null_stream_;

};

} // namespace policies
} // namespace serialization
} // namespace mrr


#endif // #ifndef LOGGING_POLICIES_HXX__
