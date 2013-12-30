// Copyright (c) 2011-2013 Thilo von Braun
// Distributed under the EUPL v1.1 software license, see the accompanying
// file license.txt or http://www.osor.eu/eupl/european-union-public-licence-eupl-v.1.1
//
// Version 0.2.0: added support for different database back-ends, mysql & firebird to start
//                instead of passing connections to helper methods, pass a ready to use 'query' (mysql) or 'statement' (firebird)

// class dbBTC: our Database class to store relevant bitcoin data from the complete block chain

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
//								ID=5:  "best block height" = highest block height from continuous chain, which is considered the 'right' one


// If database engine is mySQL, db = BiRD

#ifdef BIRDDB_MYSQL
	#include <mysql++.h>
	#include <ssqls.h>
#endif

// If database engine is Firebird, db = BiRD

#ifdef BIRDDB_FIREBIRD
	#define IBPP_WINDOWS
	#include <ibpp.h>
	#include "fbsqlpp.h"
	#include "fbpool.h"
#endif

#include <Windows.h>
#include <stdlib.h>
#include <WS2TCPIP.H>
#include "strlcpy.h"

#include "serialize.h"
#include "key.h"
#include "bignum.h"
#include "base58.h"

//
// Track status of database optimize settings
//
enum dbOptimizeStatus
{
	opt_unknown = 0,			// optimize settings not yet sent
	opt_download,				// database optimal for catching up with current blockchain
	opt_query					// database optimal for querying bitcoin balances
};

#ifdef BIRDDB_MYSQL
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
// Structures for efficient block confirming
sql_create_3(pkTxOutSpent, 1, 3,
    mysqlpp::sql_int_unsigned, txID,
	mysqlpp::sql_int_unsigned, smartID,
	mysqlpp::sql_int_unsigned_null, storeID)
sql_create_9(vTxOutAvailable, 1, 9,
    mysqlpp::sql_blob, hash,
	mysqlpp::sql_int_unsigned, txindex,
	mysqlpp::sql_smallint_unsigned, txtype,
	mysqlpp::sql_bigint_unsigned, txamount,
	mysqlpp::sql_blob, smhash,
	mysqlpp::sql_blob, smadr,
	mysqlpp::sql_int_unsigned_null, smid,
	mysqlpp::sql_blob, sthash,
	mysqlpp::sql_int_unsigned_null, stid
	)

// Structures for efficient MultiPruning
sql_create_4(MPOuts, 3, 4,
	mysqlpp::sql_int_unsigned, blockID,
	mysqlpp::sql_int_unsigned, txN,
	mysqlpp::sql_int_unsigned, txOutN,
	mysqlpp::sql_blob, txHash)
sql_create_5(MPIns, 2, 5,
	mysqlpp::sql_blob, opHash,
	mysqlpp::sql_int, opN,
	mysqlpp::sql_int_unsigned, blockID,
	mysqlpp::sql_int_unsigned, txN,
	mysqlpp::sql_int_unsigned, txInN)

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

#define BIRDDB_ConnectionPtr mysqlpp::Connection*
#define BIRDDB_Query         mysqlpp::Query
// exceptions in mysqlpp
#define BIRDDB_EXDB_Connect  mysqlpp::ConnectionFailed
#endif

#ifdef BIRDDB_FIREBIRD
class BitcoinAddress
	{
	public:	//data members
		BIRDDB::sql_int_unsigned ID;
		BIRDDB::sql_char base58;
		BIRDDB::sql_binary hash160;

	public: //code members
		BitcoinAddress(unsigned int a) { ID=a; base58=nullptr; hash160=nullptr; };
	};
class TxOutAvailable
	{
	public: //data members
		BIRDDB::sql_int_unsigned ID;
		BIRDDB::sql_binary hash;
		BIRDDB::sql_int_unsigned txindex;
		BIRDDB::sql_smallint_unsigned txtype;
		BIRDDB::sql_bigint txamount;
		BIRDDB::sql_int_unsigned smartbtcaddr;
		BIRDDB::sql_int_unsigned storebtcaddr;
		BIRDDB::sql_int_unsigned blockheight;

	public: //code members
		TxOutAvailable(unsigned int a) { ID=a; hash=nullptr; txindex=0; txtype=0; txamount=0; smartbtcaddr=0; storebtcaddr=0; blockheight=0; };
	};
class ChainBlocks
	{
	public:
		BIRDDB::sql_int_unsigned ID;
		BIRDDB::sql_binary hash;
		BIRDDB::sql_binary prevhash;
		BIRDDB::sql_int height;
		BIRDDB::sql_int status;
	};
class ChainTxs
	{
	public:
		BIRDDB::sql_int_unsigned blockID;
		BIRDDB::sql_int_unsigned txN;
		BIRDDB::sql_binary txHash;
	};
class ChainTxIns
	{
	public:
		BIRDDB::sql_int_unsigned blockID;
		BIRDDB::sql_int_unsigned txN;
		BIRDDB::sql_int_unsigned txInN;
		BIRDDB::sql_binary opHash;
		BIRDDB::sql_int opN;
	};
class ChainTxOuts
	{
	public:
		BIRDDB::sql_int_unsigned blockID;
		BIRDDB::sql_int_unsigned txN;
		BIRDDB::sql_int_unsigned txOutN;
		BIRDDB::sql_bigint_unsigned value;
		BIRDDB::sql_binary smartID;
		BIRDDB::sql_char smartIDAdr;
		BIRDDB::sql_binary storeID;
		BIRDDB::sql_smallint_unsigned txType;
	};
class pkCounters
	{
	public:
		BIRDDB::sql_int_unsigned ID;
		BIRDDB::sql_int_unsigned pkCount;
	};
class TxToSend
	{
	public:
		BIRDDB::sql_int_unsigned ID;
		BIRDDB::sql_binary tx;
		BIRDDB::sql_int_unsigned txStatus;
	};
class TxUnconfirmed
	{
	public:
		BIRDDB::sql_int_unsigned ID;
		BIRDDB::sql_binary hash;
		BIRDDB::sql_int life;
	};
class TxInUnconfirmed
	{
	public:
		BIRDDB::sql_int_unsigned TxID;
		BIRDDB::sql_int txN;
		BIRDDB::sql_binary hash;
		BIRDDB::sql_int_unsigned txindex;
	};

#define BIRDDB_ConnectionPtr BIRDDB::Database
#define BIRDDB_Query         BIRDDB::Statement
// exceptions in ibpp
#define BIRDDB_EXDB_Connect  BIRDDB::SQLException&
#endif  // BIRDDB_FIREBIRD

class ConnectionPoolBTC : public BIRDDB::ConnectionPool
{
public:
	// Constructor of the pool of connections to our BiRD database
	ConnectionPoolBTC(const std::string& db, const std::string& server, const std::string& user, const std::string& password) :
	  db_ (db),
	  server_ (server),
	  user_ (user),
	  password_ (password)
	  {}
	~ConnectionPoolBTC()
	{
		clear();
	}

protected:	// superclass overrides
	BIRDDB_ConnectionPtr create()
    {
#ifdef BIRDDB_FIREBIRD
		try {
			BIRDDB::Database db = BIRDDB::DatabaseFactory(server_, db_, user_, password_, "", "NONE", "");
			db->Connect();
			return db;
		}
		catch (IBPP::Exception& e) {
			return 0;
		}

#endif
#ifdef BIRDDB_MYSQL
		return new mysqlpp::Connection (
			db_.empty() ? 0 : db_.c_str(),
			server_.empty() ? 0 : server_.c_str(),
			user_.empty() ? 0 : user_.c_str(),
			password_.empty() ? 0 : password_.c_str());
#endif
	}
	void destroy(BIRDDB_ConnectionPtr cp)
    {
        // Superclass can't know how we created the Connection, so
        // it delegates destruction to us, to be safe.
#ifdef BIRDDB_MYSQL
		delete cp;
#endif
#ifdef BIRDDB_FIREBIRD
		cp->Disconnect();   // will further auto-destruct
#endif
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
	enum dbOptimizeStatus eOptimizeStatus;		// retain optimal status of database
	int block_depth;							// block depth which is considered safe
	// next int are RW in MultiPrune Thread, RO in others
	int block_multiprune_start;					// multiblock prune executing from this height
	int block_multiprune_end;					// multiblock prune performed up to this height
	// next int are RW in BlockConfirm Thread, RO in others
	int block_confirm_end;						// confirming blocks up to this height

#if (REUSETABLES == 0)
	Concurrency::concurrent_queue<unsigned int> cqReuseableBTCID;									// store reuseable bitcoin address ID's
	void CheckQueueBTCID(BIRDDB_Query& q, unsigned int uiMinSize, unsigned int uiMaxSize=1000);		// check if queue contains at least uiMinSize elements, if not add extra ones up to uiMaxSize
	Concurrency::concurrent_queue<unsigned int> cqReuseableTxOut;									// store reuseable TXOut ID's
	void CheckQueueTxOut(BIRDDB_Query& q, unsigned int uiMinSize, unsigned int uiMaxSize=1000);
#endif

	bool DeleteTable(BIRDDB_Query& q, const char *sTable, HWND hPBar, int iPos);

public:
	dbBTC(void);								// constructor
	~dbBTC(void);								// destructor
	boost::mutex dbmutex;						// mutex to prevent database deadlocks

	// DATABASE CONNECTION HELPER FUNCTIONS
	void SetServer(const char* sServer);		// set server string to connect to
	// Connect: make a connection pool to the stored IP4 address
	//  Returns: true if pool initialisation succeeded, false if failed
	bool SetupConnectionPool(void);
	BIRDDB_ConnectionPtr GrabConnection(void);
	void ReleaseConnection(BIRDDB_ConnectionPtr c);
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
	unsigned int GetBitcoinAddressID(BIRDDB_Query& q, uint160 *uiHash, BOOL bAdd=false);		
	unsigned int GetBitcoinAddressID(BIRDDB_Query& q, const string &sBase58, BOOL bAdd=false);
	// AddBitcoinAddress: add the bitcoin address to the database
	//		Returns: ID of the bitcoin address in the database,
	//		INPUT: Hash and/or Base58 encoding (set NULL pointer if corresponding value not yet calculated)
	unsigned int AddBitcoinAddress(BIRDDB_Query& q, uint160 *uiHash, const string &sBase58);
	//      INPUT: hash and/or Base58 encoding already in database format
	unsigned int AddBitcoinAddress(BIRDDB_Query& q, const BIRDDB::sql_blob& mHash, const BIRDDB::String& mBase58); 
	// DeleteBitcoinAddress: deletes the bitcoin address identified by ID from the database
	//		Use this if you are sure there are no more Tx for this address
	//		Returns: true if delete was succesfull
	bool DeleteBitcoinAddress(BIRDDB_Query& q, const unsigned int btcID);
	// DeleteBitcoinAddressCheck: deletes the bitcoin address identified by ID from the database after checking there are no tx's anymore
	bool DeleteBitcoinAddressCheck(BIRDDB_Query& q, const unsigned int btcID);
	// AddTxOut: adds an output transaction to our database
	//		Returns: the ID of the transaction in our database, 0 if unsuccesfull
	unsigned int AddTxOut(BIRDDB_Query& q, int iBlockHeight, ChainTxOuts &TxOutToAdd, BIRDDB::sql_blob& TxHash);
	// DeleteTxOut: remove a transaction from our database, at it is being used as an input in another transaction
	//		Returns: true on successful removal
	bool DeleteTxOut(BIRDDB_Query& q, BIRDDB::sql_blob& txHash, BIRDDB::sql_int& txIndex);
	// GetMaxHeightKnown : returns largest height found in blocks (due to BIP0034, this can be not part of contigious downloaded chain)
	int GetMaxHeightKnown(BIRDDB_Query& q);
	// GetBestHeight : returns the highest block in best downloaded contigious chain
	int GetBestHeight(BIRDDB_Query& q);
	// SetBestHeightKnown : memorizes the highest block height in best chain 
	bool SetBestHeight(BIRDDB_Query& q, int iBestHeight);
	// GetSafeHeight : returns the block height that has been fully processed (and considered safe)
	int GetSafeHeight(BIRDDB_Query& q);
	// GetBlockDepth : returns depth of blocks considered safe
	int GetBlockDepth(void);
	// GetBlockConfirmationEnd : returns block height up to where confirmations are currently made
	int GetBlockConfirmationEnd(void);
	void SetBlockConfirmationEnd(int ConfirmedHeight);
	// CheckHeights : checks if any blocks in ChainBlocks with unknown height can be linked into current chain, returns true if some could be linked
	bool CheckHeights(BIRDDB_Query& q);
	// IsBlockHashKnown: true if hash is found in ChainBlocks table
	bool IsBlockHashKnown(BIRDDB_Query& q, uint256 *bHash);
	// IsTxHashKnown: true if hash is found in TxUnconfirmed table
	bool IsTxHashKnown(BIRDDB_Query& q, uint256 *bHash);
	// Retrieve the hash from ChainBlocks if we know the height, returns in ui uint256(0) if not found
    bool GetBlockHashFromHeight(BIRDDB_Query& q, int iHeight, uint256& ui);
	// Retrieve height from a block with known hash
	// Returns: -3  : hash not found
	//			-2  : hash found, but height of block is unknown
	//			-1  : special case of ui is all zero (if we ask with prevHash of first block)
	//			>=0 : hash found and its height is known
	int GetBlockHeightFromHash(BIRDDB_Query& q, BIRDDB::sql_blob &fromHash);
	// Delete all block chain data (in ChainBlocks, ChainTxIns and ChainTxOuts) of the indicated height
	void DeleteBlockDataOfHeight(BIRDDB_Query& q, int iHeight);
	// Add the block header to our ChainBlocks temporary block table, newBlock.ID gets updated
	bool AddBlockToChain(BIRDDB_Query& q, ChainBlocks &newBlock);

	// Inserts a new (incomplete) record into ChainTxs
	bool InsertChainTx(BIRDDB_Query& q, ChainTxs &newTx);
	// Updates transaction hash once we processed complete raw transaction from a message
	bool UpdateChainTx(BIRDDB_Query& q, ChainTxs &updatedTx);

	// Insert a new record into ChainTxIns
	bool InsertChainTxIn(BIRDDB_Query& q, ChainTxIns &newTxIn);
	// Insert a new record into ChainTxOuts
	bool InsertChainTxOut(BIRDDB_Query& q, ChainTxOuts &newTxIn);
	// Insert a new record into TxUnconfirmed
	bool InsertTxUnconfirmed(BIRDDB_Query& q, TxUnconfirmed &newTx);
	// Insert a new record into TxInUnconfirmed
	bool InsertTxInUnconfirmed(BIRDDB_Query& q, TxInUnconfirmed &newTx);
	// Set life to zero for the given unconfirmed transaction
	void TxUnconfirmedDies(BIRDDB_Query& q, BIRDDB::sql_blob& txhash);
	// Decrease life for all unconfirmed transactions & remove all with life<0
	void TxUnconfirmedAges(BIRDDB_Query& q);
	// Counts the number of currently unconfirmed transactions
	int NrTxUnconfirmed(BIRDDB_Query& q);
	// Confirm any blocks that are 'deep enough' in ChainBlocks:
	//		- delete TxOutAvailable transactions used as inputs;
	//		- add new output transactions into TxOutAvailable table;
	//		- set block status to 99 (and remove previous status=99 block(s));
	//  Returns the height processed (= new safe height)
	int ConfirmTempBlocks(BIRDDB_ConnectionPtr my_conn, DWORD dwSleep);
	// Optimize database settings and index/keys for download/catching up
	bool OptimizeForDownload(BIRDDB_Query& q);
	// Optimize database settings and index/keys for querying
	bool OptimizeForQuerying(BIRDDB_Query& q);
	// Prepruning inside a single block, removing outputs (and corresponding input) that get spent in the same block
	bool PruneInsideBlock(BIRDDB_Query& q, BIRDDB::sql_int_unsigned blockID);
	// Prepruning a range of blocks, removing any ouputs (and the corresponding input) that get spent in the same block range
	bool PruneMultiBlock(BIRDDB_ConnectionPtr my_conn, int HeightStart, int HeightEnd);
	bool PruneMultiBlockParallel(int HeightStart, int HeightEnd);
	// Send transactions in queue
	bool SendWaitingTxs(void);

};

inline string Hash160ToAddress(const BIRDDB::sql_blob& mHash) {
	// add 1-byte version number to the front
	vector<unsigned char> vch(1, ADDRESSVERSION);
	vch.insert(vch.end(), mHash.begin(), mHash.end());
    return EncodeBase58Check(vch);
}