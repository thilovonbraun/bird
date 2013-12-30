// Copyright (c) 2013 Thilo von Braun
// Distributed under the EUPL v1.1 software license, see the accompanying
// file license.txt or http://www.osor.eu/eupl/european-union-public-licence-eupl-v.1.1
//
// Version 0.1.0: initial release
//
// Definition of types similar to MySQL++ wrapper (mysqlpp)

#if !defined(FPSQLPP_H)
#define FPSQLPP_H
#define IBPP_WINDOWS

#include "ibpp.h"

namespace IBPP {
//
// Declare the closest C++ equivalent for each Firebird data type
//
	typedef __int16				sql_smallint;
	typedef unsigned __int16	sql_smallint_unsigned;
	typedef __int32				sql_int;
	typedef unsigned __int32	sql_int_unsigned;
	typedef __int64				sql_bigint;
	typedef unsigned __int64	sql_bigint_unsigned;
	typedef float				sql_float;		// 4 bytes
	typedef double				sql_double;		// 8 bytes
	typedef std::string			sql_char;
	typedef std::string			sql_binary;		// is char with charset OCTETS to hold binary data
	typedef std::string			sql_varchar;
	typedef std::string			sql_tinyblob;	// up to 255 chars
	typedef std::string			sql_blob;		// up to 65535 chars
	typedef Blob				sql_mediumblob; // up to 2^24 chars
	typedef Date				sql_date;
	typedef Time				sql_time;
	typedef Timestamp			sql_timestamp;

	typedef std::string		String;
}
#endif // FPSQLPP_H not defined