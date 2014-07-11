#ifndef CONCURRENCY_CONTROL_POLICIES_HXX__
#define CONCURRENCY_CONTROL_POLICIES_HXX__

/** This file contains concurrency control policies to be used in the Cache object.
 *
 *  The options available are:
 *    - NoConcurrencyControl - Do nothing to prevent multiple access.
 *    - EntryLocking - Lock the cache at the entry level whenever possible.
 *
 *  Each concurrency control policy must define the following member functions:
 *    - EntryLockType LockEntry(Key const&k)
 *    - GlobalLockType LockAll()
 *
 *  Where EntryLockType is the type of mutex used to lock an entry (must satisfy the Mutex concept),
 *  GlobalLockType is the type of mutex used to lock the entire cache (must satisfy the Mutex
 *  concept as well as allowing for recursive locking).
 */

#include <map>
#include <mutex>
#include <thread>

namespace mrr {
namespace concurrency {
namespace policies {


/** A class satisfying the Mutex concept that performs no concurrency control.
 *
 *  This class is returned by the concurrency control policy NoConcurrencyControl whenever a lock
 *  is requested. This is the default argument for the Cache object since unless otherwise requested,
 *  the Cache is designed for single threaded use as to incur no overhead.
 */
class NoOpMutex
{
public:
	NoOpMutex() = default;
	NoOpMutex(NoOpMutex const&) = delete;
	NoOpMutex(NoOpMutex&&) = delete;

	NoOpMutex& operator =(NoOpMutex const&) = delete;
	NoOpMutex& operator =(NoOpMutex&&) = delete;

	~NoOpMutex() = default;

	/** No-op performed on a lock() operation. */
	void lock()
	{
	}

	/** No-op performed on an unlock() operatior. */
	void unlock()
	{
	}
};


/** A concurrency control policy class that performs no concurrency control.
 *
 *  By default, the Cache object is suited for single threaded use. As a result, no overhead
 *  costs should be associated with it. To accomplish this, we define a policy class that returns
 *  a no-op mutex when an entry or the entire cache is requested to be locked.
 *
 *  @tparam K
 *    The key type used to index into the cache.
 */
template <typename K>
class NoConcurrencyControl
{
public:
	using EntryLockType = NoOpMutex;
	using GlobalLockType = NoOpMutex;

	/** Return a NoOpMutex to be used by the Cache code when locking the entire cache. */
	GlobalLockType& LockAll()
	{
		return m_;
	}

	/** Return a NoOpMutex to be used by the Cache when locking a singular entry. */
	EntryLockType& LockEntry(K const& k)
	{
		return m_;
	}

private:
	NoOpMutex m_; ///< The mutex that is returned when requested by #LockAll() or #LockEntry().
};



/** Forward declaration of the fine grained locking class. Needed because the CollectionMutex
 *  requires a reference to the concurrency policy.
 */
template <typename K>
class EntryLocking;



/** A class satisfying the Mutex concept suited for locking the entire cache.
 *
 *  Since it will be used as a GlobalLockType, it must be a recursively lockable as
 *  specified by the Cache class.
 *
 *  @tparam K The key type used to index into the cache.
 */
template <typename K>
class CollectionMutex
{
	friend class EntryLocking<K>;

public:
	/** Default constructor, the mutex is unlocked by default. */
	CollectionMutex()
		: locked_(false)
	{
	}

	CollectionMutex(CollectionMutex const&) = delete;
	CollectionMutex(CollectionMutex&&) = delete;

	CollectionMutex& operator =(CollectionMutex const&) = delete;
	CollectionMutex& operator =(CollectionMutex&&) = delete;

	~CollectionMutex() = default;

	/** Lock the entire cache.
	 *
	 *  If the collection is already locked by the current thread, there is nothing to do. If
	 *  not, then lock the global mutex in the policy class. Once this lock is obtained, obtain
	 *  the locks for all the entries in the cache to ensure that no operations are being
	 *  performed.
	 */
	void lock()
	{
		if (locked_ && owner_thread_ == std::this_thread::get_id())
			return;

		locking_policy_class().global_mutex().lock();
		for (auto& entry_mutex : locking_policy_class().entry_mutexes())
			entry_mutex.lock();

		owner_thread_ = std::this_thread::get_id();
		locked_ = true;
	}

	/** Unlock the entire cache.
	 *
	 *  Unlocking all of the individual entries in the cache then unlock the global mutex.
	 */
	void unlock()
	{
		for (auto& entry_mutex : locking_policy_class().entry_mutexes())
			entry_mutex.unlock();

		locked_ = false;
		locking_policy_class().global_mutex().unlock();
	}

private:
	/** Construct the mutex with a reference to the policy class.
	 *
	 *  This is needed so that the mutexes in the policy class can be accessed.
	 */
	CollectionMutex(EntryLocking<K>& lpc)
		: locking_policy_class_(lpc)
	{
	}

	/** Getter for the locking policy class reference. */
	EntryLocking<K>& locking_policy_class()
	{
		return locking_policy_class_;
	}

	EntryLocking<K>& locking_policy_class_; ///< Reference to the policy class.
	bool locked_;                               ///< Whether the collection is already locked.
	std::thread::id owner_thread_;              ///< The thread that currency owns the lock.
};



/** A concurrency control policy class that performs locking on the entry level in the Cache.
 *
 *  This is another option available for the concurrency policy of the Cache class when it
 *  is going to be used in a multi-threaded environment. This policy allows for exclusive
 *  locking at the entry level in the cache, it does not distinguish between readers and
 *  writers.
 *
 *  If an entry level lock is requseted, it returs a reference to a std::mutex that is stored
 *  in a map within the policy class. If a global lock is requested, it returns a
 *  CollectionMutex which locks all of the entries in the Cache upon a call to lock().
 *
 *  @tparam K
 *    The key type used to index into the cache.
 */

template <typename K>
class EntryLocking
{
	friend class CollectionMutex<K>;

public:
	using EntryLockType = std::mutex;
	using GlobalLockType = CollectionMutex<K>;

	/** Default constructor, pass a reference of the policy to the collection mutex. */
	EntryLocking()
		: full_cache_mutex_(*this)
	{
	}

	/** Lock the entire cache. See
	 *
	 *  @see CollectionMutex<K>
	 */
	GlobalLockType& LockAll()
	{
		full_cache_mutex_.lock();
		return full_cache_mutex_;
	}


	/** Lock an individual entry in the cache.
	 *
	 *  @param k
	 *    The key to lock.
	 */
	EntryLockType& LockEntry(K const& k)
	{
		std::lock_guard<std::mutex> global_lk(global_mutex);
		std::mutex& m = entry_mutexes_[k];
		m.lock();
		return m;
	}

private:
	/** Getter to retrieve the global cache mutex object. */
	std::mutex& global_mutex()
	{
		return global_mutex_;
	}

	/** Getter to retrieve the map of mutexes for the entries. */
	std::map<K, std::mutex>& entry_mutexes()
	{
		return entry_mutexes_;
	}

private:
	std::mutex global_mutex_;               ///< Mutex to facilitate exclusive access to the cache.
	std::map<K, std::mutex> entry_mutexes_; ///< Mutexes for each of the entries in the cache.
	CollectionMutex<K> full_cache_mutex_;   ///< Mutex to control access to the entire cache.
};

} // namespace policies
} // namespace concurrency
} // namespace mrr

#endif // #ifndef CONCURRENCY_CONTROL_POLICIES_HXX__
