# Overview

This repository provides a generic cache implementation, modeled after
the `std::map` interface, that can be used to store any object
persistently. Through the use of policies, the library can be
customized by specifying how logging is performed, how objects are
serialized, as well as how concurrent control to entries is handled.

Not only does the cache work for built-in types and user defined
types, but there is also a version, `PolymorphicCache`, that can handles
objects that belong to a polymorphic hierarchy. Currently, the
polymorphic cache has not yet been modified to use policy classes from
it's original implementation, but that should be coming soon.

In future releases, the cache classes will feature a caching
algorithm policy to control when and how objects in the cache will be
swapped out.

# Basic Usage

At first look, with all the policy template arguments defaulted, it
looks shockingly similar to a `std::map`, as was intended, although
there are some slight differences that will be noted later.

To create a basic cache object, just pass the key and value type as
template arguments. For example:

    mrr::Cache<int, std::string> c;

Next, set the function which generates the filename to store the
object, based on the key. This function is used whenever a new object
is created, loaded from disk, or removed from disk.

    c.setFilenameFunction(
      [](int i) {
        return std::string("data/") + std::to_string(i) + "/value.txt";
      }
    );

Now, to insert new values into the cache, use the `insert()` function
that you are familiar with, passing in a pair with the key and
value. This creates the appropriate storage location on disk and saves
the value automatically. How the object is serialized will be covered
in a later section, just for now, accept that since this is a built in
object that has operators << and >> overloaded, it works (but it's not
just limited to that, don't worry).

    c.insert(std::make_pair(1, "one"));

Next, you'll probably want to access something in the cache, either in
this invocation of the application, or another (just not at the same
time... yet). You can do this with the `find()` function, although the
return value is slightly different. Instead of getting a pair back,
you will receive an iterator to either the object you requested, or to
the end of the container. Since the key type is not stored, you just
have to dereference it to get the value, but make sure to compare
against the `end()` of the cache first.

    auto iter = c.find(1);
    if (iter != c.end())
      std::cout << *iter << std::endl;

You can modify the value as you would expect, but note that this
operation is not thread safe, even with concurrency control because
you are modifying the value outside the scope that the cache has
control over. There are other ways of doing this that will be outlined
below. 

    *iter = "uno";

And finally, save any changes made to the cache to disk:

    c.save();

And that's it, there's the basic usage!


# Advanced Usage

Now, I mentoined earlier that I would explain how the objects get
serialized to disk, and I mentoined the istream and ostream overloads
on the object, but I left out some important information.

## Policies

Much of the functionality of the cache class is implemented using
policy based design, which means that Cache merely provides a shell
that composes the functionality of the policies into a coherent
unit. You can read about policy based design on Wikipedia to get an
overview, and if you want to go in depth, pick up a copy of Andrei
Alexandrescu's book Modern C++ Design: Generic Programming and Design
Patterns Applied, it really is an excellent book, although a bit
outdated since the release of C++11.

Even though in the example, there are only 2 template arguments
visible, there are actually 5 template arguments to the Cache class,
the last 3 being the policy classes for serialization, logging, and
concurrency control. I did my best to put them in the order with which
people would likely need to manually specify them and gave them
reasonable defaults.

### The Serialization Policy

The default serialization policy that will work for built-in types and
simple user defined types is `IOStream` which lives in the
`mrr::serialization::policies` namespace. What this means is, as long
as your object can be correctly written and read using `operator <<`
and `operator >>` respectively, then this policy will work fine. In
most cases though, especially with complex objects, you're going to
want to use a serialization library. I will be adding an
implementation for Cereal soon and possibly one for
Boost Serialization.

The serialization policy class must have the following two member
functions:

    void Serialize(std::ofstream& os, T const& v, std::string const& format)
    void Deserialize(std::ifstream& is, T& v, std::string const& format)
	
If a class you write has these functions and they behave
appropriately, then you have written a serialization policy class for
the Cache.

Once you have your policy class, just put it as the 3rd template
argument when declaring the cache. For example, if we had the Cereal
policy class implemented, we could use it as follows:

    cache<int, std::string, mrr::serialization::policies::Cereal> c;

Pretty simple eh? :)


### The Logging Policy

This policy controls how and where log messages are displayed. This
policy defaults to `mrr::logging::policies::StdErr`, a policy that I
have defined that writes all logs, regardless of their log level, to
`std::cerr`, a reasonable default behaviour. There is also a
`NoLogging` option which discards all log messages if you don't
care. I will be implementing policies for some of the major logging
frameworks soon, and will be taking advantage of extended policy
functionality.

To change the logging policy, simply change the 4th template parameter
like so:

    cache<int, std::string, Cereal, mrr::logging::policies::NoLogging> c;

Note that you must specify all template arguments up to the one that
you want to modify, that's just how templates work, you can't specify
positional arguments.


### The Concurrency Control Policy

The last policy that is currently implemented is the concurrency
control policy. This policy controls how access to the cache by
multiple threads simultaneously is dealt with. The default that I went
with is no concurrency control, no overhead (well, incredibly little,
a no-op function call for each `lock()` and `unlock()` operation,
hopefully removed by the optimizer).

If you need concurrent access, you can use the `EntryLocking` policy,
which locks the cache at the entry level, so multiple threads can be
accessing and modifying entries of the cache simultaneously. Just add
it as the 5th and final (at this point) template parameter.

    cache<
      int, std::string,
      Cereal, NoLogging, mrr::concurrency::policies::EntryLocking
    > c;


## Typedefs Are Your Friend

Whenever using a class that is written using policy based design, it
is always best to make a typedef. One, it saves typing since the type
definition can sometimes get unweildy. Second, if you decide to change
one of the policies later, you only need to modify it in one spot. So,
for example, lets say you're caching Foos, you might make a typedef as
follows:

    using FooCache = mrr::Cache<
      int, Foo,
      mrr::serialization::policies::Cereal,
      mrr::logging::policies::Log4Cxx,
      mrr::concurrency::policies::EntryLocking
    >;


## Safely Using the Cache Concurrenty

I mentioned above, that getting the pointer to the object and then
directly manipulating it is not thread safe. This is because after
control has left the cache, it can no longer control concurrent
access. To aleviate this, there are several methods to use when
manipulating objects in the cache.

### Looking Up A Field

To lookup a field within a cached object, use the `callGetterMemFn()`
function. This takes the key of the object, and a pointer to the
member function to call, returning a `pair<bool, T>` where `T` is the
type of the field you are accessing. If the boolean is `true`, then
the object was successfully looked up and the second entry in the pair
will be the value retrieved by the getter.

On the other hand, if the object can not be retrieved (for example, if
it doesn't exist), then the boolean value will be false and the second
entry in the pair will be a default constructed `T`.

For example, if you had a class `Foo` that has a field `a_` of type
`chra ` with the getter function `a()`, then you can access the field
`a_` in object `n` as follows:

    std::pair<bool,char> result = c.callGetterMemFn(n, &Foo::a);

### Updating A Field and Calling Other Functions

For most other member functions that perform some operation on the
object and return `void`, there is the `callUpdateMemFn()` function,
which takes a key, a pointer to member function, and a variadic list
of arguments to forward to the member function.



# TODO

- Better docs for advanced usage
- Add docs for `PolymorphicCache`
- Implement a caching algorithm to keep cache size in check (LRU
  family likely)
