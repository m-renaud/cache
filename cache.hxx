#ifndef CACHE_HXX__
#define CACHE_HXX__

#include <logging_policies.hxx>
#include <serialization_policies.hxx>
#include <concurrency_control_policies.hxx>

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

#include <fstream>
#include <functional>
#include <map>

namespace mrr {


/** An abstraction of an in-memory cache.
 *
 *  This class is responsible for all caching of loaded from disk, removing objects from disk,
 *  updating objects in the cache, and saving any changes to disk when requested.
 *
 *  @tparam Key
 *    The unique value to lookup entries in the cache (see #key_type).
 *
 *  @tparam Value
 *    The type of object to be stored in the cache and on disk (see #mapped_type).
 *
 *  @tparam ConcurrencyPolicy
 *    The policy class to use when enforcing concurrency constraints. This type must provide
 *    the functions LockEntry(Key) and LockAll(), which each return a reference to a mutex that
 *    has already been locked. The mutex returned from LockAll() must be a recursive mutex.
 *
 *  @todo Add monitoring and statistics information.
 */
template <
	typename Key,
	typename Value,
	typename LoggingPolicy = mrr::logging::policies::StdErr,
	template <typename V> class SerializationPolicy = mrr::serialization::policies::OstreamOverload,
	template <typename K> class ConcurrencyPolicy = mrr::concurrency::policies::NoConcurrencyControl
>
class Cache
	: public LoggingPolicy,
	  public SerializationPolicy<Value>,
	  public ConcurrencyPolicy<Key>
{
public:
	/** The type used to lookup a value in the cache, usually a unique identifier for the object.
	 *
	 *  For a more thorough description of its usage, see #setFilenameFunction().
	 */
	using key_type = Key;

	/** The object to be stored in the cache.
	 *
	 *  Internally, this type is wrapped in a std::shared_ptr and is returned as such to allow for
	 *  `nullptr` to be returned, indicating that the object request is not in the cache or on disk.
	 */
	using mapped_type = Value;


	/** The type of mutex used to lock an individual entry in the cache.
	 *
	 *  This can be any class that satisfies the Mutex concept and is specified in the
	 *  concurrency policy class.
	 */
	using EntryLockType = typename ConcurrencyPolicy<Key>::EntryLockType;

	/** The type of mutex used to lock the entire cache.
	 *
	 *  This can be any class that satisfies the Mutex concept and is specified in the
	 *  concurrency policy class.
	 */
	using GlobalLockType = typename ConcurrencyPolicy<Key>::GlobalLockType;


	/** The type used for the in-memory cache of entries. */
	using cache_type = std::map<key_type, std::shared_ptr<mapped_type> >;

	/** The signature of the function to generate a filename. */
	using filename_function_type = std::function<std::string(key_type)>;


public:
	/** Default constructor for the cache, no operations performed */
	Cache() = default;

	/** Retrieve the object from the cache associated with the key **k**.
	 *
	 *  This will return the object from the in-memory cache if it exists there, and if not
	 *  it will attempt to load the object from disk (if it exists).
	 *
	 *  @param k
	 *    The key of the object to retrieve.
	 *
	 *  @retval std::shared_ptr if the object could be retrieved from memory or disk.
	 *  @retval nullptr if the object could not be retrieved.
	 *
	 *  @warning The lock on the data is released when this function exits. Be careful
	 *    with the result.
	 */
	std::shared_ptr<mapped_type> operator[](key_type const& k)
	{
		std::lock_guard<EntryLockType> key_lock(this->LockEntry(k), std::adopt_lock);

		auto iter = m_cache.find(k);

		if (iter != m_cache.end())
			return iter->second;
		else
			return m_cache[k] = loadFromDisk(k);
	}

	/** Sets the function that generates a unique filename given a #key_type for the cache.
	 *
	 *  In order to read an object from disk (#loadFromDisk()) or write it to disk (#writeToDisk()),
	 *  the cache needs to be able to determine where the object should reside on disk. This is done
	 *  by the user providing a function that given a #key_type, produces a std::string holding the
	 *  absolute path of where that object should reside on disk.
	 *
	 *  A usual format is: /path/to/data/dir/<key>/data.format
	 *
	 *  @param fn_func
	 *    The function that is invoked to generate a filename.
	 */
	void setFilenameFunction(filename_function_type const& fn_func)
	{
		m_filename_func = fn_func;
	}


	/** Create a new instance of #mapped_type on the filesystem and refresh the cache.
	 *
	 *  When a new entry is created, it is not loaded into memory until it is requested
	 *  for another operation.
	 *
	 *  @param k
	 *    The key of the object to be inserted into the cache.
	 *  @param v
	 *    The new object to be created.
	 *
	 *  @retval true if the instance was successfully created.
	 *  @retval false if the instance failed to be created.
	 */
	bool create(key_type const& k, mapped_type const& v)
	{
		std::lock_guard<EntryLockType> key_lock(this->LockEntry(k), std::adopt_lock);

		std::string filename = m_filename_func(k);

		// If the object already exists, something has gone wrong somewhere. keys should be unique.
		if (fs::exists(filename))
		{
			this->LogError() << "Already exists: " << k << this->LogEndLine();

			return false;
		}

		// Create the directory for the object.
		fs::path directory = fs::path(filename).parent_path();
		bool directoryCreated = fs::create_directories(directory);

		if (! directoryCreated)
		{
			this->LogError() << "Directory " << directory << " cannot be created."
			                 << this->LogEndLine();
			return false;
		}

		// Write the object to disk.
		std::ofstream file_handle(filename);
		this->Serialize(file_handle, v, "xml");

		// Refresh the cache entry.
		refresh(k);

		this->LogInfo() << "Created at " << filename << this->LogEndLine();

		return true;
	}


	/** Remove an object from the cache as well as from permanent storage.
	 *
	 *  Unlike #erase(), this method removes the on-disk storage of the object.
	 *
	 *  @param k
	 *    The unique identifier of the object to remove.
	 *
	 *  @warning
	 *    This methods removes data from permanent storage.
	 *
	 *  @note
	 *    If data is accidentally removed, it *can* be recovered from the trash folder.
	 */
	bool remove(key_type const& k)
	{
		std::lock_guard<EntryLockType> key_lock(LockEntry(k), std::adopt_lock);

		std::string filename = m_filename_func(k);
		this->LogInfo() << "Deleting " << k << " at " << filename << this->LogEndLine();

		fs::path directory = fs::path(filename).parent_path();

		// Check to ensure that the object to be deleted exists in the filesystem.
		if (! fs::exists(directory))
		{
			this->LogInfo() << "Attempting to delete non-existent entry: " << directory
			                << this->LogEndLine();
			return false;
		}

		fs::path trash_directory = directory.parent_path()/"trash"/fs::path(filename).parent_path();

		// Create a trash directory if it doesn't already exist.
		if (! fs::exists(trash_directory.parent_path()))
			fs::create_directories(trash_directory.parent_path());

		this->LogInfo() << "Orig. Directory: " << directory << this->LogEndLine();
		this->LogInfo() << "Trash Directory: " << trash_directory << this->LogEndLine();

		// Move the object to the trash directory.
		fs::rename(directory, trash_directory);

		this->LogInfo() << "Rename successful" << this->LogEndLine();

		// Remove the object from the cache.
		erase(k);

		return true;
	}


	/** Call a member function to update the entry corresponding to key k in the cache.
	 *
	 *  This allows all access to the cache entries to be controlled by the cache itself,
	 *  providing proper concurrency control when specified.
	 *
	 *  @param k
	 *    The key to apply the update member function on.
	 *  @param ptr_mem
	 *    The member function to call on the cache entry.
	 *  @param args
	 *    The arguments to forward to the member function.
	 *
	 *  @tparam Args
	 *    The variadic argument pack that will get forwarded to the member function.
	 */
	template <typename... Args>
	bool callUpdateMemFn(key_type const& k, void (mapped_type::*ptr_mem)(Args...), Args&&... args)
	{
		std::lock_guard<EntryLockType> key_lock(LockEntry(k), std::adopt_lock);

		std::shared_ptr<mapped_type> ptr = (*this)[k];
		if (ptr == nullptr)
			return false;

		(ptr.get()->*ptr_mem)(std::forward<Args>(args)...);
		return true;
	}


	/** Call a getter function on a cache entry.
	 *
	 *  @param k
	 *    The key to call the getter function on.
	 *  @param ptr_mem
	 *    The getter member function to apply.
	 *
	 *  @tparam T
	 *    The type of the field being retrieved by the getter function.
	 *
	 *  @retval <true,T> if the object could be retrieved.
	 *  @retval <false,T{}> if the object could not be retrieved.
	 */
	template <typename T>
	std::pair<bool, T> callGetterMemFn(key_type const& k, T const& (mapped_type::*ptr_mem)() const)
	{
		std::lock_guard<EntryLockType> key_lock(LockEntry(k), std::adopt_lock);

		std::shared_ptr<mapped_type> ptr = (*this)[k];

		if (ptr == nullptr)
			return std::make_pair(false, T());
		else
			return std::make_pair(true, (ptr.get()->*ptr_mem)());
	}


	/** Refresh the cache entry from disk.
	 *
	 *  This is necessary to call when a new object is created because if at a previous time,
	 *  the object with key **k** was looked up, the cache would be populated will `nullptr`.
	 *
	 *  @param k
	 *    The key to refresh in the cache.
	 */
	void refresh(key_type const& k)
	{
		std::lock_guard<EntryLockType> key_lock(this->LockEntry(k), std::adopt_lock);
		auto iter = m_cache.find(k);

		if (iter != m_cache.end())
			m_cache[k] = loadFromDisk(k);
	}


	/** Save the enties of the cache to disk.
	 *
	 *  This is done by looping through the entries currently in the cache,
	 *  and if they are not `nullptr`, calling #writeToDisk() on the key and
	 *  value.
	 *
	 *  @note Any changes that have been made to the entries in the cache will become
	 *    permanent when this is called. Until version control is added, changes
	 *    cannot be undone.
	 */
	void save()
	{
		std::lock_guard<GlobalLockType> global_lock(this->LockAll(), std::adopt_lock);

		for (std::pair<const key_type, const std::shared_ptr<mapped_type>> const& p : m_cache)
		{
			if (p.second != nullptr)
			{
				this->LogTrace() << "Saving object " << p.first << this->LogEndLine();
				writeToDisk(p.first, p.second);
			}
		}
	}


	/** Clear the entire cache.
	 *
	 *  Remove all entries from the in-memory cache, writing any changes to disk first.
	 *
	 *  @note If you want to clear the cache without writing changes to disk, use #force_clear().
	 */
	void clear()
	{
		std::lock_guard<GlobalLockType> global_lock(this->LockAll(), std::adopt_lock);
		save();
		this->force_clear();
	}


	/** Remove all entries from the cache, don't save beforehand.
	 *
	 *  @warning Any changes that have been made in the cache **will not** be written to disk
	 *    and will be lost permanently.
	 */
	void forceClear()
	{
		m_cache.clear();
	}


	/** Erase an entry from the cache but do not remove it from disk.
	 *
	 *  @param k
	 *    The key to remove from the in-memory cache.
	 *
	 *  @note This method only removes the entry from the in-memory cache. If you wish to remove
	 *    the entry on disk as well, use #remove().
	 */
	void erase(Key const& k)
	{
		auto iter = m_cache.find(k);

		if (iter != m_cache.end())
			m_cache.erase(iter);
	}

private:

	/** Load on object from disk and put it in the cache.
	 *
	 *  @param k
	 *    The key of the object to load from disk.
	 *
	 *  @retval std::shared_ptr if the object associated with key **k** resides on disk.
	 *  @retval nullptr if the object cannot be loaded.
	 */
	std::shared_ptr<mapped_type> loadFromDisk(key_type const& k)
	{
		std::string filename = m_filename_func(k);
		this->LogDebug() << "  " << k << " not found in cache, loading from " << filename
		                 << this->LogEndLine();

		if (fs::exists(filename))
		{
			std::ifstream file_handle(filename);
			mapped_type v;
			this->Deserialize(file_handle, v, "xml");

			return std::make_shared<mapped_type>(v);
		}
		else
		{
			return std::shared_ptr<mapped_type>(nullptr);
		}
	}

	/** Write an object to disk.
	 *
	 *  @param k
	 *    The key of the object to write to disk.
	 *  @param v
	 *    The object to write to disk.
	 */
	void writeToDisk(key_type const& k, std::shared_ptr<mapped_type> const& v)
	{
		std::string filename = m_filename_func(k);
		this->LogDebug() << "  Saving " << k << " to " << filename << this->LogEndLine();

		std::ofstream file_handle(filename);
		this->Serialize(file_handle, *v, "xml");
	}

private:
	cache_type m_cache;                      ///< The in-memory cache.
	filename_function_type m_filename_func;  ///< The function to generate a filename from a key.
};

} // namespace mrr

#endif // #ifndef CACHE_HXX__
