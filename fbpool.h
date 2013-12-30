/***********************************************************************
 Copyright (c) 2013 Thilo von Braun
 
 Implementation of a Connection Pool for Firebird database
 Used together with IBPP C++ wrapper

 In fact, this is a pool of connected IBPP::Database objects
***********************************************************************/

#if !defined(IBPP_FPPOOL_H)
#define IBPP_FPPOOL_H
#define IBPP_WINDOWS

#include <assert.h>
#include <time.h>
#include <boost/thread.hpp>
#include "ibpp.h"

namespace IBPP {

// The pool's policy for connection reuse is to always return the 
// most recently used connection that's not being used right now.
// This ensures that excess connections don't hang around any longer
// than they must.  If the pool were to return the \em least recently
// used connection, it would be likely to result in a large pool of
// sparsely used connections because we'd keep resetting the last-used 
// time of whichever connection is least recently used at that moment.

class ConnectionPool
{
public:
	// Create empty pool
	ConnectionPool() { }

	// Destroy object
	//
	// If the pool raises an assertion on destruction, it means our
	// subclass isn't calling clear() in its dtor as it should.
	virtual ~ConnectionPool() { assert(empty()); }

	// Returns true if pool is empty
	bool empty() const { return pool_.empty(); }

	// Grab a free connection from the pool.
	// This method creates a new connection if an unused one doesn't
	// exist, and destroys any that have remained unused for too long.
	// If there is more than one free connection, we return the most
	// recently used one; this allows older connections to die off over
	// time when the caller's need for connections decreases.
	//
	// Do not delete the returned pointer.  This object manages the
	// lifetime of connection objects it creates.
	//
	// Returns a pointer to the connection
	virtual Database grab();

	// Return a connection to the pool
	//
	// Marks the connection as no longer in use.
	//
	// The pool updates the last-used time of a connection only on
	// release, on the assumption that it was used just prior.  There's
	// nothing forcing you to do it this way: your code is free to
	// delay releasing idle connections as long as it likes.  You
	// want to avoid this because it will make the pool perform poorly;
	// if it doesn't know approximately how long a connection has
	// really been idle, it can't make good judgements about when to
	// remove it from the pool.
	//
	// pc: pointer to a Connection object to be returned to the pool and marked as unused.
	virtual void release(const Database pc);

	// Removes the given connection from the pool
	//
	// If you mean to simply return a connection to the pool after
	// you're finished using it, call release() instead.  This method
	// is primarily for error handling: you somehow have figured out
	// that the connection is defective, so want it destroyed and
	// removed from the pool.
	//
	// pc: pointer to a Connection object to be removed from the pool and destroyed
	void remove(const Database pc);

	// Grab a free connection from the pool, testing that it's connected before returning it.
	// Returns a pointer to the connection
	virtual Database safe_grab();

	// Remove all unused connections from the pool
	void shrink() { clear(false); }

protected:
	// Drains the pool, freeing all allocated memory.
	//
	// A derived class must call this in its dtor to avoid leaking all
	// Connection objects still in existence.  We can't do it up at
	// this level because this class's dtor can't call our subclass's
	// destroy() method.
	//
	// all: if true, remove all connections, even those in use
	void clear(bool all = true);

	// Create a new connection
	//
	// Subclasses must override this.
	//
	// Essentially, this method lets your code tell ConnectionPool
	// what server to connect to, what login parameters to use, what
	// connection options to enable, etc.  ConnectionPool can't know
	// any of this without your help.
	//
	// Returns: a connected Connection object
	virtual Database create() = 0;

	// Destroy a connection
	//
	// Subclasses must override this.
	//
	// This is for destroying the objects returned by create().
	// Because we can't know what the derived class did to create the
	// connection we can't reliably know how to destroy it.
	virtual void destroy(Database) = 0;

	// The maximum number of seconds a connection is
	// able to remain idle before it is dropped.
	//
	// Subclasses must override this as it encodes a policy issue.
	//
	virtual unsigned int max_idle_time() = 0;

	// Returns the current size of the internal connection pool.
	size_t size() const { return pool_.size(); }

private:
	//// Internal types
	struct ConnectionInfo {
		Database conn;
		time_t last_used;
		bool in_use;

		ConnectionInfo(Database c) :
		conn(c),
		last_used(time(0)),
		in_use(true)
		{
		}

		// Strict weak ordering for ConnectionInfo objects.
		// 
		// This ordering defines all in-use connections to be "less
		// than" those not in use.  Within each group, connections
		// less recently touched are less than those more recent.
		bool operator<(const ConnectionInfo& rhs) const
		{
			const ConnectionInfo& lhs = *this;
			return lhs.in_use == rhs.in_use ?
					lhs.last_used < rhs.last_used :
					lhs.in_use;
		}
	};
	typedef std::list<ConnectionInfo> PoolT;
	typedef PoolT::iterator PoolIt;

	//// Internal support functions
	Database find_mru();
	void remove(const PoolIt& it);
	void remove_old_connections();

	//// Internal data
	PoolT pool_;
	boost::mutex mutex_;
};

} // end namespace IBPP

#endif // !defined(IBPP_FPPOOL_H)

