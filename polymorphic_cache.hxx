#ifndef POLYMORPHIC_CACHE_HXX__
#define POLYMORPHIC_CACHE_HXX__

#include <log4cxx/logger.h>

#include <boost/filesystem.hpp>

namespace fs = boost::filesystem;

#include <fstream>
#include <functional>
#include <map>

/** An abstraction of an in-memory cache, specialized for polymorphic objects..
 *
 *  This class is responsible for all caching of loaded from disk, removing objects from disk,
 *  updating objects in the cache, and saving any changes to disk when requested.
 *
 *  @tparam Key
 *    The unique value to lookup entries in the cache (see #key_type).
 *
 *  @tparam Value
 *    The base type of objects to be stored in the cache and on disk (see #mapped_base_type).
 *
 *  @todo Add monitoring and statistics information.
 */
template <typename Key, typename Value>
class PolymorphicCache
{
public:
	/** The type used to lookup a value in the cache, usually a unique identifier for the object.
	 *
	 *  For a more thorough description of its usage, see #setFilenameFunction().
	 */
	using key_type = Key;

	/** The base type of objects to be stored in the cache.
	 *
	 *  Internally, this type is wrapped in a std::shared_ptr and is returned as such to allow for
	 *  `nullptr` to be returned, indicating that the object request is not in the cache or on disk.
	 */
	using mapped_base_type = Value;


public:
	/** The type used for the in-memory cache of entries. */
	using cache_type = std::map<key_type, std::shared_ptr<mapped_base_type> >;

	/** The signature of the function to generate a filename. */
	using filename_function_type = std::function<std::string(key_type)>;


public:
	PolymorphicCache() = default;

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
	 */
	std::shared_ptr<mapped_base_type> operator[](key_type const& k)
	{
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
	 *  @param fn_func
	 *    The function that is invoked to generate a filename.
	 */
	void setFilenameFunction(filename_function_type const& fn_func)
	{
		m_filename_func = fn_func;
	}


	/** Create a new instance of #mapped_base_type on the filesystem and refresh the cache.
	 *
	 *  @param k
	 *    The key of the object to be inserted into the cache.
	 *  @param v
	 *    The new object to be created, passed as pointer to base.
	 *
	 *  @retval true if the instance was successfully created.
	 *  @retval false if the instance failed to be created.
	 */
	bool create(key_type const& k, std::shared_ptr<mapped_base_type> const& v)
	{
		std::string filename = m_filename_func(k);

		// If the survey already exists, something has gone wrong somewhere. UIDs should be unique.
		if (fs::exists(filename))
		{
			LOG4CXX_ERROR(logger, "Already exists: " << v->getUID());
			return false;
		}

		// Create the directory for the survey.
		fs::path directory = fs::path(filename).parent_path();
		bool directoryCreated = fs::create_directories(directory);

		if (! directoryCreated)
		{
			LOG4CXX_ERROR(logger, "Directory " << directory << " cannot be created.");
			return false;
		}

		// Write the survey to disk.
		std::ofstream file_handle(filename);
		cerealise(file_handle, v, "xml");

		// Refresh the cache entry.
		refresh(v->getUID());

		LOG4CXX_INFO(logger, "Created at " << filename);

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
		std::string filename = m_filename_func(k);
		LOG4CXX_INFO(logger, "Deleting " << k << " at " << filename);

		fs::path directory = fs::path(filename).parent_path();

		// Check to ensure that the survey to be deleted exists in the filesystem.
		if (! fs::exists(directory))
		{
			LOG4CXX_INFO(logger, "Attempting to delete non-existent entry: " << directory);
			return false;
		}

		fs::path trash_directory = directory.parent_path()/"trash"/fs::path(filename).parent_path();

		// Create a trash directory if it doesn't already exist.
		if (! fs::exists(trash_directory.parent_path()))
			fs::create_directories(trash_directory.parent_path());

		LOG4CXX_INFO(logger, "Orig. Directory: " << directory);
		LOG4CXX_INFO(logger, "Trash Directory: " << trash_directory);

		// Move the survey to the trash directory.
		fs::rename(directory, trash_directory);

		LOG4CXX_INFO(logger, "Rename successful");

		// Remove the survey from the cache.
		erase(k);

		return true;
	}


	/**
	 *
	 */
	template <typename TrueType, typename... Args>
	bool callUpdateMemFn(key_type const& k, void (TrueType::*ptr_mem)(Args...), Args&&... args)
	{
		TrueType* ptr = dynamic_cast<TrueType*>((*this)[k].get());
		if (ptr == nullptr)
			return false;

		(ptr->*ptr_mem)(std::forward<Args>(args)...);
		return true;
	}


	/**
	 *
	 */
	template <typename TrueType, typename T>
	std::pair<bool, T> callGetterMemFn(key_type const& k, T const& (TrueType::*ptr_mem)() const)
	{
		TrueType* ptr = dynamic_cast<TrueType*>((*this)[k].get());

		if (ptr == nullptr)
			return std::make_pair(false, T());
		else
			return std::make_pair(true, (ptr->*ptr_mem)());
	}

	/** Set the value of a field for a cache entry.
	 *
	 *  @tparam T
	 *    The type of the field to be set.
	 *  @tparam TrueType
	 *    The derived class that the field belongs to.
	 *
	 *  @param k
	 *    The key for the entry to update
	 *  @param ptr_mem
	 *    The pointer to member to update.
	 *  @param value
	 *    The new value of the field.
	 *
	 *  @retval true if the field was successfully updated.
	 *  @retval false if the field was unable to be updated.
	 */
	template <typename T, typename TrueType>
	bool set(key_type const& k, T TrueType::*ptr_mem, T const& value)
	{
		TrueType* ptr = dynamic_cast<TrueType*>((*this)[k].get());

		if (ptr == nullptr)
			return false;

		ptr->*ptr_mem = value;

		// Class* ptr_true_type = dynamic_cast<Class*>(ptr.get());

		// if (ptr == nullptr)
		//	return false;

		// ptr_true_type->*ptr_mem = value;

		return true;
	}


	/** Insert an item into a collection field of a cache entry.
	 *
	 *  @tparam C
	 *    The type of container to be modified.
	 *  @tparam T
	 *    The type of value to be inserted into the container.
	 *  @tparam TrueType
	 *    The derived class the field belongs to.
	 *
	 *  @param k
	 *    The key for the entry to update.
	 *  @param ptr_mem
	 *    The pointer to member to update.
	 *  @param value
	 *    The value to be added to the collection.
	 *
	 *  @retval true if the field was successfully updated.
	 *  @retval false if the field was unable to be updated.
	 */
	template <typename C, typename T, typename TrueType>
	bool collectionInsert(key_type const& k, C TrueType::*ptr_mem, T const& value)
	{
		TrueType* ptr = dynamic_cast<TrueType*>((*this)[k].get());

		if (ptr == nullptr)
			return false;

		(ptr->*ptr_mem).insert(value);
		return true;
	}


	/** Remove an item from collection field of a cache entry.
	 *
	 *  @tparam C
	 *    The type of container to be modified.
	 *  @tparam T
	 *    The type of value to be removed from the container.
	 *  @tparam TrueType
	 *    The derived class the field belongs to.
	 *
	 *  @param k
	 *    The key for the entry to update.
	 *  @param ptr_mem
	 *    The pointer to member to update.
	 *  @param value
	 *    The value to be removed from the collection.
	 *
	 *  @retval true if the field was successfully updated.
	 *  @retval false if the field was unable to be updated.
	 */
	template <typename C, typename T, typename TrueType>
	bool collectionRemove(key_type const& k, C TrueType::*ptr_mem, T const& value)
	{
		TrueType* ptr = dynamic_cast<TrueType*>((*this)[k].get());

		if (ptr == nullptr)
			return false;

		(ptr->*ptr_mem).erase(value);
		return true;
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
	void save() const
	{
		for (std::pair<const key_type, const std::shared_ptr<mapped_base_type>> const& p : m_cache)
		{
			if (p.second != nullptr)
			{
				LOG4CXX_TRACE(logger, "Saving survey " << p.first);
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
		save();
		force_clear();
	}


	/** Remove all entries from the cache, don't save beforehand.
	 *
	 *  @warning Any changes that have been made in the cache **will not** be written to disk
	 *    and will be lost permanently.
	 */
	void force_clear()
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
	std::shared_ptr<mapped_base_type> loadFromDisk(key_type const& k)
	{
		std::string filename = m_filename_func(k);
		LOG4CXX_DEBUG(logger, "  " << k << " not found in cache, loading from " << filename);

		if (fs::exists(filename))
		{
			std::ifstream file_handle(filename);
			std::shared_ptr<mapped_base_type> p;
			decerealise(file_handle, p, "xml");

			return p;
		}
		else
		{
			return std::shared_ptr<mapped_base_type>(nullptr);
		}
	}

	/** Write an object to disk.
	 *
	 *  @param k
	 *    The key of the object to write to disk.
	 *
	 *  @param v
	 *    The object to write to disk.
	 */
	void writeToDisk(key_type const& k, std::shared_ptr<mapped_base_type> const& v) const
	{
		std::string filename = m_filename_func(k);
		LOG4CXX_DEBUG(logger, "  Saving " << k << " to " << filename);

		std::ofstream file_handle(filename);
		cerealise(file_handle, v, "xml");
	}

private:
	cache_type m_cache;                      ///< The in-memory cache.
	filename_function_type m_filename_func;  ///< The function to generate a filename from a key.
	static log4cxx::LoggerPtr logger;        ///< The logger instance for the class.
};

#endif // #ifndef CACHE_HXX__
