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

