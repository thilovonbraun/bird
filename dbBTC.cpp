// Copyright (c) 2012-2013 Thilo von Braun
// Distributed under the EUPL v1.1 software license, see the accompanying
// file license.txt or http://www.osor.eu/eupl/european-union-public-licence-eupl-v.1.1

// Implementation of our database class for easy actual coding
// Release
//	0.2.0: update to include Firebird support
//  0.1.0: initial release (MySQL support only)

#include "StdAfx.h"
#include "bird.h"
#include "bird_TCP.h"
#include "dbBTC.h"
#include <CommCtrl.h>
#include <ppl.h>
#include <algorithm>

extern BTCtcp BTCnode;

dbBTC::dbBTC(void)
{	
	cpBTC = NULL;
	block_multiprune_start = -1;
	block_multiprune_end = -1;
	block_confirm_end = -1;
	eOptimizeStatus = opt_unknown;
}

dbBTC::~dbBTC(void)
{
	delete cpBTC;		// clean up connection pool
}

void dbBTC::SetServer(const char* sServer)
{
	dbserver.assign(sServer);
}

bool dbBTC::SetupConnectionPool()
{
	delete cpBTC;		// clean up previous pool
	eOptimizeStatus = opt_unknown;
	BOOST_LOG_TRIVIAL(info) << "Setting up connection pool";
	TCHAR sTLoad[MAX_LOADSTRING];	// temporary TCHAR load string
	char sDB[MAX_LOADSTRING*2];		// database name
	char sUSER[MAX_LOADSTRING*2];	// user name
	char sPW[MAX_LOADSTRING*2];		// password
#ifdef BIRDDB_MYSQL
	const TCHAR sHeader[]=L"mysql";
#endif
#ifdef BIRDDB_FIREBIRD
	const TCHAR sHeader[]=L"firebird";
#endif
	GetPrivateProfileString(sHeader, L"db", L"bird", sTLoad, MAX_LOADSTRING, szINIFile);
	WideCharToMultiByte(CP_ACP, 0, sTLoad, -1, sDB, MAX_LOADSTRING*2, NULL, NULL);
	GetPrivateProfileString(sHeader, L"user", L"bird", sTLoad, MAX_LOADSTRING, szINIFile);
	WideCharToMultiByte(CP_ACP, 0, sTLoad, -1, sUSER, MAX_LOADSTRING*2, NULL, NULL);
	GetPrivateProfileString(sHeader, L"password", L"btc", sTLoad, MAX_LOADSTRING, szINIFile);
	WideCharToMultiByte(CP_ACP, 0, sTLoad, -1, sPW, MAX_LOADSTRING*2, NULL, NULL);
	BOOST_LOG_TRIVIAL(debug) << "Setting up connection pool db=" << sDB << "; user=" << sUSER << "; password=" << sPW;
	cpBTC = new ConnectionPoolBTC(sDB, dbserver.c_str(), sUSER, sPW);  // new connection pool to our BiRD database
	block_depth = GetPrivateProfileInt(sHeader, L"block_depth", 10, szINIFile);
	if (block_depth<10)
		block_depth=10;
	bool bRet = true;

#if (REUSETABLES==0)
	cqReuseableBTCID.clear();
	cqReuseableTxOut.clear();
	bRet = FillIDs("BitcoinAddress", cqReuseableBTCID) && FillIDs("TxOutAvailable", cqReuseableTxOut);
#endif

	BOOST_LOG_TRIVIAL(info) << "Connection pool setup done (" << bRet << ")";
	BOOST_LOG_TRIVIAL(info) << "Safe block depth: " << block_depth;
	return bRet;
}

bool dbBTC::IsConnected()
{
	return (cpBTC != NULL);
}

void dbBTC::DropConnectionPool()
{
	delete cpBTC;
	cpBTC=NULL;
	eOptimizeStatus = opt_unknown;
	BOOST_LOG_TRIVIAL(info) << "Connection pool dropped";
}

BIRDDB_ConnectionPtr dbBTC::GrabConnection(void)
{
	BIRDDB_ConnectionPtr pc;
	if (cpBTC!=NULL) {
		try {
			pc = cpBTC->grab();		// try to grab a connection
		}
		catch (BIRDDB_EXDB_Connect cf) {
			TCHAR sTCaption[MAX_LOADSTRING];
			TCHAR sTError[MAX_LOADSTRING];
			LoadString(hInst, IDS_Err_Caption, sTCaption, MAX_LOADSTRING);
			MultiByteToWideChar(CP_ACP, NULL, cf.what(), -1, sTError, MAX_LOADSTRING);
			MessageBox(hWndMain, sTError, sTCaption, MB_OK|MB_ICONEXCLAMATION);
			BOOST_LOG_TRIVIAL(error) << "GrabConnection failed: " << cf.what();
			pc = NULL;										// illegal connection
		}
		return pc;
	}
	else
		return NULL;
}

void dbBTC::ReleaseConnection(BIRDDB_ConnectionPtr c)
{
	if (cpBTC!=NULL)
		cpBTC->release(c);
}

unsigned int dbBTC::GetBitcoinAddressID(BIRDDB_Query& q, uint160 *uiHash, BOOL bAdd)
{
	// get uint160 as an unformatted binary string
	std::string sHash;		
	sHash.assign(uiHash->begin(),uiHash->end());
	// make select query to retrieve ID
#ifdef BIRDDB_MYSQL
	q << "SELECT ID FROM BitcoinAddress WHERE hash160=" << mysqlpp::quote << sHash;
	mysqlpp::StoreQueryResult qres = q.store();
	if (qres.empty()) {								// unknown bitcoin address
		if (bAdd) 									// we should add it to the database
			return AddBitcoinAddress(q, uiHash, "");
		else
			return 0;								// not found, and we shouldn't add it -> return "not found"
	}
	else
		return qres[0][0];							// return found ID which is first row, first column of query
#endif
#ifdef BIRDDB_FIREBIRD
	q->Prepare("SELECT ID FROM BitcoinAddress WHERE hash160 = ?");
	q->Set(1, sHash);
	q->Execute();
	int qres=0;
	if (q->Fetch()) {    // bitcoin address exists, get it
		q->Get(1, &qres);
	}
	else {
		if (bAdd)
			return AddBitcoinAddress(q, uiHash, "");
	}
	return (unsigned int)qres;
#endif
	return 0;										// if we get here, something did not work as expected, return "not found"
}

unsigned int dbBTC::GetBitcoinAddressID(BIRDDB_Query& q, const string &sBase58, BOOL bAdd)
{
	q << "SELECT ID FROM BitcoinAddress WHERE base58=" << mysqlpp::quote << sBase58;
	mysqlpp::StoreQueryResult qres = q.store();
	if (qres.empty()) {								// unknown bitcoin address
		if (bAdd) 									// we should add it to the database
			return AddBitcoinAddress(q, NULL, sBase58);
		else
			return 0;								// not found, and we shouldn't add it -> return "not found"
	}
	else
		return qres[0][0];							// return found ID which is first row, first column of query
	return 0;										// if we get here, something did not work as expected, return "not found"
}

unsigned int dbBTC::AddBitcoinAddress(BIRDDB_Query& q, uint160 *uiHash, const string &sBase58)
{
	if (uiHash == NULL)
		return 0;			// pointer should be valid
	unsigned int ibtcAddr;

#if REUSETABLES
	q << "SELECT ID FROM pkReuseBitcoinAddress LIMIT 1";		// see if some IDs are reuseable
	mysqlpp::StoreQueryResult qres = q.store();
	if (qres.empty())
		ibtcAddr=0;
	else {		// get reuseable ID and delete it from table
		ibtcAddr = qres[0][0];
		q << "DELETE FROM pkReuseBitcoinAddress WHERE ID=" << ibtcAddr;	//make delete query
		q.execute();
	}
#else	// get reuseable from concurrent queue
	if (! cqReuseableBTCID.try_pop(ibtcAddr) ) 			// try to get a reuseable ID
		ibtcAddr = 0;
#endif

	if (ibtcAddr==0) {												// no reuseable IDs
		q << "UPDATE pkCounters SET pkCount=LAST_INSERT_ID(pkCount+1) WHERE ID=1";		// get next possible ID
		q.exec();
		ibtcAddr=(unsigned int)q.insert_id();					// get updated counter
	}
	if (ibtcAddr) {		// we found some ID, now go ahead and add the new row
		BitcoinAddress btcnew(ibtcAddr);			// create structure, fill already with ID
		std::vector<unsigned char> v;
		if (!uiHash) {	// don't have Hash value yet
			DecodeBase58Check(sBase58,v);		// decode the bitcoin address string
			btcnew.hash160.assign((const char *)&v[1], 20);	// first byte is version information, next 20 bytes are hash
		}
		else 
			btcnew.hash160.assign((const char *)uiHash->begin(), 20);
		if (sBase58.empty()) 	// don't have base58 string yet
			btcnew.base58 = Hash160ToAddress(*uiHash);
		q.insert(btcnew);						// make the insert SQL statement
		try {
			q.execute();
		}
		catch (const mysqlpp::BadQuery& er) {	// Handle any query errors
			ShowError(IDS_Err_BadQuery, er.what());
	    }
 
	}
	return ibtcAddr;
}

unsigned int dbBTC::AddBitcoinAddress(BIRDDB_Query& q, const BIRDDB::sql_blob& mHash, const BIRDDB::String& mBase58)
{
	if (mHash.is_null())
		return 0;			// hash should be present
	unsigned int ibtcAddr=0;

#if REUSETABLES
	q << "SELECT ID FROM pkReuseBitcoinAddress LIMIT 1";		// see if some IDs are reuseable
	mysqlpp::StoreQueryResult qres = q.store();
	if (qres.empty())
		ibtcAddr=0;
	else {		// get reuseable ID and delete it from table
		ibtcAddr = qres[0][0];
		q << "DELETE FROM pkReuseBitcoinAddress WHERE ID=" << ibtcAddr;	//make delete query
		q.execute();
	}
#else	// get reuseable from concurrent queue
	if (! cqReuseableBTCID.try_pop(ibtcAddr) ) 			// try to get a reuseable ID
		ibtcAddr = 0;
#endif

	if (ibtcAddr==0) {												// no reuseable IDs
		q << "UPDATE pkCounters SET pkCount=LAST_INSERT_ID(pkCount+1) WHERE ID=1";		// get next possible ID
		q.exec();
		ibtcAddr=(unsigned int)q.insert_id();					// get updated counter
	}
	if (ibtcAddr) {		// we found some ID, now go ahead and add the new row
		BitcoinAddress btcnew(ibtcAddr);				// create structure, fill already with ID
		if (mBase58.is_null()) 							// don't have base58 string yet
            btcnew.base58 = Hash160ToAddress(mHash);
		else
			btcnew.base58 = mBase58;
		btcnew.hash160 = mHash;
		q.insert(btcnew);						// make the insert SQL statement
		try {
			q.execute();
		}
		catch (const mysqlpp::BadQuery& er) {	// Handle any query errors
			if (er.errnum()==1062) {			// duplicate entry
				if (strstr(er.what(), "PRIMARY")) {		// duplicate entry is for primary key, indicates a problem !
					BOOST_LOG_TRIVIAL(error)<<"Duplicate primary key when adding bitcoin address " << btcnew.base58 << ": " << er.what();
				}
				else {
					BOOST_LOG_TRIVIAL(trace) << "Skipping adding a duplicate entry for " << btcnew.base58 << " in BitcoinAddress";
					cqReuseableBTCID.push(ibtcAddr);		// ID just allocated is reusable
				}
				q.reset();
				uint160 uihash;
				memcpy(&uihash, mHash.begin(), sizeof(uint160));
				ibtcAddr = GetBitcoinAddressID(q, &uihash, false);
			}
			else {
				BOOST_LOG_TRIVIAL(fatal)<<"Unexpected error when adding bitcoinaddress " << btcnew.base58 << ": " << er.what();
				ShowError(IDS_Err_BadQuery, er.what());
				ibtcAddr=0;
			}
	    }
	}
	return ibtcAddr;
}

bool dbBTC::DeleteBitcoinAddress(BIRDDB_Query& q, const unsigned int btcID)
{
	q << "DELETE FROM BitcoinAddress WHERE ID=" << btcID;
	BIRDDB::SimpleResult sr;
	try {
		sr = q.execute();
		if (sr.rows()>0) {		// effectively deleted
#if REUSETABLES
			q << "INSERT INTO pkReuseBitcoinAddress VALUES (" << btcID <<")";
			return q.exec();
#else
			cqReuseableBTCID.push(btcID);
			return true;
#endif
		}
	}
	catch(BIRDDB::BadQuery er) {
		BOOST_LOG_TRIVIAL(error)<<"Badquery when deleting bitcoinaddress ID " << btcID << ":" << er.what();
		q.reset();
	}
	return false;
}

bool dbBTC::DeleteBitcoinAddressCheck(BIRDDB_Query& q, const unsigned int btcID)
{
	// see if any transactions exist for this bitcoin ID
	q << "SELECT ID FROM TxOutAvailable WHERE smartbtcaddr=" << btcID << " OR storebtcaddr=" << btcID << " LIMIT 1";
	mysqlpp::StoreQueryResult qr;
	bool bRet = (qr = q.store());
	if (bRet) {	// query succeeded
		if (qr.num_rows()==0)						// no rows returned
			return DeleteBitcoinAddress(q, btcID);
		else
			return true;							// queries OK, but we can't delete ID as we still have tx's
	}
	return false;									// if we get here, something is wrong
}

unsigned int dbBTC::AddTxOut(BIRDDB_Query& q, int iBlockHeight, ChainTxOuts &TxOutToAdd, BIRDDB::sql_blob &TxHash)
{
	unsigned int iTxOut;			// our new transaction ID

#if REUSETABLES		
	q << "SELECT ID FROM pkReuseTxOut LIMIT 1";		// see if some IDs are reuseable
	mysqlpp::StoreQueryResult qres = q.store();
	if (qres.empty())
		iTxOut=0;
	else {		// get reuseable ID and delete it from table
		iTxOut = qres[0][0];
		q << "DELETE FROM pkReuseTxOut WHERE ID=" << iTxOut;	//make delete query
		q.execute();
	}
#else	// check vector to see if some IDs are reuseable
	if (! cqReuseableTxOut.try_pop(iTxOut))			// try to get a reuseable ID
		iTxOut = 0;
#endif

	if (iTxOut==0) {											// no reuseable IDs
		q << "UPDATE pkCounters SET pkCount=LAST_INSERT_ID(pkCount+1) WHERE ID=2";	// get next possible ID
		q.exec();
		iTxOut=(unsigned int)q.insert_id();				// get updated counter
	}
	if (iTxOut) {			// we got a transaction ID to use
		TxOutAvailable row(iTxOut);		// record to insert
		uint160 ui;
		row.hash=TxHash;
		row.txindex=TxOutToAdd.txOutN;
		row.txtype=TxOutToAdd.txType;
		row.txamount=TxOutToAdd.value;
		row.blockheight=iBlockHeight;
		memcpy(ui.begin(), TxOutToAdd.smartID.data(), 20);
		row.smartbtcaddr=GetBitcoinAddressID(q, &ui, true);
		memcpy(ui.begin(), TxOutToAdd.storeID.data(), 20);
		if (ui == 0)	// see if all zeroes was stored in database, if yes then store NULL now
			row.storebtcaddr = mysqlpp::null;
		else 
			row.storebtcaddr=GetBitcoinAddressID(q, &ui, true);
		q.insert(row);
		try {
		  q.execute();
		}
		catch (const mysqlpp::BadQuery& er) {	// Handle any query errors
			BOOST_LOG_TRIVIAL(error) << "Bad query in AddTxOut for block "<<iBlockHeight<<": "<< er.what();
			q.reset();
	    }
		catch (const mysqlpp::BadConversion& er) {  
			BOOST_LOG_TRIVIAL(error) << "Bad conversion in AddTxOut for block "<<iBlockHeight<<": "<< er.what();
			q.reset();
	    }
		catch (const mysqlpp::Exception& er) {  // Catch-all for any other MySQL++ exceptions
			BOOST_LOG_TRIVIAL(error) << "Error in AddTxOut for block "<<iBlockHeight<<": "<< er.what();
			q.reset();
		}
	}
	return iTxOut;
}

bool dbBTC::DeleteTxOut(BIRDDB_Query& q, BIRDDB::sql_blob& txHash, BIRDDB::sql_int& txIndex)
{
	unsigned int uiTxOut=0;
	unsigned int uiSmartBTCaddr=0;
	// get ID for the transaction to delete

	q  << "SELECT ID, smartbtcaddr FROM TxOutAvailable WHERE hash=" << mysqlpp::quote << txHash << " AND txindex=" << txIndex;
	mysqlpp::StoreQueryResult qres = q.store();
	if (!qres.empty()) {					// transaction found
		uiTxOut = qres [0][0];
		uiSmartBTCaddr = qres [0][1];
	}
	if (uiTxOut) {													// ID to delete found
		q << "DELETE FROM TxOutAvailable WHERE ID=" << uiTxOut;		// delete SQL statement
		q.exec();
#if REUSETABLES
		q << "INSERT INTO pkReuseTxOut VALUES (" << uiTxOut <<")";
		q.exec();
#else
		cqReuseableTxOut.push(uiTxOut);
#endif
		return DeleteBitcoinAddressCheck(q, uiSmartBTCaddr);
	}
	return false;
}

int dbBTC::GetMaxHeightKnown(BIRDDB_Query& q)
{
	q << "SELECT MAX(height) FROM ChainBlocks";
	mysqlpp::StoreQueryResult qres = q.store();
	if (!qres.empty()) {
		return qres[0][0];
	}
	return -1;
}

// SetBestHeight : sets the block height of the best block chain currently received
bool dbBTC::SetBestHeight(BIRDDB_Query& q, int iBestHeight)
{
	q << "UPDATE pkCounters SET pkCount=" << iBestHeight << " WHERE ID=5";
	bool bResult=false;
	try {
		bResult = q.exec();
	}
	catch (mysqlpp::BadQuery e) {
		BOOST_LOG_TRIVIAL(error) << "Unable to set Best Height to " << iBestHeight << ": " << e.what();
		q.reset();
	}
	return bResult;
}


// GetBestHeight : returns the block height of the best chain currently received
//    Due to BIP0034 we can receive blocks with a height known (due to BIP0034 info in it) NOT currently chaining to our best chain
//    GetBestHeight doe snot take into account those BIP0034 blocks.
//    We keep track of the best height in pkCounters Table.
int dbBTC::GetBestHeight(BIRDDB_Query& q)
{
	q << "SELECT ID, pkCount FROM pkCounters WHERE ID=5";		// ID=5 stores best block
	mysqlpp::StoreQueryResult qres = q.store();
	if (qres.empty()) {
		BOOST_LOG_TRIVIAL(error) << "GetBestHeight failed to retrieve ID=5 record from pkCounters";
		q.reset();
		return -2;		// it should never be empty
	}
	else {
		unsigned int i=qres[0][1];		// block height saved in pkCounters table
		return i;						// return pkCount value
	}
}

// GetSafeHeight : returns the block height that has been fully processed (and considered safe)
//    Returns: >0   block height as stored in pkCounters, not verified if block in ChainBlocks (it should be)
//			    0	block height is 0 as indicated in pkCounters: no blocks yet processed
//			   -2	no ID=4 found in pkCounters or no ChainBlocks table, database needs to be checked !
//			   -3   no active database connection
int dbBTC::GetSafeHeight(BIRDDB_Query& q)
{
	q << "SELECT ID, pkCount FROM pkCounters WHERE ID=4";		// ID=4 stores processed block
	mysqlpp::StoreQueryResult qres = q.store();
	if (qres.empty()) {
		BOOST_LOG_TRIVIAL(error) << "GetSafeHeight failed to retrieve ID=4 record from pkCounters";
		return -2;		// it should never be empty
	}
	else {
		unsigned int i=qres[0][1];		// block height saved in pkCounters table
		return i;						// return pkCount value, should be automatically in ChainBlocks if i>0
	}
}

bool dbBTC::IsBlockHashKnown(BIRDDB_Query& q, uint256 *bHash)
{
	std::string sHash;		
	sHash.assign(bHash->begin(),bHash->end());
	q << "SELECT ID FROM ChainBlocks WHERE hash=" << mysqlpp::quote << sHash;
	mysqlpp::StoreQueryResult qres = q.store();
	return (!qres.empty());
}

bool dbBTC::IsTxHashKnown(BIRDDB_Query& q, uint256 *bHash)
{
	mysqlpp::sql_blob sHash(reinterpret_cast<char *>(bHash->begin()),sizeof(uint256));
	q << "SELECT ID FROM TxUnconfirmed WHERE hash=" << mysqlpp::quote << sHash;
	mysqlpp::StoreQueryResult qres = q.store();
	return (!qres.empty());
}

// get the hash out of ChainBlocks for a certain block height
// currently only looks in temporary stored blocks in ChainBlocks table
// returns 0 if not found
bool dbBTC::GetBlockHashFromHeight(BIRDDB_Query& q, int iHeight, uint256& ui)
{
	q << "SELECT ID, hash, height FROM ChainBlocks WHERE height=" << iHeight;
	vector<ChainBlocks> qres;
	q.storein(qres);
	if (qres.size()==0)	{		// did not find hash in temporary stored blocks
		ui = uint256(0);
		return false;
	}
	else {
		memcpy(ui.begin(), qres[0].hash.data(), 32);	// copy from result to passed reference
		return true;
	}		
}

// Retrieve height from a block with known hash
// Returns: -3  : hash not found
//			-2  : hash found, but height of block is unknown
//			>=0 : hash found and its height is known
// Function checks
//		1. ChainBlocks table which holds current unprocessed blocks to get height of it
//		2. if not found, checks if it is equal to hash of Genesis Block (first block we receive on first startup will contain prev hash = hash genesis block !)
int dbBTC::GetBlockHeightFromHash(BIRDDB_Query& q, BIRDDB::sql_blob &fromHash)
{
	size_t i,j;
	q << "SELECT ID, height FROM ChainBlocks WHERE hash=" << mysqlpp::quote << fromHash;
//	std::string s = q.str();
	mysqlpp::StoreQueryResult qres = q.store();
	if (qres.empty()) {			// did not find hash in temporary stored blocks, see if it matches Genesis block hash
		j = fromHash.size();
		char *p = (char *)hashGenesisBlock.begin();
		for (i=0; i<j; i++) {
			if (fromHash.at(i)!=*p++)
				break;
		}
		return (i==j)? 0: -3;  // return 0 if fromHash=hashGenesisBlock, -3 otherwise (as hash is not found)
	}
	else {
		return qres[0][1];
	}
}

// delete the block with the given height, including related TxIns and TxOuts from Chain___ tables
// we have "on delete cascade" option so TxIns and TxOuts are automatically removed if we delete the record in the ChainBlocks table
void dbBTC::DeleteBlockDataOfHeight(BIRDDB_Query& q, int iHeight)
{
	if (iHeight>0) {
		BOOST_LOG_TRIVIAL(info) << "Deleting block " << iHeight << " from ChainBlocks";
		q << "DELETE FROM ChainBlocks WHERE status<90 AND height=" << iHeight << " LIMIT 5";	// include status check so that 'undeleteable' blocks are never deleted, include LIMIT to avoid error 1175 wehn in safe update mode !
		q.exec();
	}
}

// Adds a temporary block to ChainBlocks using the given data
// Returns in newBlock.ID the ID of the record just created
// Returns false if execute query fails, true otherwise
bool dbBTC::AddBlockToChain(BIRDDB_Query& q, ChainBlocks &newBlock)
{
	q << "UPDATE pkCounters SET pkCount=LAST_INSERT_ID(pkCount+1) WHERE ID=3";     // update query to get next ID
	bool bResult=q.exec();
	if (!bResult)
		return bResult;											// abort is update went wrong
	newBlock.ID=(mysqlpp::sql_int_unsigned)q.insert_id();
	// construct record to add
	try {
		q.insert(newBlock);
		bResult = q.exec();
		if (bResult) {
			BOOST_LOG_TRIVIAL(trace) << "Inserted block ID " << newBlock.ID << " into ChainBlocks";
		}
	}
	catch(mysqlpp::BadQuery e) {
		BOOST_LOG_TRIVIAL(error) << "Adding block ID " << newBlock.ID << " to ChainBlocks failed:" << e.what();
		q.reset();
		bResult=false;
	}
	return bResult;
}

// Insert a new (incomplete) record into ChainTxs
bool dbBTC::InsertChainTx(BIRDDB_Query& q, ChainTxs &newTx)
{
	q.insert(newTx);						// create insert query
	bool b=false;
	try {
		b = q.exec();
	}
	catch(mysqlpp::BadQuery e) {
		BOOST_LOG_TRIVIAL(error) << "Inserting chain tx failed: " << e.what();
		q.reset();
	}
	return b;
}

// Updates transaction hash once we processed complete raw transaction from a message
bool dbBTC::UpdateChainTx(BIRDDB_Query& q, ChainTxs &updatedTx)
{
	q << "UPDATE " << updatedTx.table() << " SET " << updatedTx.equal_list("", ChainTxs_txHash);
	q << " WHERE " << updatedTx.equal_list(" and ", true, true, false);
	bool b=false;
	try {
		b = q.exec();
	}
	catch(mysqlpp::BadQuery e) {
		BOOST_LOG_TRIVIAL(error) << "Updating chain tx failed: " << e.what();
		q.reset();
	}
	return b;
}

// Insert a new record into ChainTxIns
bool dbBTC::InsertChainTxIn(BIRDDB_Query& q, ChainTxIns &newTxIn)
{
	q.insert(newTxIn);						// create insert query
	bool b=false;
	try {
		b = q.exec();
	}
	catch(mysqlpp::BadQuery e) {
		BOOST_LOG_TRIVIAL(error) << "Inserting chain txin failed: " << e.what();
		q.reset();
	}
	return b;
}

bool dbBTC::InsertChainTxOut(BIRDDB_Query& q, ChainTxOuts &newTxOut)
{
	q.insert(newTxOut);
	bool b=false;
	try {
		b = q.exec();
	}
	catch(mysqlpp::BadQuery e) {
		BOOST_LOG_TRIVIAL(error) << "Inserting chain txout failed: " << e.what();
		q.reset();
	}
	return b;
}

// Insert a new record into TxUnconfirmed
bool dbBTC::InsertTxUnconfirmed(BIRDDB_Query& q, TxUnconfirmed &newTx)
{
	bool b=false;
	q.insert(newTx);						// create insert query
	try {
	  b = q.exec();
	}
	catch (mysqlpp::BadQuery e) {
		BOOST_LOG_TRIVIAL(error) << "Inserting unconfirmed tx failed: " << e.what();
		ShowError(IDS_Err_BadQuery, e.what());
		q.reset();
	}
	if (b)
		newTx.ID = (mysqlpp::sql_int_unsigned)q.insert_id();
	return b;
}

// Insert a new record into TxInUnconfirmed
bool dbBTC::InsertTxInUnconfirmed(BIRDDB_Query& q, TxInUnconfirmed &newTx)
{
	q.insert(newTx);						// create insert query
	bool bRes = false;
	try {
		bRes = q.exec();
	}
	catch (mysqlpp::BadQuery e) {
		ShowError(IDS_Err_BadQuery, e.what());
		q.reset();
	}
	return bRes;
}

// Set life to zero for the given unconfirmed transaction
void dbBTC::TxUnconfirmedDies(BIRDDB_Query& q, BIRDDB::sql_blob& txhash)
{
	// query: UPDATE TxUnconfirmed SET life=0 WHERE hash='txhash'
	q.reset();
	try {
		q << "SET SQL_SAFE_UPDATES=0";
		q.exec();
		q << "UPDATE TxUnconfirmed SET life=0 WHERE hash=" << mysqlpp::quote << txhash;
		q.exec();
		q << "SET SQL_SAFE_UPDATES=1";
		q.exec();
	}
	catch (mysqlpp::BadQuery e) {
		BOOST_LOG_TRIVIAL(error) << "TxUnconfirmedDies bad query:" << e.what();
		q.reset();
	}
}

// Decrease life and remove txs with life<0
void dbBTC::TxUnconfirmedAges(BIRDDB_Query& q)
{
	try {
		q << "SET SQL_SAFE_UPDATES=0";
		q.exec();
		q << "UPDATE TxUnconfirmed SET life=life-1";
		q.exec();
		q << "DELETE FROM TxUnconfirmed WHERE life<0";
		q.exec();
		q << "SET SQL_SAFE_UPDATES=1";
		q.exec();
	}
	catch (mysqlpp::BadQuery e) {
		BOOST_LOG_TRIVIAL(error) << "TxUnconfirmedAges bad query:" << e.what();
		q.reset();
	}
}

// Count the number of unconfirmed transactions
int dbBTC::NrTxUnconfirmed(BIRDDB_Query& q)
{
	q << "SELECT COUNT(*) FROM TxUnconfirmed WHERE life>0";
	mysqlpp::StoreQueryResult qres = q.store();
	return qres[0][0];
}

bool dbBTC::CheckHeights(BIRDDB_Query& q)
{
	bool bFoundHeight = false;
	bool bFoundInFor;
	vector<ChainBlocks> vChain;
	int iChain, iHeight;
	do {
		vChain.clear();
		q << "SELECT * FROM ChainBlocks WHERE status<1";	// get all blocks with unknown height
		q.storein(vChain);
		bFoundInFor=false;
		for (iChain=0; iChain!=vChain.size(); iChain++) {	// loop through all blocks with unknown height
			iHeight = GetBlockHeightFromHash(q, vChain[iChain].prevhash);		// see if we have previous hash, and if so, what height is has
			if (iHeight>=-1) {	// found the previous hash, and it has a meaningfull height
				ChainBlocks newBlock = vChain[iChain];
				newBlock.height = iHeight+1;
				newBlock.status = 1;
				q.update(vChain[iChain], newBlock);	// make update query
				q.exec();							// and execute it
				bFoundHeight=true;					// notify calling procedure we could resolve some blocks
				bFoundInFor=true;					// force repeat of do-while
				vChain.clear();						// clear the vector for next storein !
				break;								// force exit of for loop as we got to start from the beginning again
			}
		}
	} while (bFoundInFor);			// repeat the whole thing until for loop looped through all blocks without finding anything usefull

	vChain.clear();
	iHeight = GetBestHeight(q);
	q << "SELECT * FROM ChainBlocks WHERE height>=" << iHeight << " ORDER BY height";	// retrieve all blocks with height equal or better then current best height
	q.storein(vChain);
	for (iChain=0; iChain<vChain.size(); iChain++,iHeight++) {
		if (vChain[iChain].height > iHeight)
			break;
	}
	if (iChain>1)	// chain extended with at least one block
		SetBestHeight(q, iHeight-1);


// optimization on initial block download:
//  probably some new blocks at end of chain arrive while downloading, these will clutter up the table
//	so each run with nothing resolved will decrease height by 1 of all status=0 blocks
//	and we will delete all heights < -100
//	if (!bFoundHeight) {
//		q << "UPDATE ChainBlocks SET height=height-1 WHERE status=0";
//		q.execute();
//		q << "DELETE FROM ChainBlocks WHERE height<-100";
//		q.execute();
//	}
// ABOVE IDEA WAS BAD, new algorithm:
//		blocks arriving without a determined height are probably new blocks found while synchronizing (from previous load to up-to-date situation)
//		so we will keep them !
//		Once block chain is "up-to-date" (=equal height as advertised by peer on version message), height will be decreased on each call to this procedure.
//		Once they hit height -100, we'll delete those blocks, as they are probably orphan blocks not part of the main block chain
	if (!bFoundHeight && BTCnode.GetNodeStatus()>=5) {	// no blocks resolved and chain is up-to-date
		q << "SET SQL_SAFE_UPDATES=0";
		q.exec();
		q << "UPDATE ChainBlocks SET height=height-1 WHERE status=0";
		q.exec();
		q << "DELETE FROM ChainBlocks WHERE height<-100";
		q.exec();
		q << "SET SQL_SAFE_UPDATES=1";
		q.exec();
	}
	return bFoundHeight;
}

#ifdef CTB_V2
int dbBTC::ConfirmTempBlocks(BIRDDB_ConnectionPtr my_conn, DWORD dwSleep)
{
	unsigned int iBlockID;
	int iBlockDepth=GetBlockDepth();
	BIRDDB_Query q = my_conn->query();
	int iBestHeight = GetBestHeight(q);	// take snapshot of current heights
	int iSafeHeight = GetSafeHeight(q);
	if (iSafeHeight<0) {
		BOOST_LOG_TRIVIAL(error) << "Negative SafeHeight (" << iSafeHeight << ") detected while confirming blocks.";
		return iSafeHeight;
	}
	int iConfirmHeight = iSafeHeight + iBlockDepth;								// limit to maximum 'block_depth' confirmations at a time
	if (iConfirmHeight > iBestHeight - iBlockDepth)
		iConfirmHeight=iBestHeight - iBlockDepth;									// and keep at least 'block_depth' blocks 'temporary'
	if (block_multiprune_start>=0 && iConfirmHeight>=block_multiprune_start)
		iConfirmHeight = block_multiprune_start -1;								// if multipruning running, don't confirm any of its blocks yet

	//if (iConfirmHeight>iSafeHeight) {					// something to do
		//step 0: PruneTxMultiBlock: remove tx I/O from unconfirmed but safe blocks ("coins get spent in safe blocks")
		//		  This eliminates the work to add and later delete again the coins in our large TxOutAvailable table.
		//        Doing it already in the Chain___ tables should be much faster (as they will not grow very large).
	//	int iPruneUpTo = iBestHeight-iNrSpent;
	//	if (iPruneUpTo-block_multiprune > ((iPruneUpTo-iSafeHeight)>>3)) {   // limit the number of multiprune calls on initial downloads
	//		boost::lock_guard<boost::mutex> lock(dbmutex);
	//		q << "CALL PruneTxMultiBlock(" << iSafeHeight+1 << "," << iPruneUpTo << ")";
	//		BOOST_LOG_TRIVIAL(info) << "Multiblock pruning from height "<< iSafeHeight+1 << " to " << iPruneUpTo;
	//		bool b=false;
	//		try {
	//			b = q.exec();
	//			block_multiprune = iPruneUpTo;				// save up to which height pruning took place
	//		}
	//		catch ( mysqlpp::BadQuery e) {
	//			BOOST_LOG_TRIVIAL(error) << "Multiblock pruning failed: " << e.what();
	//			q.reset();
	//		}
	//		catch (...) {
	//			BOOST_LOG_TRIVIAL(error) << "Multiblock pruning failed with unknown exception";
	//			q.reset();
	//		}
	//	}
	//}
	block_confirm_end = iConfirmHeight;				// save result in private variable class
	mysqlpp::StoreQueryResult qres;					// general StoreQueryResult
	vector<pkTxOutSpent> vOutSpent;					// results of "outputs spent" 
	vector<vTxOutAvailable> vOutAvail;				// results of OA = outputs available
	mysqlpp::SimpleResult qsimple;
	while (iConfirmHeight > iSafeHeight)  {			// chain has more then 'block-depth' blocks unconfirmed
		iSafeHeight++;								// new safe height will be one higher if everything goes well
		q << "SELECT ID, status from ChainBlocks WHERE height=" << iSafeHeight;	// get ID for this block height
		qres = q.store();
		if (qres.empty()) {	// o-oh, can't find block
			ShowError(IDS_Err_MissingTempBlock, iSafeHeight);
			BOOST_LOG_TRIVIAL(error) << "Could not find block height "<< iSafeHeight << " while confirming blocks";
			iConfirmHeight=iSafeHeight;
			break;			// no point in continuing
		}
		iBlockID = (unsigned int)qres[0][0];
		
		//step 1:  do cleanup
		//step 1a: clear our memory table to hold IDs
		q << "TRUNCATE TABLE pkTxOutSpent";
		q.exec();
		vOutSpent.clear();

		//step1b: remove transaction outputs that are also inputs in the same block ("coins get spent in same block")
		//        update v0.2: already performed when the block transactions were first stored (PruneInsideBlock), no need to repeat

		//step1c: update status of block to reflect ongoing transfer
		q << "UPDATE ChainBlocks SET status=98 WHERE ID=" << iBlockID;
		if (!q.exec()) {
			BOOST_LOG_TRIVIAL(error) << "Unable to set status to 98 for block ID "; iBlockID;
			ShowError(IDS_Err_UnableToStoreTxOutAvailable);
			iConfirmHeight=iSafeHeight;
			break;
		}

		//step 2: get, for all transactions in this block, for each transaction input (that is not coinbase) the corresponding ID in the TxOutAvailable table
		//        these IDs = previous outputs that are now spent and can be forgotten
		q << "INSERT INTO pkTxOutSpent SELECT ID AS txID, smartbtcaddr AS smartID, storebtcaddr AS storeID FROM TxOutAvailable t INNER JOIN ChainTxIns c ON (t.Hash=c.opHash AND t.txindex=c.opN) WHERE c.BlockID=" << iBlockID;  //  << " AND c.opN>=0";
		q.exec();
		q << "SELECT * FROM pkTxOutSpent ORDER BY txID";
		q.storein(vOutSpent);

		//step 3:  get, for all transactions in this block, all needed info for each transaction ouput
		//		   these records are the new spendable outputs
		vOutAvail.clear();
		q << "SELECT c.txHash as hash, co.txOutN as txindex, co.txType as txtype, co.value as txamount, co.smartID as smhash, co.smartIDAdr as smadr, b1.ID as smid, co.storeID as sthash, IF(co.storeID='";
		q << "\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0\\0',0,b2.ID) as stid";
		q << " FROM ChainTxOuts co INNER JOIN ChainTxs c ON (c.blockID=co.blockID AND c.txN=co.txN)";
		q << " LEFT JOIN bitcoinaddress b1 ON (co.smartID=b1.hash160)";
		q << " LEFT JOIN bitcoinaddress b2 ON (co.storeID=b2.hash160)";
		q << " WHERE co.blockID=" << iBlockID;
		q.storein(vOutAvail);
		unsigned int iNrNeeded = (unsigned int)vOutAvail.size();

		//step 4: if new spendable outputs have 'new' bitcoinaddresses, add them to the BitcoinAddress table
		//step 4a: first pass to see how many new addresses (so we can allocate IDs with a single call to the database)
		int iNrNewBTCAdr=0;
		unsigned int ip1;
		for (ip1=0; ip1<iNrNeeded; ++ip1) {
			if (vOutAvail[ip1].smid.is_null)
				iNrNewBTCAdr++;
			if (vOutAvail[ip1].stid.is_null)
				iNrNewBTCAdr++;
		}
		//  if NULLs were found, make sure we have enough IDs (single database call to get all extra IDs)
		if (iNrNewBTCAdr>0) {
			CheckQueueBTCID(q, iNrNewBTCAdr + 500);	// at least 500 IDs available after we are done confirming the block
		}
		//step 4b: second pass to add new addresses
		for (ip1=0; ip1<iNrNeeded; ++ip1) {
			if (vOutAvail[ip1].smid.is_null) {
				unsigned int uia = 0;
				while (uia==0)
					uia = AddBitcoinAddress(q, vOutAvail[ip1].smhash, vOutAvail[ip1].smadr);
				vOutAvail[ip1].smid = uia;
			}
			if (vOutAvail[ip1].stid.is_null) {
				unsigned int uia = 0;
				while (uia==0)
					uia = AddBitcoinAddress(q, vOutAvail[ip1].sthash, mysqlpp::String(NULL, 0, mysqlpp::mysql_type_info::string_type, true));
				vOutAvail[ip1].stid = uia;
			}
			if (vOutAvail[ip1].stid ==  mysqlpp::sql_int_unsigned_null(0))
				vOutAvail[ip1].stid=mysqlpp::null;
		}

		//step 5: check that we have enough IDs useable to update/insert rows in TxOutAvailable
		if (iNrNeeded > vOutSpent.size()) {
			CheckQueueTxOut(q, iNrNeeded - (unsigned int)vOutSpent.size() + 500);		// at least 500 IDs available after we are done confirming the block
		}

		//step 6: update database: move from ChainBlocks to TxOutAvailable
		{
		boost::lock_guard<boost::mutex> lock(dbmutex);		// get database lock before starting to avoid deadlocks
		mysqlpp::Transaction myTrans(*my_conn);				// wrap 1 block inside a transaction
		bool bRes=false;
		for (unsigned int j=0; j<iNrNeeded; ++j) {       // loop through all needed txouts that need an ID
			TxOutAvailable toa(0, vOutAvail[j].hash, vOutAvail[j].txindex, vOutAvail[j].txtype, vOutAvail[j].txamount, vOutAvail[j].smid.data, vOutAvail[j].stid, iSafeHeight);
			bRes=false;
			if (j<vOutSpent.size()) {			// first use UPDATE on spent txouts (optimized instead of DELETE and INSERT of same ID)
				toa.ID=vOutSpent[j].txID;		// reuse the ID
				q << "UPDATE " << toa.table() << " SET " << toa.equal_list() << " WHERE " << toa.equal_list(" and ", true);
			}
			else {						// now use INSERT for new IDs
				if (cqReuseableTxOut.try_pop(toa.ID)) {
					q.insert(toa);
				}
				else {
					q.reset();
					break;		// abort cause we're unable to get a value as ID
				}
			}
			try {
				bRes = q.exec();
			}
			catch (mysqlpp::BadQuery e) {
				if (e.errnum() == 1062) {
					BOOST_LOG_TRIVIAL(error) << "Skipping adding tx in block " << iSafeHeight << ": " << e.what();
					q.reset();
					bRes=true;
				}
				else
				{
					BOOST_LOG_TRIVIAL(fatal) << "Badquery trying to add tx in block " << iSafeHeight << ": " << e.what();
					q.reset();
					bRes=false;
				}
			}
			if (!bRes) 
				break;
		}
		if (iNrNeeded!=0 && !bRes) { // something wrong in loop
			myTrans.rollback();
			BOOST_LOG_TRIVIAL(error) << "Problems in 1st phase confirming block " << iSafeHeight << ". Rolling back.";
			ShowError(IDS_Err_UnableToStoreTxOutAvailable);
		}
		else { // for loop went ok
			// DO DELETE OF REMAINING SPENT OUTPUTS (if any) and STORE THOSE ID's in concurrent queue object for later reuse
			if (vOutSpent.size()>iNrNeeded) {
				q << "SET SQL_SAFE_UPDATES=0";
				bRes = q.exec();
				if (iNrNeeded!=0) {
					// Delete from spent table the ID's already used
					q << "DELETE FROM pkTxOutSpent ORDER BY txID LIMIT " << iNrNeeded;
					bRes = q.exec();
				}
				if (bRes) {
					q << "DELETE FROM TxOutAvailable USING TxOutAvailable INNER JOIN pkTxOutSpent ON TxOutAvailable.ID=pkTxOutSpent.txID";
					bRes= q.exec();
				}
				if (bRes) {
					for (int j=iNrNeeded; j<vOutSpent.size(); ++j) {
						cqReuseableTxOut.push(vOutSpent[j].txID);
					}
				}
				q << "SET SQL_SAFE_UPDATES=1";
				bRes = q.exec();
			}
			if (iNrNeeded!=0 && !bRes) { // something wrong in loop
				myTrans.rollback();
				BOOST_LOG_TRIVIAL(error) << "Problems in 2nd phase confirming block " << iSafeHeight << ". Rolling back.";
				ShowError(IDS_Err_UnableToStoreTxOutAvailable);
			}
			else {
				myTrans.commit();	// commit if everything went without errors
				BOOST_LOG_TRIVIAL(trace) << "Block " << iSafeHeight << "confirmed";
			}
		}
		} // release mutex
		// step 6: check for possible unused bitcoin addresses
		for (int j=0; j<vOutSpent.size(); j++) {		// each TxOutAvailable output that got spent has 2 possible superfluous bitcoin addresses
			DeleteBitcoinAddressCheck(q, vOutSpent[j].smartID);
			if (!(vOutSpent[j].storeID.is_null))
				DeleteBitcoinAddressCheck(q, vOutSpent[j].storeID.data);
			//q << "DELETE FROM BitcoinAddress WHERE ID=" << vOutSpent[j].smartID;
			//try {
			//	q.exec();											// try to delete it
			//	cqReuseableBTCID.push(vOutSpent[j].smartID);		// it worked, so now it is reuseable
			//}
			//catch (const mysqlpp::BadQuery e) {
			//	if (e.errnum() != 1451) {   
			//		BOOST_LOG_TRIVIAL(error) << "Badquery when deleting a bitcoinaddress: " << e.what();
			//	}
			//	q.reset();  // can't delete bitcoin address because of foreign key constraint --> quietly ignore
			//}
			//if (!vOutSpent[j].storeID.is_null) {
			//	q << "DELETE FROM BitcoinAddress WHERE ID=" << vOutSpent[j].storeID;
			//	try {
			//		q.exec();												// try to delete it
			//		cqReuseableBTCID.push(vOutSpent[j].storeID.data);		// it worked, so now it is reuseable
			//	}
			//	catch (const mysqlpp::BadQuery e) {
			//		if (e.errnum() != 1451) {   
			//			BOOST_LOG_TRIVIAL(error) << "Badquery when deleting a store bitcoinaddress: " << e.what();
			//		}
			//		q.reset();
			//	}
			//}
		}

		// step 7: finalize confirmation of this block
		{
		boost::lock_guard<boost::mutex> lock(dbmutex);		// get database lock before starting to avoid deadlocks
		mysqlpp::Transaction myTrans2(*my_conn);			// wrap status updates inside a transaction
		q << "SET SQL_SAFE_UPDATES=0";
		q.exec();
		q << "DELETE FROM ChainBlocks WHERE status=99";		// delete old confirmed blocks, and due to cascade so all Txs, TxIns and TxOuts get deleted too
		qsimple = q.execute();
		q << "SET SQL_SAFE_UPDATES=1";
		q.exec();
		q << "UPDATE ChainBlocks SET status=99 WHERE ID=" << iBlockID;	// our processed block is now confirmed
		if (q.execute()) {
			q << "UPDATE pkCounters SET pkCount=" << iSafeHeight << " WHERE ID=4";	// save new safe height in database table pkCounters
			qsimple = q.execute();
			myTrans2.commit();
		}
		else {
			myTrans2.rollback();
			BOOST_LOG_TRIVIAL(error) << "Unable to set status to 99 for the confirmed block ID=" << iBlockID;
			ShowError(IDS_Err_MissingTempBlock, iSafeHeight);
		}
		}
		TCHAR szStaticText[10];
		_itow_s(iSafeHeight, szStaticText, 10, 10);
		SetWindowText(hStaticProcessedBlock,szStaticText);   
	}
	Sleep(dwSleep);
	return iSafeHeight;
}
#else
bool dbBTC::ConfirmTempBlocks(mysqlpp::Connection *my_conn)
{
	if (my_conn==NULL)
		return false;
	mysqlpp::Query q = my_conn->query();
	mysqlpp::StoreQueryResult qres;
	mysqlpp::SimpleResult qsimple;
	vector<ChainTxs> vTxs;
	vector<ChainTxIns> vTxIns;
	vector<ChainTxOuts> vTxOuts;
	int iBlockID;
	int iBestHeight = GetBestHeightKnown(my_conn);	// take snapshot of current heights
	int iSafeHeight = GetSafeHeight(my_conn);
	if (iBestHeight> (iSafeHeight+20))
		iBestHeight=iSafeHeight+20;					// limit to maximum 10 confirmations
	while (iBestHeight > iSafeHeight+10)  {			// chain has more then 10 blocks unconfirmed
		iSafeHeight++;								// new safe height will be one higher if everything goes well
		q << "SELECT ID from ChainBlocks WHERE height=" << iSafeHeight;	// get ID for this block height
		qres = q.store();
		if (qres.empty()) {	// o-oh, can't find block
			ShowError(IDS_Err_MissingTempBlock, iSafeHeight);
			break;			// no point in continuing
		}
		iBlockID = qres[0][0];
		vTxs.clear();
		q << "SELECT * FROM ChainTxs WHERE blockID=" << iBlockID;		// get all transactions in this block
		q.storein(vTxs);
		if (vTxs.size()==0) { // o-oh, can't find any related transactions
			ShowError(IDS_Err_MissingTxs, iSafeHeight);
			break;
		}
		DWORD dwWaitResult;
		do {
			dwWaitResult = WaitForSingleObject(ghMutexDBLock, INFINITE);		// get database lock before starting transaction
		} while (dwWaitResult==WAIT_ABANDONED);
		if (dwWaitResult != WAIT_OBJECT_0) {
			ShowError(IDS_Err_Mutex);
			break;							// can't get database lock
		}
		mysqlpp::Transaction myTrans(*my_conn);							// wrap 1 block inside a transaction
		// loop through all transactions and:
		//    - get related Txins: remove from TxOutAvailable these txins
		//	  - get related Txouts: add to TxOutAvailable these txouts

		BOOST_FOREACH(ChainTxs &chTx, vTxs) {
			// retrieve TxIns:
			vTxIns.clear();
			q << "SELECT * FROM ChainTxIns WHERE " << chTx.equal_list(" and ", ChainTxs_blockID, ChainTxs_txN);
			q.storein(vTxIns);
			vTxOuts.clear();
			q << "SELECT * FROM ChainTxOuts WHERE " << chTx.equal_list(" and ", ChainTxs_blockID, ChainTxs_txN);
			q.storein(vTxOuts);
			if (vTxIns.size()>0 && vTxOuts.size()>0) {	// transaction I/O's found
				BOOST_FOREACH(ChainTxIns &chTxIn, vTxIns) {		// for each input: delete the TxOut in TxOutAvailable
					if (chTxIn.opN>=0) {		// generation input has index -1, so that's not a valid previous transaction
						DeleteTxOut(my_conn, chTxIn.opHash, chTxIn.opN);
					}
				}
				BOOST_FOREACH(ChainTxOuts &chTxOut, vTxOuts) {	// for each output: add it as a TxOutAvailable
					if (AddTxOut(my_conn, iSafeHeight, chTxOut, chTx.txHash)==0)
						ShowError(IDS_Err_UnableToStoreTxOutAvailable);
				}
			}
		}
		// all inputs and outputs processed:
		q << "DELETE FROM ChainBlocks WHERE status=2";  // delete old confirmed blocks, and due to cascade so all Txs, TxIns and TxOuts get deleted too
		qsimple = q.execute();
		q << "UPDATE ChainBlocks SET status=2 WHERE ID=" << iBlockID;	// our processed block is now confirmed
		if (q.execute()) {
			q << "UPDATE pkCounters SET pkCount=" << iSafeHeight << " WHERE ID=4";	// save new safe height in database table pkCounters
			qsimple = q.execute();
			myTrans.commit();
		}
		else {
			myTrans.rollback();
			ShowError(IDS_Err_MissingTempBlock, iSafeHeight);
		}
		if (!ReleaseMutex(ghMutexDBLock))
			ShowError(IDS_Err_Mutex);
	}
	return true;
}
#endif

// CombineReplace:
//		* combines path and filename in a safe way
//		* optionally deletes that file if it exists
//		* returns a standard string with forward slashes to use in MySQL
void CombineReplace(std::string& sOut, const std::string& strPath, const std::string& strFile, bool bDelete=false)
{
	boost::filesystem::path p;
	p /= strPath;
	p /= strFile;		// combine path and file name
	if (bDelete)
		boost::filesystem::remove(p);							// delete file if exists
	sOut = p.string();
}

bool dbBTC::DumpDatabase(HWND hDlg, const char *szPath)
{
	HWND hProgressBar = GetDlgItem(hDlg, IDC_PROGRESS1);
	HWND hText = GetDlgItem(hDlg, IDC_STCOMMENT);
	TCHAR szText[MAX_LOADSTRING];
	mysqlpp::Connection *my_conn = this->GrabConnection();
	if (my_conn == NULL)
		return false;						// we are not connected, so we can't get an ID
	mysqlpp::Query q = my_conn->query();
	mysqlpp::SimpleResult qsimple;
	std::string strPath(szPath);
	std::string strOutFile;			// path+filename for sql in standard string

// Visualize the process
	SendMessage(hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 7));		// set range 0 to 7
	SendMessage(hProgressBar, PBM_SETPOS, 0, 0);						// set position to 0
	ShowWindow(hProgressBar, SW_SHOW);
	LoadString(hInst, IDS_WRITEFILES, szText, MAX_LOADSTRING);
	SetWindowText(hText, szText);

// dump table BitcoinAddress
	CombineReplace(strOutFile, strPath, "BitcoinAddress.txt", true);
	q << "SELECT ID, base58, HEX(hash160) FROM BitcoinAddress INTO OUTFILE " << mysqlpp::quote << strOutFile;
	q << " FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"' LINES TERMINATED BY '\\n'";
	try {
		qsimple=q.execute();
	}
	catch (const mysqlpp::BadQuery& er) {   // handle query errors
		ShowError(IDS_Err_BadQuery, er.what());
		ReleaseConnection(my_conn);
		return false;
	}
	SendMessage(hProgressBar, PBM_SETPOS, 1, 0);						// set position to 1

// dump table TxOutAvailable
	CombineReplace(strOutFile, strPath, "TxOutAvailable.txt", true);
	q << "SELECT ID, HEX(hash), txindex, txtype, txamount, smartbtcaddr, storebtcaddr, blockheight FROM TxOutAvailable INTO OUTFILE " << mysqlpp::quote << strOutFile;
	q << " FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"' LINES TERMINATED BY '\\n'";
	try {
		qsimple=q.execute();
	}
	catch (const mysqlpp::BadQuery& er) {   // handle query errors
		ShowError(IDS_Err_BadQuery, er.what());
		ReleaseConnection(my_conn);
		return false;
	}
	SendMessage(hProgressBar, PBM_SETPOS, 2, 0);						// set position to 2

	// dump table ChainBlocks
	CombineReplace(strOutFile, strPath, "ChainBlocks.txt", true);
	q << "SELECT ID, HEX(hash), HEX(prevhash), height, status FROM ChainBlocks INTO OUTFILE " << mysqlpp::quote << strOutFile;
	q << " FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"' LINES TERMINATED BY '\\n'";
	try {
		qsimple=q.execute();
	}
	catch (const mysqlpp::BadQuery& er) {   // handle query errors
		ShowError(IDS_Err_BadQuery, er.what());
		ReleaseConnection(my_conn);
		return false;
	}
	SendMessage(hProgressBar, PBM_SETPOS, 3, 0);						// set position to 3

	// dump table ChainTxs
	CombineReplace(strOutFile, strPath, "ChainTxs.txt", true);
	q << "SELECT blockID, txN, HEX(txhash) FROM ChainTxs INTO OUTFILE " << mysqlpp::quote << strOutFile;
	q << " FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"' LINES TERMINATED BY '\\n'";
	try {
		qsimple=q.execute();
	}
	catch (const mysqlpp::BadQuery& er) {   // handle query errors
		ShowError(IDS_Err_BadQuery, er.what());
		ReleaseConnection(my_conn);
		return false;
	}
	SendMessage(hProgressBar, PBM_SETPOS, 4, 0);						// set position to 4

	// dump table ChainTxIns
	CombineReplace(strOutFile, strPath, "ChainTxIns.txt", true);
	q << "SELECT blockID, txN, txInN, HEX(opHash), opN FROM ChainTxIns INTO OUTFILE " << mysqlpp::quote << strOutFile;
	q << " FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"' LINES TERMINATED BY '\\n'";
	try {
		qsimple=q.execute();
	}
	catch (const mysqlpp::BadQuery& er) {   // handle query errors
		ShowError(IDS_Err_BadQuery, er.what());
		ReleaseConnection(my_conn);
		return false;
	}
	SendMessage(hProgressBar, PBM_SETPOS, 5, 0);						// set position to 5

	// dump table ChainTxOuts
	CombineReplace(strOutFile, strPath, "ChainTxOuts.txt", true);
	q << "SELECT blockID, txN, txOutN, value, HEX(smartID), HEX(storeID), txType FROM ChainTxOuts INTO OUTFILE " << mysqlpp::quote << strOutFile;
	q << " FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"' LINES TERMINATED BY '\\n'";
	try {
		qsimple=q.execute();
	}
	catch (const mysqlpp::BadQuery& er) {   // handle query errors
		ShowError(IDS_Err_BadQuery, er.what());
		ReleaseConnection(my_conn);
		return false;
	}
	SendMessage(hProgressBar, PBM_SETPOS, 6, 0);						// set position to 6

	// dump table pkCounters
	CombineReplace(strOutFile, strPath, "pkCounters.txt", true);
	q << "SELECT ID, pkCount FROM pkCounters INTO OUTFILE " << mysqlpp::quote << strOutFile;
	q << " FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"' LINES TERMINATED BY '\\n'";
	try {
		qsimple=q.execute();
	}
	catch (const mysqlpp::BadQuery& er) {   // handle query errors
		ShowError(IDS_Err_BadQuery, er.what());
		ReleaseConnection(my_conn);
		return false;
	}
	SendMessage(hProgressBar, PBM_SETPOS, 7, 0);						// set position to 7
	ReleaseConnection(my_conn);

	return true;
}

bool dbBTC::DeleteTable(BIRDDB_Query& q, const char *sTable, HWND hPBar, int iPos)
{
	q << "DELETE QUICK FROM " << sTable;
	try {
		mysqlpp::SimpleResult qsimple=q.execute();
	}
	catch (const mysqlpp::BadQuery& er) {		// handle query errors
		ShowError(IDS_Err_BadQuery, er.what());
		return false;
	}	
	SendMessage(hPBar, PBM_SETPOS, iPos, 0);	// set position of progress bar
	return true;
}

bool dbBTC::LoadDatabase(HWND hDlg, const char *szPath, bool bLocal)
{
	HWND hProgressBar = GetDlgItem(hDlg, IDC_PROGRESS1);
	HWND hText = GetDlgItem(hDlg, IDC_STCOMMENT);
	BIRDDB_ConnectionPtr my_conn = this->GrabConnection();
	if (my_conn == NULL)
		return false;						// we are not connected, so we can't get an ID
	mysqlpp::Query q = my_conn->query();
	mysqlpp::SimpleResult qsimple;
	std::string strPath(szPath);
	std::string strOutFile;			// path+filename for sql in standard string
	char *szLoadStatement;
	bool bOk;
	TCHAR szText[MAX_LOADSTRING];

// Visualize the process
	SendMessage(hProgressBar, PBM_SETRANGE, 0, MAKELPARAM(0, 14));		// set range 0 to 14
	SendMessage(hProgressBar, PBM_SETPOS, 0, 0);						// set position to 0
	ShowWindow(hProgressBar, SW_SHOW);
	LoadString(hInst, IDS_DELETETABLE, szText, MAX_LOADSTRING);
	SetWindowText(hText, szText);

// First empty all tables, we use DELETE syntax as we have foreign key constraints in the tables
	bOk = DeleteTable(q, "TxOutAvailable", hProgressBar, 1);
	bOk = bOk && DeleteTable(q, "ChainTxIns", hProgressBar, 2);
	bOk = bOk && DeleteTable(q, "ChainTxOuts", hProgressBar, 3);
	bOk = bOk && DeleteTable(q, "ChainTxs", hProgressBar, 4);
	bOk = bOk && DeleteTable(q, "ChainBlocks", hProgressBar, 5);
	bOk = bOk && DeleteTable(q, "BitcoinAddress", hProgressBar, 6);
	bOk = bOk && DeleteTable(q, "pkCounters", hProgressBar, 7);
	
	if (!bOk) {
		ShowError(IDS_Err_DeletingTables);
		ReleaseConnection(my_conn);
		return false;		// stop if we cannot empty all tables
	}

	LoadString(hInst, IDS_LOADFILES, szText, MAX_LOADSTRING);
	SetWindowText(hText, szText);

	szLoadStatement = (bLocal) ? "LOAD DATA LOCAL INFILE " : "LOAD DATA INFILE ";	// set correct load statement

// load table BitcoinAddress
	CombineReplace(strOutFile, strPath, "BitcoinAddress.txt", false);
	q << szLoadStatement << mysqlpp::quote << strOutFile << " INTO TABLE BitcoinAddress";
    q << " FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"' LINES TERMINATED BY '\\n'";
    q << " (ID, base58, @hhash) SET hash160=UNHEX(@hhash)";
	try {
		qsimple=q.execute();
	}
	catch (const mysqlpp::BadQuery& er) {   // handle query errors
		ShowError(IDS_Err_BadQuery, er.what());
	}
	SendMessage(hProgressBar, PBM_SETPOS, 8, 0);	// set position of progress bar

// load table ChainBlocks
	CombineReplace(strOutFile, strPath, "ChainBlocks.txt", false);
	q << szLoadStatement << mysqlpp::quote << strOutFile << " INTO TABLE ChainBlocks";
    q << " FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"' LINES TERMINATED BY '\\n'";
    q << " (ID, @hhash, @phash, height, status) SET hash=UNHEX(@hhash), prevhash=UNHEX(@phash)";
	try {
		qsimple=q.execute();
	}
	catch (const mysqlpp::BadQuery& er) {   // handle query errors
		ShowError(IDS_Err_BadQuery, er.what());
	}
	SendMessage(hProgressBar, PBM_SETPOS, 9, 0);						// set position to 9

// load table ChainTxs
	CombineReplace(strOutFile, strPath, "ChainTxs.txt", false);
	q << szLoadStatement << mysqlpp::quote << strOutFile << " INTO TABLE ChainTxs";
	q << " FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"' LINES TERMINATED BY '\\n'";
	q << " (blockID, txN, @htxhash) SET txhash=UNHEX(@htxhash)";
	try {
		qsimple=q.execute();
	}
	catch (const mysqlpp::BadQuery& er) {   // handle query errors
		ShowError(IDS_Err_BadQuery, er.what());
	}
	SendMessage(hProgressBar, PBM_SETPOS, 10, 0);						// set position to 10

// load table ChainTxIns
	CombineReplace(strOutFile, strPath, "ChainTxIns.txt", false);
	q << szLoadStatement << mysqlpp::quote << strOutFile << " INTO TABLE ChainTxIns";
	q << " FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"' LINES TERMINATED BY '\\n'";
	q << " (blockID, txN, txInN, @hopHash, opN) SET opHash=UNHEX(@hopHash)";
	try {
		qsimple=q.execute();
	}
	catch (const mysqlpp::BadQuery& er) {   // handle query errors
		ShowError(IDS_Err_BadQuery, er.what());
	}
	SendMessage(hProgressBar, PBM_SETPOS, 11, 0);						// set position to 11

// load table ChainTxOuts
	CombineReplace(strOutFile, strPath, "ChainTxOuts.txt", false);
	q << szLoadStatement << mysqlpp::quote << strOutFile << " INTO TABLE ChainTxOuts";
	q << " FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"' LINES TERMINATED BY '\\n'";
	q << " (blockID, txN, txOutN, value, @hsmartID, @hstoreID, txType) SET smartID=UNHEX(@hsmartID), storeID=UNHEX(@hstoreID)";
	try {
		qsimple=q.execute();
	}
	catch (const mysqlpp::BadQuery& er) {   // handle query errors
		ShowError(IDS_Err_BadQuery, er.what());
	}
	SendMessage(hProgressBar, PBM_SETPOS, 12, 0);						// set position to 12

// load table TxOutAvailable
	CombineReplace(strOutFile, strPath, "TxOutAvailable.txt", false);
	q << szLoadStatement << mysqlpp::quote << strOutFile << " INTO TABLE TxOutAvailable";
	q << " FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"' LINES TERMINATED BY '\\n'";
	q << " (ID, @hhash, txindex, txtype, txamount, smartbtcaddr, storebtcaddr, blockheight) SET hash=UNHEX(@hhash)";
	try {
		qsimple=q.execute();
	}
	catch (const mysqlpp::BadQuery& er) {   // handle query errors
		ShowError(IDS_Err_BadQuery, er.what());
	}
	SendMessage(hProgressBar, PBM_SETPOS, 13, 0);						// set position to 13

// load table pkCounters
	CombineReplace(strOutFile, strPath, "pkCounters.txt", false);
	q << szLoadStatement << mysqlpp::quote << strOutFile << " INTO TABLE pkCounters";
	q << " FIELDS TERMINATED BY ',' OPTIONALLY ENCLOSED BY '\"' LINES TERMINATED BY '\\n'";
	q << " (ID, pkCount)";
	try {
		qsimple=q.execute();
	}
	catch (const mysqlpp::BadQuery& er) {   // handle query errors
		ShowError(IDS_Err_BadQuery, er.what());
	}
	SendMessage(hProgressBar, PBM_SETPOS, 14, 0);						// set position to 14
	ReleaseConnection(my_conn);
	return true;
}

bool dbBTC::FillIDs(const char *szTable, Concurrency::concurrent_queue<unsigned int>& cq)
{
	BIRDDB_ConnectionPtr my_conn = this->GrabConnection();
	if (my_conn==NULL)
		return false;		// no connection, abort
	TCHAR szScan[MAX_LOADSTRING];
	TCHAR szText[MAX_LOADSTRING];
	TCHAR wcstring[MAX_LOADSTRING];				// buffer to convert to TCHAR
	size_t convertedChars = 0;
    mbstowcs_s(&convertedChars, wcstring, MAX_LOADSTRING, szTable, _TRUNCATE);
	LoadString(hInst, IDS_DS_SCANNING, szScan, MAX_LOADSTRING);
	swprintf_s(szText, MAX_LOADSTRING, szScan, wcstring);
	SetWindowText(hStaticSQLStatus, szText);
	InvalidateRect(hWndMain, &rcSQLStatus, true);
	BOOST_LOG_TRIVIAL(info) << "Scanning for unused IDs in table "<< szTable;

	//mysqlpp::Query q = my_conn->query("(SELECT (a.id+1) AS id FROM ");
	//q << szTable << " a WHERE NOT EXISTS (SELECT 1 FROM " << szTable << " b WHERE b.id=(a.id+1)) AND a.id NOT IN (SELECT MAX(c.id) FROM ";
	//q << szTable << " c)) UNION ALL ( SELECT (a.id-1) AS id FROM ";
	//q << szTable << " a WHERE NOT EXISTS (SELECT 1 FROM " << szTable << " b WHERE b.id=(a.id-1)) AND a.id NOT IN (SELECT MIN(c.id) FROM ";
	//q << szTable << " c)) ORDER BY id";

	//// query will return pairs of numbers: each pair = (start,stop) of missing range of items
	//mysqlpp::StoreQueryResult qres;
	//bool bRet;
	//try {
	//	bRet= (qres = q.store());
	//	if (bRet) {		// query executed normally
	//		for (size_t i = 0; i < qres.num_rows(); i+=2) {		// loop through each pair of rows
	//			unsigned int k = qres[i+1][0];		// stop value
	//			for (unsigned int j = qres[i][0]; j <= k; j++)	// add from "start" to "stop" to our vector
	//				cq.push(j);
	//		}
	//	}
	//}
	//catch (const mysqlpp::BadQuery& e) {	// handle query errors
	//	ShowError(IDS_Err_BadQuery, e.what());
	//	bRet=false;
	//}

	mysqlpp::Query q = my_conn->query("SELECT id FROM ");
	q << szTable << " ORDER BY id";

	mysqlpp::UseQueryResult qres;
	bool bRet;

	try {
		bRet= (qres = q.use());
		if (bRet) {  // query executed normally
			unsigned int k=1;
			while (mysqlpp::Row rrow = qres.fetch_row()) {
				unsigned int j = rrow[0];
				if (j != k) {   // missing ids
					while (j>k) 
						cq.push(k++);
				}
				k++;
			}
		}
	}
	catch (const mysqlpp::BadQuery& e) {	// handle query errors
		BOOST_LOG_TRIVIAL(error) << "Badquery in FillIDs: "<< e.what();
		ShowError(IDS_Err_BadQuery, e.what());
		bRet=false;
	}

	this->ReleaseConnection(my_conn);
	return bRet;	// return result of SQL query
}


// SendWaitingTxs
// Send all transactions with status=1 to the bitcoin network
bool dbBTC::SendWaitingTxs(void)
{
	bool bRet = true;
	BIRDDB_ConnectionPtr my_conn = this->GrabConnection();
	if (my_conn == NULL)
		return false;						// we are not connected, so we can't get an ID
	mysqlpp::Query q = my_conn->query("SELECT * FROM TxToSend WHERE txStatus=1");	// retrieve all transactions waiting to be sent
	vector<TxToSend> qres;
	q.storein(qres);
	if (qres.size()==0)	{		// no transactions waiting
		this->ReleaseConnection(my_conn);
		return bRet;
	}
	else {
		BOOST_FOREACH(TxToSend &txts, qres) {
			std::vector<unsigned char> vtx(txts.tx.begin(), txts.tx.end());
			if (BTCnode.SendMsg_Tx(vtx)) {	// succesfully transmitted to Bitcoin network
				TxToSend ori_tx = txts;
				txts.txStatus=2;				// reflect this in Status
				q.update(ori_tx, txts);
				q.execute();
			}
			else
				bRet=false;					// if any transaction fails, return failure
		}
	}
	this->ReleaseConnection(my_conn);
	return bRet;
}

bool dbBTC::OptimizeForDownload(BIRDDB_Query& q)
{
	if (eOptimizeStatus==opt_download)
		return true;					// database already in optimized download status
	boost::lock_guard<boost::mutex> lock(dbmutex);
	BOOST_LOG_TRIVIAL(info) << "Optimizing database for downloading";
	q << "CALL OptimizeForDownload()";
	bool b=false;
	try {
	  b = q.exec();
	}
	catch ( mysqlpp::BadQuery e) {
		BOOST_LOG_TRIVIAL(error) << "Optimizing failed: " << e.what();
	}
	catch (...) {
		BOOST_LOG_TRIVIAL(error) << "Optimizing failed with unknown exception";
	}
	if (b)
		eOptimizeStatus=opt_download;
	return b;
}

bool dbBTC::OptimizeForQuerying(BIRDDB_Query& q)
{
	if (eOptimizeStatus==opt_query)
		return true;
	boost::lock_guard<boost::mutex> lock(dbmutex);
	BOOST_LOG_TRIVIAL(info) << "Optimizing database for querying";
	q << "CALL OptimizeForQueries()";
	bool b=false;
	try {
	  b = q.exec();
	}
	catch ( mysqlpp::BadQuery e) {
		BOOST_LOG_TRIVIAL(error) << "Optimizing failed: " << e.what();
	}
	catch (...) {
		BOOST_LOG_TRIVIAL(error) << "Optimizing failed with unknown exception";
	}
	if (b)
		eOptimizeStatus=opt_query;
	return b;
}

int dbBTC::GetBlockDepth(void)
{
	return block_depth;
}

int dbBTC::GetBlockConfirmationEnd(void)
{
	return block_confirm_end;
}

void dbBTC::SetBlockConfirmationEnd(int ConfirmedHeight)
{
	block_confirm_end = ConfirmedHeight;
}

bool dbBTC::PruneInsideBlock(BIRDDB_Query& q, BIRDDB::sql_int_unsigned blockID)
{
	BOOST_LOG_TRIVIAL(trace) << "Pruning inside block "<< blockID;
	q << "CALL PruneTxInsideBlock(" << blockID << ")";
	bool b=false;
	try {
	  b = q.exec();
	}
	catch ( mysqlpp::BadQuery e) {
		BOOST_LOG_TRIVIAL(error) << "Calling PruneTxInsideBlock failed: " << e.what();
		return false;
	}
	catch (...) {
		BOOST_LOG_TRIVIAL(error) << "PruneInsideBlock failed with unknown exception";
		return false;
	}
	return b;
}

// Code optimized query to find all spent outputs in temporary blocks between the two heights
// Execution time should be N * log N, instead of N * N using standard SQL
bool dbBTC::PruneMultiBlock(BIRDDB_ConnectionPtr my_conn, int HeightStart, int HeightEnd)
{
	if (HeightStart+1 >= HeightEnd)
		return true;							// nothing to do
	if (block_multiprune_end == HeightEnd) {	// previous multiprune up to same height ?
		dwSleepProcessBlock=0;					//   yes -> nothing to do -> reset throttling
		return true;
	}

	block_multiprune_start=HeightStart;			// inform other threads of our multi pruning attempt
	block_multiprune_end=HeightEnd;

	BIRDDB_Query q = my_conn->query();

	// step 1: gather info about the outputs
	q << "SELECT cto.blockID, cto.txN, cto.txOutN, ct.txHash FROM ChainTxOuts cto INNER JOIN ChainBlocks cb ON cb.ID=cto.blockID";
	q << " INNER JOIN ChainTxs ct ON (ct.blockID=cto.blockID AND ct.txN=cto.txN) WHERE cb.height>=" << HeightStart;
	q << " AND cb.height<=" << HeightEnd << " LIMIT 50000";

	BOOST_LOG_TRIVIAL(info) << "PruneMultiBlock from " << HeightStart << " to " << HeightEnd;
	std::vector<MPOuts> vOut;
	vOut.reserve(50000);
	try {
		q.storein(vOut);		// store into vector container
	}
	catch (mysqlpp::BadQuery e) {
		BOOST_LOG_TRIVIAL(error) << "PruneMultiBlock step 1 failed: " << e.what();
		q.reset();
		return false;
	}

	BOOST_LOG_TRIVIAL(info) << "  PMB: " << vOut.size() << " outputs to analyze";

	// step 2: gather info about the inputs
	q << "SELECT cti.opHash, cti.opN, cti.blockID, cti.txN, cti.txInN FROM ChainTxIns cti INNER JOIN ChainBlocks cb ON cb.ID=cti.blockID";
	q << " WHERE cb.height>" << HeightStart << " AND cb.height<=" << HeightEnd << " LIMIT 50000";
	std::set<MPIns> sIn;
	std::set<MPIns>::iterator sInIt;
	sIn.clear();
	try {
		q.storein(sIn);			// store in a set for optimized search (log n)
	}
	catch (mysqlpp::BadQuery e) {
		BOOST_LOG_TRIVIAL(error) << "PruneMultiBlock step 2 failed: " << e.what();
		q.reset();
		return false;
	}
	if (vOut.size()>=40000 || sIn.size()>=40000) {
		dwSleepProcessBlock += 10;				// reduce processing of new blocks
		block_multiprune_end=HeightStart;		// select limit reached, unknown end --> set start so that different start height gives another multiprune attempt
	}
	else {
		if (dwSleepProcessBlock>=50)
			dwSleepProcessBlock -= 30;
		else
			dwSleepProcessBlock=0;
	}

	BOOST_LOG_TRIVIAL(info) << "  PMB: " << sIn.size() << " inputs to analyze, sleep time: " << dwSleepProcessBlock << " ms";

	int iFound=0;
	for (int i=0; i<vOut.size(); i++) {								// iterate over all outputs
		sInIt = sIn.find(MPIns(vOut[i].txHash, vOut[i].txOutN));	// look for an input that uses this output
		if (sInIt != sIn.end()) {									// found it !
			try {
				mysqlpp::Transaction tr(*my_conn);
				// do deletes
				q << "DELETE FROM ChainTxOuts WHERE blockID=" << vOut[i].blockID << " AND txN=" << vOut[i].txN << " AND txOutN=" << vOut[i].txOutN << " LIMIT 1";
				if (q.exec()) {
					q << "DELETE FROM ChainTxIns WHERE blockID=" << sInIt->blockID << " AND txN=" << sInIt->txN << " AND txInN=" << sInIt->txInN << " LIMIT 1";
					q.exec();
				}
				tr.commit();
				sIn.erase(sInIt);										// remove input from our set
				iFound++;
			}
			catch(mysqlpp::BadQuery e) {
				BOOST_LOG_TRIVIAL(error) << "  PMB delete error: " << e.what();
			}
		}
	}
	BOOST_LOG_TRIVIAL(info) << "  PMB: pruned " << iFound << " entries";
	block_multiprune_start=-1;											// multi pruning ended
	return true;
}

bool dbBTC::PruneMultiBlockParallel(int HeightStart, int HeightEnd)
{
	if (HeightStart+1 >= HeightEnd)
		return true;							// nothing to do
	if (block_multiprune_end == HeightEnd) {	// previous multiprune up to same height ?
		dwSleepProcessBlock=0;					//   yes -> nothing to do -> reset throttling
		return true;
	}

	block_multiprune_start=HeightStart;			// inform other threads of our multi pruning attempt
	block_multiprune_end=HeightEnd;
	BOOST_LOG_TRIVIAL(info) << "PruneMultiBlock from " << HeightStart << " to " << HeightEnd;

	BIRDDB_ConnectionPtr mc1 = GrabConnection();	// parallel select queries to database, needs 2 simultaneous connections
	if (mc1==NULL)
		return false;
	BIRDDB_ConnectionPtr mc2 = GrabConnection();
	if (mc2==NULL) {
		ReleaseConnection(mc1);
		return false;
	}

	BIRDDB_Query q1 = mc1->query();
	// step 1a: gather info about the outputs
	q1 << "SELECT cto.blockID, cto.txN, cto.txOutN, ct.txHash FROM ChainTxOuts cto INNER JOIN ChainBlocks cb ON cb.ID=cto.blockID";
	q1 << " INNER JOIN ChainTxs ct ON (ct.blockID=cto.blockID AND ct.txN=cto.txN) WHERE cb.height>=" << HeightStart;
	q1 << " AND cb.height<=" << HeightEnd << " LIMIT 100000";
	BIRDDB_Query q2 = mc2->query();
	// step 1b: gather info about the inputs
	q2 << "SELECT cti.opHash, cti.opN, cti.blockID, cti.txN, cti.txInN FROM ChainTxIns cti INNER JOIN ChainBlocks cb ON cb.ID=cti.blockID";
	q2 << " WHERE cb.height>" << HeightStart << " AND cb.height<=" << HeightEnd << " LIMIT 100000";
	std::vector<MPOuts> sqrOut;
	sqrOut.reserve(100000);
	std::vector<MPIns> sqrIn;
	sqrIn.reserve(100000);
	BOOST_LOG_TRIVIAL(info) << "  PMB: setup done";
	try {
	concurrency::parallel_invoke(
		[&] { q1.storein(sqrOut); BOOST_LOG_TRIVIAL(info) << "  PMB: sqrOut done";},
		[&] { q2.storein(sqrIn); BOOST_LOG_TRIVIAL(info) << "  PMB: sqrIn done"; }
		);
	}
	catch (mysqlpp::BadQuery e) {
		BOOST_LOG_TRIVIAL(error) << "PruneMultiBlock step 1 failed: " << e.what();
		ReleaseConnection(mc2);
		ReleaseConnection(mc1);
		return false;
	}
	ReleaseConnection(mc2);
	ReleaseConnection(mc1);

	BOOST_LOG_TRIVIAL(info) << "  PMB: " << sqrOut.size() << " outputs and " << sqrIn.size() << " inputs to analyze";
	// do throttling of incoming new blocks if necessary
	if (sqrOut.size()>=80000 || sqrIn.size()>=80000) {
		dwSleepProcessBlock += 10;				// reduce processing of new blocks
	}
	else {
		if (dwSleepProcessBlock>=50)
			dwSleepProcessBlock -= 30;
		else
			dwSleepProcessBlock=0;
	}
	if (sqrOut.size()>=99900 || sqrIn.size()>=99900)
		block_multiprune_end=HeightStart;		// select limit reached, unknown end --> set start so that different start height gives another multiprune attempt

	BOOST_LOG_TRIVIAL(info) << "  PMB: sleep time: " << dwSleepProcessBlock << " ms";

	// step 2: sort the inputs to allow much faster finds
	concurrency::parallel_sort(begin(sqrIn), end(sqrIn));
	BOOST_LOG_TRIVIAL(info) << "  PMB: inputs sorted";

	// step 3: iterate over all outputs to see if we can find it as an input, and if so, both the output and the input can be 'pruned'
	unsigned int iFound=0;
	concurrency::parallel_for_each(begin(sqrOut), end(sqrOut), [&](MPOuts mpout) {
		MPIns MPInKey = MPIns(mpout.txHash, mpout.txOutN);
		std::vector<MPIns>::iterator sqrInIt = std::lower_bound(begin(sqrIn), end(sqrIn), MPInKey);
		if (sqrInIt!=end(sqrIn) && sqrInIt->opN == MPInKey.opN && sqrInIt->opHash == MPInKey.opHash) {  // found a matching in- and output !
			mysqlpp::Connection::thread_start();
			try {
				BIRDDB_ConnectionPtr mc1 = GrabConnection();
				if (mc1!=NULL) {
					mysqlpp::Transaction tr(*mc1);
					BIRDDB_Query q = mc1->query();
					q << "DELETE FROM ChainTxOuts WHERE blockID=" << mpout.blockID << " AND txN=" << mpout.txN << " AND txOutN=" << mpout.txOutN << " LIMIT 1";
					if (q.exec()) {
						q << "DELETE FROM ChainTxIns WHERE blockID=" << sqrInIt->blockID << " AND txN=" << sqrInIt->txN << " AND txInN=" << sqrInIt->txInN << " LIMIT 1";
						q.exec();
					}
					tr.commit();
					ReleaseConnection(mc1);
					InterlockedIncrement(&iFound);
				}
			}
			catch(mysqlpp::BadQuery e) {
				BOOST_LOG_TRIVIAL(error) << "  PMB delete error: " << e.what();
				if (mc1!=NULL)
					ReleaseConnection(mc1);
			}
			mysqlpp::Connection::thread_end();
		}

	});

	BOOST_LOG_TRIVIAL(info) << "  PMB: pruned " << iFound << " entries";
	block_multiprune_start=-1;											// multi pruning ended
	return true;
}

#if (REUSETABLES==0)
void dbBTC::CheckQueueBTCID(BIRDDB_Query& q, unsigned int uiMinSize, unsigned int uiMaxSize)
{
	if (uiMinSize>uiMaxSize)
		uiMaxSize=uiMinSize;
	// see if enough IDs are in our cq
	unsigned int uicqsize=(unsigned int)cqReuseableBTCID.unsafe_size();
	if (uiMinSize > uicqsize) {	// more IDs needed
		q << "UPDATE pkCounters SET pkCount=LAST_INSERT_ID(pkCount+" << (uiMaxSize-uicqsize) << ") WHERE ID=1";	// get next possible IDs
		if (q.exec()) {
			unsigned int iNrpk = (unsigned int)q.insert_id();
			for (unsigned int i=iNrpk-uiMaxSize+uicqsize+1; i<=iNrpk; i++)
				cqReuseableBTCID.push(i);
		}
	}
}

void dbBTC::CheckQueueTxOut(BIRDDB_Query& q, unsigned int uiMinSize, unsigned int uiMaxSize)
{
	if (uiMinSize>uiMaxSize)
		uiMaxSize=uiMinSize;
	// see if enough IDs are in our cq
	unsigned int uicqsize=(unsigned int)cqReuseableTxOut.unsafe_size();
	if (uiMinSize > uicqsize) {	// more IDs needed
		q << "UPDATE pkCounters SET pkCount=LAST_INSERT_ID(pkCount+" << (uiMaxSize-uicqsize) << ") WHERE ID=2";	// get next possible IDs
		if (q.exec()) {
			unsigned int iNrpk = (unsigned int)q.insert_id();
			for (unsigned int i=iNrpk-uiMaxSize+uicqsize+1; i<=iNrpk; i++)
				cqReuseableTxOut.push(i);
		}
	}
}
#endif