// Copyright (c) 2011-2012 Thilo von Braun
// Distributed under the EUPL v1.1 software license, see the accompanying
// file license.txt or http://www.osor.eu/eupl/european-union-public-licence-eupl-v.1.1


// class dbBTC: our Database class to store relevant bitcoin data from the complete block chain

// Database engine is mySQL, db = BiRD
// Database goal: to have a list of all transactions outputs that not have been redeemed yet
// Reason: our client needs to make a transaction from the bitcoinaddress stored on the smartcard

// Tables:
//		* BitcoinAddress :  save base58 and hash160 representation of active
//		* TxOutAvailable :  data of all our special transactions that are still open (= txout that not has been used in another txin)
//								* txType:	0 = unknown
//											1 = to bitcoinaddress
//											2 = to IP/generation
//											3 = our special 2 signature transaction
//		* Chain___		 :  store received blocks and tx's not yet processed
//		* pkCounters     :  first useable value for primary keys
//								ID=1:  BitcoinAddress
//								ID=2:  TxOut
//								ID=3:  Blockchain
//								ID=4:  block height considered safe (and up to this block transactions processed)

#include <mysql++.h>
#include <ssqls.h>
#include <Windows.h>
#include <stdlib.h>
#include <WS2TCPIP.H>
#include "strlcpy.h"

#include "serialize.h"
#include "key.h"
#include "bignum.h"
#include "base58.h"

// Structure to our BitcoinAddress Table records
sql_create_3(BitcoinAddress, 1, 3,
	mysqlpp::sql_int_unsigned, ID,
	mysqlpp::sql_char, base58,
	mysqlpp::sql_blob, hash160
	)

// Structure to our TxOutAvailable records
sql_create_8(TxOutAvailable, 1, 8,
	mysqlpp::sql_int_unsigned, ID,
	mysqlpp::sql_blob, hash,
	mysqlpp::sql_int_unsigned, txindex,
	mysqlpp::sql_smallint_unsigned, txtype,
	mysqlpp::sql_bigint, txamount,
	mysqlpp::sql_int_unsigned, smartbtcaddr,
	mysqlpp::sql_int_unsigned_null, storebtcaddr,
	mysqlpp::sql_int_unsigned, blockheight
	)
// Structures for temporary block data
sql_create_5(ChainBlocks, 1, 5,
	mysqlpp::sql_int_unsigned, ID,
	mysqlpp::sql_blob, hash,
	mysqlpp::sql_blob, prevhash,
	mysqlpp::sql_int, height,
	mysqlpp::sql_int, status
	)
sql_create_3(ChainTxs, 2, 3,
	mysqlpp::sql_int_unsigned, blockID,
	mysqlpp::sql_int_unsigned, txN,
	mysqlpp::sql_blob, txHash
	)
sql_create_5(ChainTxIns, 3, 5,
	mysqlpp::sql_int_unsigned, blockID,
	mysqlpp::sql_int_unsigned, txN,
	mysqlpp::sql_int_unsigned, txInN,
	mysqlpp::sql_blob, opHash,
	mysqlpp::sql_int, opN
	)
sql_create_8(ChainTxOuts, 3, 8,
	mysqlpp::sql_int_unsigned, blockID,
	mysqlpp::sql_int_unsigned, txN,
	mysqlpp::sql_int_unsigned, txOutN,
	mysqlpp::sql_bigint_unsigned, value,
	mysqlpp::sql_blob, smartID,
	mysqlpp::sql_blob, smartIDAdr,
	mysqlpp::sql_blob, storeID,
	mysqlpp::sql_smallint_unsigned, txType
	)
// Structure for pkCounters
sql_create_2(pkCounters, 1, 2,
	mysqlpp::sql_int_unsigned, ID,
	mysqlpp::sql_int_unsigned, pkCount
	)
// Structure for transactions to send
sql_create_3(TxToSend, 1, 3,
	mysqlpp::sql_int_unsigned, ID,
	mysqlpp::sql_blob, tx,
	mysqlpp::sql_int_unsigned, txStatus
	)
// Structures for unconfirmed transactions
sql_create_3(TxUnconfirmed, 1, 3,
	mysqlpp::sql_int_unsigned, ID,
	mysqlpp::sql_blob, hash,
	mysqlpp::sql_int, life
	)
sql_create_4(TxInUnconfirmed, 2, 4,
	mysqlpp::sql_int_unsigned, TxID,
	mysqlpp::sql_int, txN,
	mysqlpp::sql_blob, hash,
	mysqlpp::sql_int_unsigned, txindex
	)

class ConnectionPoolBTC : public mysqlpp::ConnectionPool
{
public:
	// Constructor of the pool of connections to our BTC database
	ConnectionPoolBTC(const char *db, const char *server, const char *user, const char *password) :
	  db_ (db ? db : ""),
	  server_ (server ? server : ""),
	  user_ (user ? user : ""),
	  password_ (password ? password : "")
	  {}
	~ConnectionPoolBTC()
	{
		clear();
	}

protected:	// superclass overrides
	mysqlpp::Connection* create()
    {
		return new mysqlpp::Connection(
			db_.empty() ? 0 : db_.c_str(),
			server_.empty() ? 0 : server_.c_str(),
			user_.empty() ? 0 : user_.c_str(),
			password_.empty() ? 0 : password_.c_str());
	}
	void destroy(mysqlpp::Connection* cp)
    {
        // Superclass can't know how we created the Connection, so
        // it delegates destruction to us, to be safe.
        delete cp;
    }
	unsigned int max_idle_time()
    {
        return 120;	// allow 2 minutes before closing idle connections
    }
private:
	std::string db_, server_, user_, password_;
};

class dbBTC
{
private:
	std::string dbserver;						// server to connect to
	ConnectionPoolBTC* cpBTC;					// pool of connections to our database

#if (REUSETABLES == 0)
	Concurrency::concurrent_queue<unsigned int> cqReuseableBTCID;	// store reuseable bitcoin address ID's
	Concurrency::concurrent_queue<unsigned int> cqReuseableTxOut;	// store reuseable TXOut ID's
#endif

	bool DeleteTable(mysqlpp::Query& q, const char *sTable, HWND hPBar, int iPos);

public:
	dbBTC(void);								// constructor
	~dbBTC(void);								// destructor
	// DATABASE CONNECTION HELPER FUNCTIONS
	void SetServer(const char* sServer);		// set server string to connect to
	// Connect: make a connection pool to the stored IP4 address
	//  Returns: true if pool initialisation succeeded, false if failed
	bool SetupConnectionPool(void);
	mysqlpp::Connection *GrabConnection(void);
	void ReleaseConnection(mysqlpp::Connection *c);
	void DropConnectionPool(void);
	// IsConnected: returns true if database connection pool is active
	bool IsConnected();

	// Dump and load of database images
	bool DumpDatabase(HWND hDlg, const char *szPath);
	bool LoadDatabase(HWND hDlg, const char *szPath, bool bLocal);

	// Find missing IDs for reuse
	bool FillIDs(const char *szTable, Concurrency::concurrent_queue<unsigned int> &cq);

	// GetBitcoinAddressID: get the ID of a Bitcoin Address
	//		Returns: ID = unique identification of bitcoin address in the database (= primary key of main table), 0 if not found
	//		INPUT:	hash or base58 of the Bitcoin Address, bAdd = true if bitcoin address should be added if not found
	unsigned int GetBitcoinAddressID(mysqlpp::Connection* my_conn, uint160 *uiHash, BOOL bAdd=false);		
	unsigned int GetBitcoinAddressID(mysqlpp::Connection* my_conn, const string &sBase58, BOOL bAdd=false);
	// AddBitcoinAddress: add the bitcoin address to the database
	//		Returns: ID of the bitcoin address in the database,
	//		INPUT: Hash and/or Base58 encoding (set NULL pointer if corresponding value not yet calculated)
	unsigned int AddBitcoinAddress(mysqlpp::Connection* my_conn, uint160 *uiHash, const string &sBase58);
	// DeleteBitcoinAddress: deletes the bitcoin address identified by ID from the database
	//		Use this if you are sure there are no more Tx for this address
	//		Returns: true if delete was succesfull
	bool DeleteBitcoinAddress(mysqlpp::Connection* my_conn, const unsigned int btcID);
	// DeleteBitcoinAddressCheck: deletes the bitcoin address identified by ID from the database after checking there are no tx's anymore
	bool DeleteBitcoinAddressCheck(mysqlpp::Connection* my_conn, const unsigned int btcID);
	// AddTxOut: adds an output transaction to our database
	//		Returns: the ID of the transaction in our database, 0 if unsuccesfull
	unsigned int AddTxOut(mysqlpp::Connection *my_conn, int iBlockHeight, ChainTxOuts &TxOutToAdd, mysqlpp::sql_blob& TxHash);
	// DeleteTxOut: remove a transaction from our database, at it is being used as an input in another transaction
	//		Returns: true on successful removal
	bool DeleteTxOut(mysqlpp::Connection *my_conn, mysqlpp::sql_blob& txHash, mysqlpp::sql_int& txIndex);
	// GetBestHeightKnown : returns the highest known block chain number
	int GetBestHeightKnown(mysqlpp::Connection *my_conn);
	// GetSafeHeight : returns the block height that has been fully processed (and considered safe)
	int GetSafeHeight(mysqlpp::Connection *my_conn);
	// CheckHeights : checks if any blocks in ChainBlocks with unknown height can be linked into current chain, returns true if some could be linked
	bool CheckHeights(mysqlpp::Connection *my_conn);
	// IsBlockHashKnown: true if hash is found in ChainBlocks table
	bool IsBlockHashKnown(mysqlpp::Connection *my_conn, uint256 *bHash);
	// IsTxHashKnown: true if hash is found in TxUnconfirmed table
	bool IsTxHashKnown(mysqlpp::Connection *my_conn, uint256 *bHash);
	// Retrieve the hash from ChainBlocks if we know the height, returns in ui uint256(0) if not found
    bool GetBlockHashFromHeight(mysqlpp::Connection *my_conn, int iHeight, uint256& ui);
	// Retrieve height from a block with known hash
	// Returns: -3  : hash not found
	//			-2  : hash found, but height of block is unknown
	//			-1  : special case of ui is all zero (if we ask with prevHash of first block)
	//			>=0 : hash found and its height is known
	int GetBlockHeightFromHash(mysqlpp::Connection *my_conn, mysqlpp::sql_blob &fromHash);
	// Delete all block chain data (in ChainBlocks, ChainTxIns and ChainTxOuts) of the indicated height
	void DeleteBlockDataOfHeight(mysqlpp::Connection *my_conn, int iHeight);
	// Add the block header to our ChainBlocks temporary block table, newBlock.ID gets updated
	bool AddBlockToChain(mysqlpp::Connection *my_conn, ChainBlocks &newBlock);

	// Inserts a new (incomplete) record into ChainTxs
	bool InsertChainTx(mysqlpp::Connection *my_conn, ChainTxs &newTx);
	// Updates transaction hash once we processed complete raw transaction from a message
	bool UpdateChainTx(mysqlpp::Connection *my_conn, ChainTxs &updatedTx);

	// Insert a new record into ChainTxIns
	bool InsertChainTxIn(mysqlpp::Connection *my_conn, ChainTxIns &newTxIn);
	// Insert a new record into ChainTxOuts
	bool InsertChainTxOut(mysqlpp::Connection *my_conn, ChainTxOuts &newTxIn);
	// Insert a new record into TxUnconfirmed
	bool InsertTxUnconfirmed(mysqlpp::Connection *my_conn, TxUnconfirmed &newTx);
	// Insert a new record into TxInUnconfirmed
	bool InsertTxInUnconfirmed(mysqlpp::Connection *my_conn, TxInUnconfirmed &newTx);
	// Set life to zero for the given unconfirmed transaction
	void TxUnconfirmedDies(mysqlpp::Connection *my_conn, mysqlpp::sql_blob& txhash);
	// Decrease life for all unconfirmed transactions & remove all with life<0
	void TxUnconfirmedAges(mysqlpp::Connection *my_conn);
	// Counts the number of currently unconfirmed transactions
	int NrTxUnconfirmed(mysqlpp::Connection *my_conn);
	// "Confirm" any blocks more then 10 levels deep in ChainBlocks:
	//		- delete TxOutAvailable transactions used as inputs;
	//		- add new output transactions into TxOutAvailable table;
	//		- set block status to 2 (and remove previous status=2 block(s));
	void ConfirmTempBlocks(mysqlpp::Connection *my_conn);

	// Send transactions in queue
	bool SendWaitingTxs(void);

};
