// Copyright (c) 2012 Thilo von Braun
// Distributed under the EUPL v1.1 software license, see the accompanying
// file license.txt or http://www.osor.eu/eupl/european-union-public-licence-eupl-v.1.1

// Implementation of our database class for easy actual coding

#include "StdAfx.h"
#include "bird.h"
#include "bird_TCP.h"
#include "dbBTC.h"
#include <CommCtrl.h>

extern BTCtcp BTCnode;

dbBTC::dbBTC(void)
{	
	cpBTC = NULL;
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
	// TODO: get databasename, username and password from a config file
	TCHAR sTLoad[MAX_LOADSTRING];	// temporary TCHAR load string
	char sDB[MAX_LOADSTRING*2];		// database name
	char sUSER[MAX_LOADSTRING*2];	// user name
	char sPW[MAX_LOADSTRING*2];		// password
	GetPrivateProfileString(L"mysql", L"db", L"bird", sTLoad, MAX_LOADSTRING, szINIFile);
	WideCharToMultiByte(CP_ACP, 0, sTLoad, -1, sDB, MAX_LOADSTRING*2, NULL, NULL);
	GetPrivateProfileString(L"mysql", L"user", L"bird", sTLoad, MAX_LOADSTRING, szINIFile);
	WideCharToMultiByte(CP_ACP, 0, sTLoad, -1, sUSER, MAX_LOADSTRING*2, NULL, NULL);
	GetPrivateProfileString(L"mysql", L"password", L"btc", sTLoad, MAX_LOADSTRING, szINIFile);
	WideCharToMultiByte(CP_ACP, 0, sTLoad, -1, sPW, MAX_LOADSTRING*2, NULL, NULL);
	cpBTC = new ConnectionPoolBTC(sDB, dbserver.c_str(), sUSER, sPW);  // new connection pool to our BiRD database

	bool bRet=true;

#if (REUSETABLES==0)
	cqReuseableBTCID.clear();
	cqReuseableTxOut.clear();
	bRet = FillIDs("BitcoinAddress", cqReuseableBTCID) && FillIDs("TxOutAvailable", cqReuseableTxOut);
#endif
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
}

mysqlpp::Connection * dbBTC::GrabConnection(void)
{
	mysqlpp::Connection* pc;
	if (cpBTC!=NULL) {
		try {
			pc = cpBTC->grab();		// try to grab a connection
		}
		catch (mysqlpp::ConnectionFailed cf) {
			TCHAR sTCaption[MAX_LOADSTRING];
			TCHAR sTError[MAX_LOADSTRING];
			LoadString(hInst, IDS_Err_Caption, sTCaption, MAX_LOADSTRING);
			MultiByteToWideChar(CP_ACP, NULL, cf.what(), -1, sTError, MAX_LOADSTRING);
			MessageBox(hWndMain, sTError, sTCaption, MB_OK|MB_ICONEXCLAMATION);
			pc = NULL;										// illegal connection
		}
		return pc;
	}
	else
		return NULL;
}

void dbBTC::ReleaseConnection(mysqlpp::Connection *c)
{
	if (cpBTC!=NULL)
		cpBTC->release(c);
}

/* void dbBTC::StartTransaction(bool bConsistent)
{
	if (bTransactionStarted)		// currently a transaction is running !
		RollbackTransaction();
	mysqlpp::Query q(mySQL_con.query("START TRANSACTION"));
	if (bConsistent) {
		q << " WITH CONSISTENT SNAPSHOT";
	}
	q.execute();

	// Setup succeeded, so mark our transaction as started.
	bTransactionStarted = true;
}

void dbBTC::CommitTransaction(void)
{
	mySQL_con.query("COMMIT").execute();
	bTransactionStarted = false;
}

void dbBTC::RollbackTransaction(void)
{
	mySQL_con.query("ROLLBACK").execute();
	bTransactionStarted = false;
}
*/
unsigned int dbBTC::GetBitcoinAddressID(mysqlpp::Connection* my_conn, uint160 *uiHash, BOOL bAdd)
{
	if (my_conn == NULL)
		return 0;						// we are not connected, so we can't get an ID
	// get uint160 as an unformatted binary string
	std::string sHash;		
	sHash.assign(uiHash->begin(),uiHash->end());
	// make select query to retrieve ID
	mysqlpp::Query q = my_conn->query("SELECT ID FROM BitcoinAddress WHERE hash160=");
	q << mysqlpp::quote << sHash;
	mysqlpp::StoreQueryResult qres = q.store();
	if (qres.empty()) {								// unknown bitcoin address
		if (bAdd) 									// we should add it to the database
			return AddBitcoinAddress(my_conn, uiHash, "");
		else
			return 0;								// not found, and we shouldn't add it -> return "not found"
	}
	else
		return qres[0][0];							// return found ID which is first row, first column of query
	return 0;										// if we get here, something did not work as expected, return "not found"
}

unsigned int dbBTC::GetBitcoinAddressID(mysqlpp::Connection *my_conn, const string &sBase58, BOOL bAdd)
{
	if (my_conn == NULL)
		return 0;						// we are not connected, so we can't get an ID
	mysqlpp::Query q = my_conn->query("SELECT ID FROM BitcoinAddress WHERE base58=");
	q << mysqlpp::quote << sBase58;
	mysqlpp::StoreQueryResult qres = q.store();
	if (qres.empty()) {								// unknown bitcoin address
		if (bAdd) 									// we should add it to the database
			return AddBitcoinAddress(my_conn, NULL, sBase58);
		else
			return 0;								// not found, and we shouldn't add it -> return "not found"
	}
	else
		return qres[0][0];							// return found ID which is first row, first column of query
	return 0;										// if we get here, something did not work as expected, return "not found"
}

unsigned int dbBTC::AddBitcoinAddress(mysqlpp::Connection *my_conn, uint160 *uiHash, const string &sBase58)
{
	if (my_conn == NULL)
		return 0;			// unable to get a connection
	unsigned int ibtcAddr;
	mysqlpp::StoreQueryResult qres;
	mysqlpp::Query q = my_conn->query();

#if REUSETABLES
	q << "SELECT ID FROM pkReuseBitcoinAddress LIMIT 1";		// see if some IDs are reuseable
	qres = q.store();
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
		q << "SELECT ID, pkCount FROM pkCounters WHERE ID=1";		// get next possible ID
		qres = q.store();
		if (!qres.empty()) {							// get counter and increment it in table
			pkCounters pkBA = qres[0];					// load result
			ibtcAddr=pkBA.pkCount;						// save it for later use
			pkCounters pkBA_orig = pkBA;				// save original row
			pkBA.pkCount++;								// increment counter
			q.update(pkBA_orig, pkBA);					// make update query
			q.execute();
		}
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
		q.execute();
	}
	return ibtcAddr;
}

bool dbBTC::DeleteBitcoinAddress(mysqlpp::Connection *my_conn, const unsigned int btcID)
{
	if (my_conn == NULL)
		return false;			// unable to get a connection
	mysqlpp::Query q = my_conn->query("DELETE FROM BitcoinAddress WHERE ID=");
	q << btcID;
	if (q.exec()) {		// deleted, add it to reuse table
#if REUSETABLES
		q << "INSERT INTO pkReuseBitcoinAddress VALUES (" << btcID <<")";
		return q.exec();
#else
		cqReuseableBTCID.push(btcID);
		return true;
#endif
	}
	else
	 return false;
}

bool dbBTC::DeleteBitcoinAddressCheck(mysqlpp::Connection *my_conn, const unsigned int btcID)
{
	if (my_conn == NULL)
		return false;			// unable to get a connection
	// see if any transactions exist for this bitcoin ID
	mysqlpp::Query q = my_conn->query("SELECT ID FROM TxOutAvailable WHERE smartbtcaddr=");
	q << btcID << " OR storebtcaddr=" << btcID << " LIMIT 1";
	mysqlpp::StoreQueryResult qr;
	bool bRet = (qr = q.store());
	if (bRet) {	// query succeeded
		if (qr.num_rows()==0)						// no rows returned
			return DeleteBitcoinAddress(my_conn, btcID);
		else
			return true;							// queries OK, but we can't delete ID as we still have tx's
	}
	return false;									// if we get here, something is wrong
}

unsigned int dbBTC::AddTxOut(mysqlpp::Connection *my_conn, int iBlockHeight, ChainTxOuts &TxOutToAdd, mysqlpp::sql_blob &TxHash)
{
	if (my_conn == NULL)
		return 0;			// unable to get a connection
	unsigned int iTxOut;			// our new transaction ID
	// wrap everything in a transaction so tables are updated consistently
	// mysqlpp::Transaction sqlTrans(mySQL_con);
	mysqlpp::StoreQueryResult qres;
	mysqlpp::Query q = my_conn->query();

#if REUSETABLES		
	q << "SELECT ID FROM pkReuseTxOut LIMIT 1";		// see if some IDs are reuseable
	qres = q.store();
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
		q << "SELECT ID, pkCount FROM pkCounters WHERE ID=2";	// get next possible ID
		qres = q.store();
		if (!qres.empty()) {					// get counter and increment it in table
			pkCounters pkTX = qres[0];			// load result
			iTxOut=pkTX.pkCount;				// save it for later use
			pkCounters pkTX_orig = pkTX;		// save original row
			pkTX.pkCount++;						// increment counter
			q.update(pkTX_orig, pkTX);			// make update query
			q.execute();
		}
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
		row.smartbtcaddr=GetBitcoinAddressID(my_conn, &ui, true);
		memcpy(ui.begin(), TxOutToAdd.storeID.data(), 20);
		if (ui == 0)	// see if all zeroes was stored in database, if yes then store NULL now
			row.storebtcaddr = mysqlpp::null;
		else 
			row.storebtcaddr=GetBitcoinAddressID(my_conn, &ui, true);
		q.insert(row);
		try {
		  q.execute();
		}
		catch (const mysqlpp::BadQuery& er) {	// Handle any query errors
			//System::String^ sBlock;
			//sBlock +=iBlockHeight;
			//System::String^ sError = gcnew System::String(er.what());
			//smartBTCGlobals::traceLog->WriteLine(System::String::Format("Query error in AddTxOut for block {0}: {1}", sBlock, sError));
	    }
		catch (const mysqlpp::BadConversion& er) {  
			// Handle bad conversions
			//System::String^ sBlock;
			//sBlock +=iBlockHeight;
			//System::String^ sError = gcnew System::String(er.what());
			//smartBTCGlobals::traceLog->WriteLine(System::String::Format("Conversion error in AddTxOut for block {0}: {1}", sBlock, sError));
	    }
		catch (const mysqlpp::Exception& er) {
			// Catch-all for any other MySQL++ exceptions
			//System::String^ sBlock;
			//sBlock +=iBlockHeight;
			//System::String^ sError = gcnew System::String(er.what());
			//smartBTCGlobals::traceLog->WriteLine(System::String::Format("Other error in AddTxOut for block {0}: {1}", sBlock, sError));
		}

	}
		// sqlTrans.commit();
	return iTxOut;
}

bool dbBTC::DeleteTxOut(mysqlpp::Connection *my_conn, mysqlpp::sql_blob& txHash, mysqlpp::sql_int& txIndex)
{
	if (my_conn == NULL)
		return false;			// unable to get a connection
	unsigned int uiTxOut=0;
	unsigned int uiSmartBTCaddr=0;
	// get ID for the transaction to delete

	mysqlpp::Query q = my_conn->query("SELECT ID, smartbtcaddr FROM TxOutAvailable WHERE hash=");
	q << mysqlpp::quote << txHash << " AND txindex=" << txIndex;
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
		return DeleteBitcoinAddressCheck(my_conn, uiSmartBTCaddr);
	}
	return false;
}

int dbBTC::GetBestHeightKnown(mysqlpp::Connection *my_conn)
{
	if (my_conn == NULL)
		return -3;			// unable to get a connection
	mysqlpp::Query q = my_conn->query("SELECT MAX(height) FROM ChainBlocks");		// get highest found height in temp blocks
	mysqlpp::StoreQueryResult qres = q.store();
	if (qres.num_rows()==0)			// something wrong with query
		return GetSafeHeight(my_conn);
	if (qres[0][0] == mysqlpp::null)
		return GetSafeHeight(my_conn);		// only if no rows in ChainBlock, we get a NULL value as return
	else
		return qres[0][0];
}

// GetSafeHeight : returns the block height that has been fully processed (and considered safe)
//    Returns: >0   block height as stored in pkCounters, not verified if block in ChainBlocks (it should be)
//			    0	block height is 0 as indicated in pkCounters: no blocks yet processed
//			   -2	no ID=4 found in pkCounters or no ChainBlocks table, database needs to be checked !
//			   -3   no active database connection
int dbBTC::GetSafeHeight(mysqlpp::Connection *my_conn)
{
	if (my_conn == NULL)
		return -3;			// unable to get a connection
	mysqlpp::Query q = my_conn->query("SELECT ID, pkCount FROM pkCounters WHERE ID=4");		// ID=4 stores processed block
	mysqlpp::StoreQueryResult qres = q.store();
	if (qres.empty())
		return -2;		// it should never be empty --> TODO: error handling
	else {
		unsigned int i=qres[0][1];		// block height saved in pkCounters table
		return i;						// return pkCount value, should be automatically in ChainBlocks if i>0
	}
}

bool dbBTC::IsBlockHashKnown(mysqlpp::Connection *my_conn, uint256 *bHash)
{
	if (my_conn == NULL)
		return false;			// connection is not OK
	std::string sHash;		
	sHash.assign(bHash->begin(),bHash->end());
	mysqlpp::Query q = my_conn->query("SELECT ID FROM ChainBlocks WHERE hash=");
	q << mysqlpp::quote << sHash;
	mysqlpp::StoreQueryResult qres = q.store();
	return (!qres.empty());
}

bool dbBTC::IsTxHashKnown(mysqlpp::Connection *my_conn, uint256 *bHash)
{
	if (my_conn == NULL)
		return false;			// connection is not OK
	mysqlpp::sql_blob sHash(reinterpret_cast<char *>(bHash->begin()),sizeof(uint256));
	mysqlpp::Query q = my_conn->query("SELECT ID FROM TxUnconfirmed WHERE hash=");
	q << mysqlpp::quote << sHash;
	mysqlpp::StoreQueryResult qres = q.store();
	return (!qres.empty());
}

// get the hash out of ChainBlocks for a certain block height
// currently only looks in temporary stored blocks in ChainBlocks table
// returns 0 if not found
bool dbBTC::GetBlockHashFromHeight(mysqlpp::Connection *my_conn, int iHeight, uint256& ui)
{
	if (my_conn == NULL) {
		ui = uint256(0);
		return false;			// unable to get a connection
	}
	mysqlpp::Query q = my_conn->query("SELECT ID, hash, height FROM ChainBlocks WHERE height=");
	q << iHeight;
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
// Returns: -4  : no database connection
//			-3  : hash not found
//			-2  : hash found, but height of block is unknown
//			>=0 : hash found and its height is known
// Function checks
//		1. ChainBlocks table which holds current unprocessed blocks to get height of it
//		2. if not found, checks if it is equal to hash of Genesis Block (first block we receive on first startup will contain prev hash = hash genesis block !)
int dbBTC::GetBlockHeightFromHash(mysqlpp::Connection *my_conn, mysqlpp::sql_blob &fromHash)
{
	if (my_conn == NULL)
		return -4;			// unable to get a connection
	int i,j;
	mysqlpp::Query q = my_conn->query("SELECT ID, height FROM ChainBlocks WHERE hash=");
	q  << mysqlpp::quote << fromHash;
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
void dbBTC::DeleteBlockDataOfHeight(mysqlpp::Connection *my_conn, int iHeight)
{
	if (my_conn == NULL)
		return;			// unable to get a connection
	mysqlpp::Query q = my_conn->query("DELETE FROM ChainBlocks WHERE status<2 AND height=");	// include status check so that last confirmed block is never deleted !
	q << iHeight;
	q.exec();
}

// Adds a temporary block to ChainBlocks using the given data
// Returns in newBlock.ID the ID of the record just created
// Returns false if execute query fails, true otherwise
bool dbBTC::AddBlockToChain(mysqlpp::Connection *my_conn, ChainBlocks &newBlock)
{
	if (my_conn == NULL)
		return false;			// unable to get a connection
	mysqlpp::Transaction myTrans(*my_conn);
	mysqlpp::Query q = my_conn->query("SELECT ID, pkCount FROM pkCounters WHERE ID=3 FOR UPDATE");		// get next ID for block
	mysqlpp::StoreQueryResult qres = q.store();
	if (qres.empty()) {   // error in database, TODO: add error handling
		return false;
	}
	// get our block primary key and increment it in pkCounters table
	pkCounters pkBC = qres[0];						// load result
	newBlock.ID=pkBC.pkCount;						// save it for later use
	pkCounters pkBC_orig = pkBC;					// save original row
	pkBC.pkCount++;									// increment counter
	q.update(pkBC_orig, pkBC);						// make update query
	q.execute();
	// construct record to add
	q.insert(newBlock);
	bool bResult = q.exec();
	myTrans.commit();
	return bResult;
}

// Insert a new (incomplete) record into ChainTxs
bool dbBTC::InsertChainTx(mysqlpp::Connection *my_conn, ChainTxs &newTx)
{
	mysqlpp::Query q = my_conn->query();	// create object
	q.insert(newTx);						// create insert query
	bool b;
	try {
		b = q.exec();
	}
	catch(mysqlpp::BadQuery e) {
		ShowError(IDS_Err_BadQuery, e.what());
	}
	return b;
}

// Updates transaction hash once we processed complete raw transaction from a message
bool dbBTC::UpdateChainTx(mysqlpp::Connection *my_conn, ChainTxs &updatedTx)
{
	mysqlpp::Query q = my_conn->query("UPDATE ");
	q << updatedTx.table() << " SET " << updatedTx.equal_list("", ChainTxs_txHash);
	q << " WHERE " << updatedTx.equal_list(" and ", true, true, false);
	bool b;
	try {
		b = q.exec();
	}
	catch(mysqlpp::BadQuery e) {
		ShowError(IDS_Err_BadQuery, e.what());
	}
	return b;
}

// Insert a new record into ChainTxIns
bool dbBTC::InsertChainTxIn(mysqlpp::Connection *my_conn, ChainTxIns &newTxIn)
{
	mysqlpp::Query q = my_conn->query();	// create object
	q.insert(newTxIn);						// create insert query
	bool b;
	try {
		b = q.exec();
	}
	catch(mysqlpp::BadQuery e) {
		ShowError(IDS_Err_BadQuery, e.what());
	}
	return b;
}

bool dbBTC::InsertChainTxOut(mysqlpp::Connection *my_conn, ChainTxOuts &newTxOut)
{
	mysqlpp::Query q = my_conn->query();	// create object
	q.insert(newTxOut);
	bool b;
	try {
		b = q.exec();
	}
	catch(mysqlpp::BadQuery e) {
		ShowError(IDS_Err_BadQuery, e.what());
	}
	return b;
}

// Insert a new record into TxUnconfirmed
bool dbBTC::InsertTxUnconfirmed(mysqlpp::Connection *my_conn, TxUnconfirmed &newTx)
{
	bool b;
	mysqlpp::Query q = my_conn->query();	// create object
	q.insert(newTx);						// create insert query
	try {
	  b = q.exec();
	}
	catch (mysqlpp::BadQuery e) {
		ShowError(IDS_Err_BadQuery, e.what());
	}
	if (b)
		newTx.ID = q.insert_id();
	return b;
}

// Insert a new record into TxInUnconfirmed
bool dbBTC::InsertTxInUnconfirmed(mysqlpp::Connection *my_conn, TxInUnconfirmed &newTx)
{
	mysqlpp::Query q = my_conn->query();	// create object
	q.insert(newTx);						// create insert query
	bool bRes = false;
	try {
		bRes = q.exec();
	}
	catch (mysqlpp::BadQuery e) {
		ShowError(IDS_Err_BadQuery, e.what());
	}
	return bRes;
}

// Set life to zero for the given unconfirmed transaction
void dbBTC::TxUnconfirmedDies(mysqlpp::Connection *my_conn, mysqlpp::sql_blob& txhash)
{
	// query: UPDATE TxUnconfirmed SET life=0 WHERE hash='txhash'
	mysqlpp::Query q = my_conn->query("UPDATE TxUnconfirmed SET life=0 WHERE hash=");
	q << mysqlpp::quote << txhash;
	q.exec();
}

// Decrease life and remove txs with life<0
void dbBTC::TxUnconfirmedAges(mysqlpp::Connection *my_conn)
{
	mysqlpp::Query q = my_conn->query("UPDATE TxUnconfirmed SET life=life-1");
	q.exec();
	q << "DELETE FROM TxUnconfirmed WHERE life<0";
	q.exec();
}

// Count the number of unconfirmed transactions
int dbBTC::NrTxUnconfirmed(mysqlpp::Connection *my_conn)
{
	mysqlpp::Query q = my_conn->query("SELECT COUNT(*) FROM TxUnconfirmed WHERE life>0");
	mysqlpp::StoreQueryResult qres = q.store();
	return qres[0][0];
}

bool dbBTC::CheckHeights(mysqlpp::Connection *my_conn)
{
	bool bFoundHeight = false;
	bool bFoundInFor;
	vector<ChainBlocks> vChain;
	int iChain, iHeight;
	mysqlpp::Query q = my_conn->query();
	do {
		q << "SELECT * FROM ChainBlocks WHERE status<1";	// get all blocks with unknown height
		q.storein(vChain);
		bFoundInFor=false;
		for (iChain=0; iChain!=vChain.size(); iChain++) {	// loop through all blocks with unknown height
			iHeight = GetBlockHeightFromHash(my_conn, vChain[iChain].prevhash);		// see if we have previous hash, and if so, what height is has
			if (iHeight>=-1) {	// found the previous hash, and it has a meaningfull height
				ChainBlocks newBlock = vChain[iChain];
				newBlock.height = iHeight+1;
				newBlock.status = 1;
				q.update(vChain[iChain], newBlock);	// make update query
				q.execute();					// and execute it
				bFoundHeight=true;				// notify calling procedure we could resolve some blocks
				bFoundInFor=true;				// force repeat of do-while
				vChain.clear();					// clear the vector for next storein !
				break;							// force exit of for loop as we got to start from the beginning again
			}
		}
	} while (bFoundInFor);			// repeat the whole thing until for loop looped through all blocks without finding anything usefull

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
		q << "UPDATE ChainBlocks SET height=height-1 WHERE status=0";
		q.execute();
		q << "DELETE FROM ChainBlocks WHERE height<-100";
		q.execute();
	}
	return bFoundHeight;
}

void dbBTC::ConfirmTempBlocks(mysqlpp::Connection *my_conn)
{
	if (my_conn==NULL)
		return;
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
		mysqlpp::Transaction myTrans(*my_conn);							// wrap 1 block inside a transaction
		q << "SELECT * FROM ChainTxs WHERE blockID=" << iBlockID;		// get all transactions in this block
		q.storein(vTxs);
		if (vTxs.size()==0) { // o-oh, can't find any related transactions
			ShowError(IDS_Err_MissingTxs, iSafeHeight);
			break;
		}
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
//						q << "DELETE FROM TxOutAvailable WHERE hash=" << mysqlpp::quote << chTxIn.opHash << " and txindex=" << chTxIn.opN;
//						qsimple = q.execute();		// delete the txout as it is now used
/*						if (qsimple.rows()<1) 		// delete did nothing !
							ShowError(IDS_Err_MissingTxOutAvailable);	// send a warning to user    --> TODO: ONLY IN STORE MODE, NOT PRIVATE MODE*/
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
	}
}

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

bool dbBTC::DeleteTable(mysqlpp::Query& q, const char *sTable, HWND hPBar, int iPos)
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
	mysqlpp::Connection *my_conn = this->GrabConnection();
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
	mysqlpp::Connection *my_conn = this->GrabConnection();
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

	mysqlpp::Query q = my_conn->query("(SELECT (a.id+1) AS id FROM ");
	q << szTable << " a WHERE NOT EXISTS (SELECT 1 FROM " << szTable << " b WHERE b.id=(a.id+1)) AND a.id NOT IN (SELECT MAX(c.id) FROM ";
	q << szTable << " c)) UNION ALL ( SELECT (a.id-1) AS id FROM ";
	q << szTable << " a WHERE NOT EXISTS (SELECT 1 FROM " << szTable << " b WHERE b.id=(a.id-1)) AND a.id NOT IN (SELECT MIN(c.id) FROM ";
	q << szTable << " c)) ORDER BY id";

	// query will return pairs of numbers: each pair = (start,stop) of missing range of items
	mysqlpp::StoreQueryResult qres;
	bool bRet;
	try {
		bRet= (qres = q.store());
		if (bRet) {		// query executed normally
			for (size_t i = 0; i < qres.num_rows(); i+=2) {		// loop through each pair of rows
				unsigned int k = qres[i+1][0];		// stop value
				for (unsigned int j = qres[i][0]; j <= k; j++)	// add from "start" to "stop" to our vector
					cq.push(j);
			}
		}
	}
	catch (const mysqlpp::BadQuery& e) {	// handle query errors
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
	mysqlpp::Connection *my_conn = this->GrabConnection();
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

