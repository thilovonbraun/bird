/***********************************************************************
 fppool.cpp - Implements the ConnectionPool class.

 Copyright (c) 2013 Thilo von Braun

***********************************************************************/

#include "StdAfx.h"
#include "fbpool.h"

#include <algorithm>
#include <functional>

namespace IBPP {


// Functor to test whether a given ConnectionInfo object is "too old".
//

template <typename ConnInfoT>
class TooOld : std::unary_function<ConnInfoT, bool>
{
public:
	TooOld(unsigned int tmax) :
	min_age_(time(0) - tmax)
	{
	}

	bool operator()(const ConnInfoT& conn_info) const
	{
		return !conn_info.in_use && conn_info.last_used <= min_age_;
	}

private:
	time_t min_age_;
};



//// clear /////////////////////////////////////////////////////////////
// Destroy connections in the pool, either all of them (completely
// draining the pool) or just those not currently in use.  The public
// method shrink() is an alias for clear(false).

void
ConnectionPool::clear(bool all)
{
	boost::lock_guard<boost::mutex> lock(mutex_);	// ensure we're not interfered with

	PoolIt it = pool_.begin();
	while (it != pool_.end()) {
		if (all || !it->in_use) {
			remove(it++);
		}
		else {
			++it;
		}
	}
}


//// find_mru //////////////////////////////////////////////////////////
// Find most recently used available connection.  Uses operator< for
// ConnectionInfo to order pool with MRU connection last.
// Returns 0 if there are no connections in use.

Database ConnectionPool::find_mru()
{
	PoolIt mru = std::max_element(pool_.begin(), pool_.end());
	if (mru != pool_.end() && !mru->in_use) {
		mru->in_use = true;
		return mru->conn;
	}
	else {
		return nullptr;
	}
}


//// grab //////////////////////////////////////////////////////////////

Database ConnectionPool::grab()
{
	boost::lock_guard<boost::mutex> lock(mutex_);	// ensure we're not interfered with
	remove_old_connections();
	Database mru = find_mru();
	if (mru != nullptr) {
		return mru;
	}
	else {
		// No free connections, so create and return a new one.
		pool_.push_back(ConnectionInfo(create()));
		return pool_.back().conn;
	}
}


//// release ///////////////////////////////////////////////////////////

void ConnectionPool::release(const Database pc)
{
	boost::lock_guard<boost::mutex> lock(mutex_);	// ensure we're not interfered with

	for (PoolIt it = pool_.begin(); it != pool_.end(); ++it) {
		if (it->conn == pc) {
			it->in_use = false;
			it->last_used = time(0);
			break;
		}
	}
}


//// remove ////////////////////////////////////////////////////////////
// 2 versions:
//
// First takes a Connection pointer, finds it in the pool, and calls
// the second.  It's public, because Connection pointers are all
// outsiders see of the pool.
//
// Second takes an iterator into the pool, destroys the referenced
// connection and removes it from the pool.  This is only a utility
// function for use by other class internals.

void ConnectionPool::remove(const Database pc)
{
	boost::lock_guard<boost::mutex> lock(mutex_);	// ensure we're not interfered with

	for (PoolIt it = pool_.begin(); it != pool_.end(); ++it) {
		if (it->conn == pc) {
			remove(it);
			return;
		}
	}
}

void ConnectionPool::remove(const PoolIt& it)
{
	// Don't lock the mutex.  Only called from other functions that do grab it.
	destroy(it->conn);
	pool_.erase(it);
}


//// remove_old_connections ////////////////////////////////////////////
// Remove connections that were last used too long ago.

void ConnectionPool::remove_old_connections()
{
	TooOld<ConnectionInfo> too_old(max_idle_time());

	PoolIt it = pool_.begin();
	while ((it = std::find_if(it, pool_.end(), too_old)) != pool_.end()) {
		remove(it++);
	}
}


//// safe_grab /////////////////////////////////////////////////////////

Database ConnectionPool::safe_grab()
{
	Database pc;
	while ( ! (pc = grab())->Connected()) {
		remove(pc);
		pc = 0;
	}
	return pc;
}


} // end namespace IBPP

