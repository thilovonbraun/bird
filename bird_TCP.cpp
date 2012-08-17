// Copyright (c) 2012 Thilo von Braun
// Distributed under the EUPL v1.1 software license, see the accompanying
// file license.txt or http://www.osor.eu/eupl/european-union-public-licence-eupl-v.1.1

#include "StdAfx.h"
#include "bird.h"
#include "bird_TCP.h"
#define MYSQLPP_SSQLS_NO_STATICS
#include "dbBTC.h"

#pragma comment(lib, "Ws2_32.lib")

extern dbBTC mydb;
extern BTCtcp BTCnode;
extern Concurrency::concurrent_queue<BTCMessage> cqInvMsg, cqBlockMsg, cqTxMsg;

uint256 hashGenesisBlock("0x000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f");

unsigned char pMessageStartStd[4]={0xF9, 0xBE, 0xB4, 0xD9};		// std network
unsigned char pMessageStartTst[4]={0xFA, 0xBF, 0xB5, 0xDA};		// testnet
std::string BTCMsgCommandsString="01 version 02 verack 03 addr 04 inv 05 getdata 06 getblocks 07 getheaders 08 tx 09 block 10 getaddr 11 checkorder 12 submitorder 13 reply 14 ping 15 alert ";

unsigned int iProtocolVersion=60000;  // protocol version 0.6.00
std::string BTCMsgUserAgent="BiRD:0.1(Windows32)/MySQL:5.5";


__int64 GetUnixTimeStamp()
{
//	System::TimeSpan ts = (dt - System::DateTime(1970,1,1,0,0,0,0));
//	return ts.TotalSeconds;
	return time(NULL);
}

uint64 GetVarInt(std::vector<unsigned char>::iterator it, short *s)
{
	uint64 ui;
	if (*it < 0xfd) {
		ui=(uint64)*it;
		*s=1;
	}
	else {
		unsigned char *p=&it[1];
		if (*it == 0xfd) {
			ui=(uint64)*(short *)p;
			*s=3;
		}
		else {
			if (*it == 0xfe) {
				ui=(uint64)*(int *)p;
				*s=5;
			}
			else {
				ui=*(uint64 *)p;
				*s=9;
			}
		}
	}
	return ui;
}

void BTCMessage::Init(bool bTestNetwork)
{
	if (bTestNetwork)
		memcpy(chStart, pMessageStartTst, sizeof(chStart));
	else
		memcpy(chStart, pMessageStartStd, sizeof(chStart));
	iMsgStatus=btcmsgstat_init;
	bPayloadChk=true;
	memset(chCommand,0,sizeof(chCommand));
	iPayloadLenRcvd=iPayloadLen=iPayloadChk=0;
	vPayload.clear();
}

BTCMessage::BTCMessage(bool bTestNetwork)
{
	Init(bTestNetwork);
}

BTCMessage::BTCMessage(const char *szCommand, bool bTestNetwork)
{
	Init(bTestNetwork);
	SetCommand(szCommand);
}

BTCMessage::BTCMessage(const enum BTCMsgCommands iCommand, bool bTestNetwork)
{
	Init(bTestNetwork);
	SetCommand(iCommand);
}

void BTCMessage::SetCommand(const char *szCommand)
{
	int iLen = strlen(szCommand);	// length of ascii string of command
	if (iLen>=sizeof(chCommand))
		iLen=sizeof(chCommand);		// can't be too long !
	else
		memset(chCommand+iLen, 0, sizeof(chCommand)-iLen);	// zero padding of remaing bytes, otherwise invalid
	memcpy(chCommand, szCommand, iLen);
	std::string strCommand(" ");		// space in front
	strCommand += szCommand;
	strCommand += ' ';					// space in back
	size_t p = BTCMsgCommandsString.find(strCommand);
	if (p==std::string::npos)  // command passed not found
		iCommand = btcmsg_unknown;
	else
		iCommand = (enum BTCMsgCommands) (BTCMsgCommandsString[p-1]-48+(BTCMsgCommandsString[p-2]-48)*10);
	iMsgStatus=btcmsgstat_command;
	SetPayloadChkStatus();
}

void BTCMessage::SetCommand(const enum BTCMsgCommands i)
{
	iCommand = i;
	std::string strI;
	strI.append(1,(i / 10)+48);
	strI.append(1,(i % 10)+48);
	size_t p1 = BTCMsgCommandsString.find(strI);
	if (p1==std::string::npos)
		memset(chCommand, 0, BTC_CommandLength);		// commandnumber not found, set command to all zero
	else {
		p1 +=3;			// skip the value + space
		int i=0;
		while (BTCMsgCommandsString[p1] != ' ')
			chCommand[i++]=BTCMsgCommandsString[p1++];	// copy string
		while (i<BTC_CommandLength)
			chCommand[i++]=0;							// zero padding
	}
	iMsgStatus=btcmsgstat_command;
	SetPayloadChkStatus();
}

void BTCMessage::SetPayloadLength(void *sBufferInt)
{
	iPayloadLen=*(int *)sBufferInt;				// SBuffer is in little endian, as is Intel CPU
}

void BTCMessage::SetPayloadChecksum(void *sBufferInt)
{
	iPayloadChk=*(int *)sBufferInt;				// SBuffer is in little endian, as is Intel CPU
}

void BTCMessage::SetPayloadChkStatus(void)
{
//	if (iCommand==btcmsg_version || iCommand==btcmsg_verack)
//		bPayloadChk = false;			// don't need checksum added to message
//	else
		bPayloadChk = true;
}

bool BTCMessage::PayloadChkStatus(void)
{
	return bPayloadChk;
}

void BTCMessage::AppendVarInt(uint64 i)
{
	unsigned char *p = (unsigned char *)&i;		// see integer as a sequence of chars
	if (i < 0xfd)
		vPayload.push_back(*p);					// small enough to get size in 1 byte
	else
		if (i <= 0xffff) {
			vPayload.push_back(0xfd);
			vPayload.push_back(*p++);
			vPayload.push_back(*p);
		}
		else
			if (i <= 0xffffffff) {
				vPayload.push_back(0xfe);
				vPayload.push_back(*p++);
				vPayload.push_back(*p++);
				vPayload.push_back(*p++);
				vPayload.push_back(*p);
			}
			else {
				vPayload.push_back(0xff);
				vPayload.insert(vPayload.end(), p, p+sizeof(uint64));
			}
}

// returns uint64 that was saved as a varint at the indicated iterator position, iterator updated to point after varint
uint64 BTCMessage::GetVarInt(std::vector<unsigned char>::iterator &it)
{
	unsigned char c = it[0];
	if (c < 0xfd) {	// value is small enough to fit 1 byte
		it++;
		return (uint64)c;
	}
	uint64 x;
	switch (c) {
	case 0xfd:	// 2 byte int
		x = (uint64)*((unsigned short *)&it[1]);
		it +=3;
		break;
	case 0xfe:  // 4 byte int
		x = (uint64)*((unsigned int *)&it[1]);
		it +=5;
		break;
	case 0xff:  // 8 byte int
		x = *(uint64 *)&it[1];
		it +=9;
		break;
	}
	return x;
}


void BTCMessage::AppendInvVector(int HashType, uint256 &ui)		// appends an inv vector to the payload
{
	// append the type of hash
	char *p = (char *)&HashType;
	vPayload.insert(vPayload.end(),p,p+sizeof(int));
	// now append the hash
	p = (char *)&ui;
	vPayload.insert(vPayload.end(),p,p+32);
}


bool BTCMessage::VerifyChecksum(void)
{
	uint256 h = Hash(vPayload.begin(),vPayload.end());
	return (memcmp(&h, &iPayloadChk, sizeof(iPayloadChk))==0);
}


BTCtcp::BTCtcp(void)			// constructor of our class
{
	nonce = (unsigned __int64)rand() << 4;		// some random nonce we get to check self connect
	skBTC = INVALID_SOCKET;		// not yet initialized our socket
	iBTCError=ERROR_SUCCESS;	// no error yet
	iBTCSocketStatus=0;			// nothing done yet
	iBTCNodeStatus=0;
    Peer_BlockHeight=0;											// block height known to other peer (from incoming version msg)
    Peer_AskedBlockHeight=0;									// highest block height asked to other peer
    iTimerPreviousBlockHeight=0;								// remember blockheight of previous timer event
	bPingTimerSet=false;
	bWriteReady=false;
}

BTCtcp::~BTCtcp(void)
{
	if (iBTCSocketStatus>=2 && skBTC!=INVALID_SOCKET)
		closesocket(skBTC);
	if (iBTCSocketStatus>=1)
		WSACleanup();
}

bool BTCtcp::ConnectToHost(int PortNo, const char *szIPAddress)
{
	return ConnectToHost(PortNo, inet_addr(szIPAddress));
}

bool BTCtcp::ConnectToHost(int PortNo, ULONG addr)
{
	char szHostName[255];
	if (iBTCSocketStatus == 0 ) {
	  if (WSAStartup(WINSOCK_VERSION, &wsaData)) return false;    // Couldn't initialize WSA
	  iBTCSocketStatus++;
	}
	if (iBTCSocketStatus == 1 ) {
	  if (wsaData.wVersion != WINSOCK_VERSION) {
		 WSACleanup();											// did not get expected version of WSA
		 iBTCSocketStatus--;
		 return false;
	  }
	}
	if (iBTCSocketStatus == 1) {
	  skBTC = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);			// TCP stream socket
	  if (skBTC == INVALID_SOCKET) {
		  iBTCError = WSAGetLastError();
		  WSACleanup();											// socket could not be created
		  iBTCSocketStatus--;
		  return false;
	  }
	  iBTCSocketStatus++;
	}
	if (iBTCSocketStatus == 2) {  // setup notification messages so we get a non-blocking socket
	    if (WSAAsyncSelect(skBTC, hWndMain, myTCP_Messages, FD_READ|FD_WRITE|FD_ACCEPT|FD_CONNECT|FD_CLOSE) == SOCKET_ERROR) {
		    iBTCError = WSAGetLastError();
			closesocket(skBTC);
		    WSACleanup();
		    iBTCSocketStatus=0;
		    return false;
		}
		iBTCSocketStatus++;
	}
	if (iBTCSocketStatus == 3) {
		dest.sin_addr.s_addr = addr;
		dest.sin_family = AF_INET;
		dest.sin_port = htons(PortNo);
		if (connect(skBTC, (const sockaddr *)&dest, sizeof(dest)) == SOCKET_ERROR) {
			iBTCError = WSAGetLastError();
			if (iBTCError == WSAEWOULDBLOCK) {	// this is normal, non blocking socket !
				iBTCError = ERROR_SUCCESS;
				iBTCSocketStatus++;
			}
		}
		else
		  iBTCSocketStatus++;	// no error probably means everything is ok
	}
	// get IP address for later use in version message
	gethostname(szHostName,255);
	struct hostent *host_entry;
	host_entry=gethostbyname(szHostName);
	myIPAddress.s_addr = *(ULONG *)host_entry->h_addr_list[0];
	return (iBTCSocketStatus>3);
}

bool BTCtcp::Disconnect()
{
	if (this->iBTCSocketStatus>=2)
		closesocket(skBTC);
	WSACleanup();
	this->iBTCSocketStatus=0;
	this->iBTCError=ERROR_SUCCESS;
	this->iBTCNodeStatus=0;			// as we disconnect, BTC node status needs reset too
	return true;
}

LRESULT BTCtcp::ProcessNotifications(HWND hWnd, LPARAM lParam)
{
	int wsaErr = WSAGETSELECTERROR(lParam);
	unsigned int ui=0;
	TCHAR szText[MAX_LOADSTRING];
	switch (WSAGETSELECTEVENT(lParam))
	{
	case FD_ACCEPT:
		break;
	case FD_CONNECT:						// connect() operation terminated
		if (wsaErr == ERROR_SUCCESS) {		// and no error --> we are connected :)
			iBTCSocketStatus = 5;
			iBTCError = ERROR_SUCCESS;
			ui=IDS_SS_CONNECTED;
		}
		else {
			iBTCSocketStatus = 3;			// "connecting..." status did not complete successfully
			iBTCError = wsaErr;				// save error information
			iBTCNodeStatus=0;				// no connection
			ui=IDS_SS_FAILED;
			ShowError(IDS_Err_Socket, iBTCError);
		}
		break;
	case FD_WRITE:
		bWriteReady = true;		// we are notified we can send (again)
		WriteToSocket();		// try to send data if something is waiting in our buffer
		break;
	case FD_READ:
		ReadFromSocket(hWnd);	// data is awaiting
		break;
	case FD_CLOSE:				// connection has been closed
		closesocket(skBTC);		// release the socket
		iBTCSocketStatus=1;
		iBTCNodeStatus=0;
		ui=IDS_SS_NOTHING;
		break;
	}
	if (ui) {	// show something to user
		LoadString(hInst, ui, szText, MAX_LOADSTRING);
		SetWindowText(hStaticSocketStatus, szText);  // update status window related to our TCP connection
		InvalidateRect(hWnd, &rcSocketStatus, true);
		LoadString(hInst, (iBTCSocketStatus>3) ? IDS_DISCONNECT:IDS_CONNECT, szText, MAX_LOADSTRING);
		SetDlgItemText(hWnd, IDC_BTNBTCCONNECT, szText);
		EnableWindow(GetDlgItem(hWnd, IDC_BTNSQLCONNECT), iBTCSocketStatus<2);
	}
	return 0;
}

void BTCtcp::AppendOutBuffer(std::vector<char>* tBuf)
{
	outBuffer.insert(BTCnode.outBuffer.end(), tBuf->begin(), tBuf->end());
	delete tBuf;		// no longer need the temporary buffer coming from the thread
}

void BTCtcp::WriteToSocket(void)
{
	if (outBuffer.empty())
		return;							// empty outBuffer, we're done
	if (outPos == outBuffer.size()) {	// nothing to send, but OutBuffer is not yet emptied
		outBuffer.clear();			
		outPos = 0;
		return;
	}
	if (bWriteReady) {	// still ok to send something
		int iLen = outBuffer.size() - outPos;								// how much data to send
		if (iLen>0) {
			int iSent = send(skBTC, &outBuffer[outPos], iLen,0);			// try to send it
			if (iSent == SOCKET_ERROR) {				// could not send anything
				iBTCError = WSAGetLastError();
				if (iBTCError == WSAEWOULDBLOCK) 		// send would block, so wait for notify message to try again
					bWriteReady = false;
					// TODO: further error handling !
			}
			else {	// something was sent
				iBTCError = ERROR_SUCCESS;
				if (iLen == iSent) { // everything was sent, yay :)
					outBuffer.clear();
					outPos = 0;
				}
				else {	// partial send, only update outPos
					outPos += iSent;
					bWriteReady = false;	// wait for next notification to write again
				}
			}
		}
	}
}

// BTCtcp::ReadFromSocket
//    Try to read data from the socket, normally called in response to notification message that data is waiting
void BTCtcp::ReadFromSocket(HWND hWnd) {
	if (inPos>0)	{	// we have some data in front that already has been processed
		inBuffer.erase(inBuffer.begin(),inBuffer.begin()+inPos);		// now is a good time to remove it
		inPos=0;														// everything from the beginning is unprocessed
	}
	char sBuffer[socketMaxReadLength];
	int iRead = recv(skBTC, sBuffer, sizeof(sBuffer), 0);				// read first in normal buffer
	if (iRead != SOCKET_ERROR) {
		inBuffer.insert(inBuffer.end(), sBuffer, sBuffer+iRead);		// add it to our inBuffer vector at the end
		PostMessage(hWnd, myMsg_ProcessReadBuffer, 0, 0);				// new data arrived, process it in message loop
	}
}

void BTCtcp::ProcessReadBuffer(void)
{
	bool bFound;
	size_t iBufSize=inBuffer.size();
	if (inPos >= iBufSize)
		return;							// nothing to process
	switch (msgIn.iMsgStatus)			// see what we already received in message
	{
	case btcmsgstat_init:	// nothing received yet, see if we find header somewhere
		bFound=false;	// our flag when searching for the 4 magic bytes
		while (inPos < iBufSize)
		{
			if (inBuffer[inPos++] == msgIn.chStart[0]) {					// matching first char
				if (inPos+3<=iBufSize) {										// enough chars in buffer
				    if (memcmp(&inBuffer[inPos],&msgIn.chStart[1],3) == 0) {	// 3 following chars match too
						bFound=true;
						inPos +=3;
						break;			// exit loop as we found it
					}
					// no match in next 3 chars, continue search
				}
				else { // possible match, but near end of buffer --> erase processed buffer and wait for next read/processing
					inBuffer.erase(inBuffer.begin(),inBuffer.begin()+inPos-1);   // -1 because matching char must be retained
					inPos=0;
					break;				// exit loop as we are near end of buffer with possible match
				}
			}
		}
		if (bFound)
			msgIn.iMsgStatus=btcmsgstat_header;			// update MsgStatsu as we found header, and continue in switch with next case
		else
			break;										// not found, stay at same lvl and exit switch

	case btcmsgstat_header:		// header found, so next 12 chars should be command
		if (inPos+12 <= iBufSize) {					// enough chars in buffer
			msgIn.SetCommand((const char *)&inBuffer[inPos]);
			inPos +=12;
			msgIn.iMsgStatus = btcmsgstat_command;  // update status and continue into next case
		}
		else { // not enough chars read --> erase processed buffer and wait for next read/processing
			inBuffer.erase(inBuffer.begin(),inBuffer.begin()+inPos);
			inPos=0;
			break;				// exit switch as we can't continue processing
		}

	case btcmsgstat_command:	// command found, now 4 bytes for payload length
		if (inPos+4 <= iBufSize) {					// enough chars in buffer
			msgIn.SetPayloadLength(&inBuffer[inPos]);
			inPos +=4;
			msgIn.iMsgStatus = btcmsgstat_paylen;  // update status and continue into next case
		}
		else { // not enough chars read --> erase processed buffer and wait for next read/processing
			inBuffer.erase(inBuffer.begin(),inBuffer.begin()+inPos);
			inPos=0;
			break;				// exit switch as we can't continue processing
		}

	case btcmsgstat_paylen:		// payload length found, now checksum (except version and verack messages
		if (msgIn.PayloadChkStatus()) {			// checksum should be there
			if (inPos+4 <= iBufSize) {			// enough chars in buffer
				msgIn.SetPayloadChecksum(&inBuffer[inPos]);
				inPos +=4;
				msgIn.iMsgStatus = btcmsgstat_chksum;
			}
			else { // not enough chars read --> erase processed buffer and wait for next read/processing
				inBuffer.erase(inBuffer.begin(),inBuffer.begin()+inPos);
				inPos=0;
				break;				// exit switch as we can't continue processing
			}
		}
		else		// message doesn't contain a checksum, so skip this part
			msgIn.iMsgStatus = btcmsgstat_chksum;

	case btcmsgstat_chksum:					// checksum (if any) found, now payload should come
		msgIn.iPayloadLenRcvd = 0;			// we start with nothing read
		msgIn.vPayload.clear();				// clear our vector that contains the payload
		msgIn.vPayload.reserve(msgIn.iPayloadLen);	// and reserve enough space to hold the payload
		msgIn.iMsgStatus = btcmsgstat_payload;		// everything initialized, now ready to get the payload

	case btcmsgstat_payload:				// reading the payload
		while (msgIn.iPayloadLenRcvd < msgIn.iPayloadLen && inPos < iBufSize) {	// as long as we don't have all chars of payload, or until end of read buffer
			msgIn.vPayload.push_back(inBuffer[inPos++]);
			msgIn.iPayloadLenRcvd++;
		}
		if (msgIn.iPayloadLenRcvd == msgIn.iPayloadLen) {  // complete message read
			msgIn.iMsgStatus = btcmsgstat_all;  // done reading
			inBuffer.erase(inBuffer.begin(),inBuffer.begin()+inPos);
			inPos=0;
			PostMessage(hWndMain, myMsg_ProcessReadMessage, 1, 0);	// put process message in windows queue
		}
		else { // not complete payload read cause we've read the complete buffer
			inBuffer.clear();
			inPos=0;
			break;
		}
	};
}

bool BTCtcp::WriteMessage(BTCMessage& msgOut)
{
	// calculate length and checksum of payload
	const char *p;
	std::vector<char>* outBuf = new std::vector<char>;			// our temporary send buffer (inside of thread)
	msgOut.iPayloadLen = msgOut.vPayload.size();
	if (msgOut.bPayloadChk) {	// msg needs to send checksum
		uint256 hash = Hash(msgOut.vPayload.begin(),msgOut.vPayload.end());
		memcpy(&msgOut.iPayloadChk, &hash, sizeof(msgOut.iPayloadChk));			// only first bytes needed
		outBuf->reserve(24 + msgOut.iPayloadLen);								// reserve enough bytes
	}
	else
		outBuf->reserve(20 + msgOut.iPayloadLen);								// don't need 4 bytes of checksum
	size_t iCurLen;
	// MESSAGE HEADER
	//   magic bytes
	for (iCurLen=0; iCurLen<sizeof(msgOut.chStart); iCurLen++)
		outBuf->push_back(msgOut.chStart[iCurLen]);
	//   command
	for (iCurLen=0; iCurLen<sizeof(msgOut.chCommand); iCurLen++)
		outBuf->push_back(msgOut.chCommand[iCurLen]);
	//   length of payload
	p = (const char *)&msgOut.iPayloadLen;
	for (iCurLen=0; iCurLen<sizeof(msgOut.iPayloadLen); iCurLen++)
		outBuf->push_back(p[iCurLen]);
	//   checksum, only if needed
	if (msgOut.bPayloadChk) {
		p = (const char *)&msgOut.iPayloadChk;
		for (iCurLen=0; iCurLen<sizeof(msgOut.iPayloadChk); iCurLen++)
			outBuf->push_back(p[iCurLen]);
	}
	// payload
	outBuf->insert(outBuf->end(),msgOut.vPayload.begin(),msgOut.vPayload.end());
	// post "AppendOutBuffer" message to main thread, so it can be appended correctly
	PostMessage(hWndMain, myMsg_AppendOutBuffer, 0, reinterpret_cast<LPARAM>(outBuf));
	return true;
}

// Process node status depending on iBTCNodeStatus
LRESULT BTCtcp::ProcessNodeStatus(HWND hWnd, LPARAM lParam)
{
	int iPrevStat = iBTCNodeStatus;
	uint256 hash_start;
	TCHAR szText[MAX_LOADSTRING];
	switch (iBTCNodeStatus) {
	case 0:	// nothing done yet --> we need to send a "version" message
		if (iBTCSocketStatus>=5) {		// we need a connected socket
			if (SendMsg_Version())
				iBTCNodeStatus++;
		}
		break;
	case 1: // "version" message sent --> we wait for verack message
		// TODO: if we don't receive a reply in a timely fashion, resend version message
		break;

	case 2:  // "verack" message received --> now send a getblocks
		// determine hash_start:
		{
			mysqlpp::Connection *my_conn = mydb.GrabConnection();
			int iSafeHeight = mydb.GetSafeHeight(my_conn);			// minimal height to ask
			if (iSafeHeight<0)
				iSafeHeight=0;
			int iBestHeight = mydb.GetBestHeightKnown(my_conn)-10;	// optimal height to ask (10 blocks deep from current known end)
			if (iBestHeight<iSafeHeight)
				iBestHeight=iSafeHeight;							// if we did not yet download 10 blocks
			if (!mydb.GetBlockHashFromHeight(my_conn, iBestHeight, hash_start)) {
				iBestHeight=0;
			}
			if (SendMsg_GetBlocks(hash_start, uint256(0))) {
				iBTCNodeStatus++;
				Peer_AskedBlockHeight = iBestHeight + 500;  // we asked up to 500 new blocks
			}
			mydb.ReleaseConnection(my_conn);
		}
		break;

	case 3:	// "getblocks" sent, we wait for inv vectors
		if (!bPingTimerSet) {	// not yet a valid timer set
			UINT_PTR uResult = SetTimer(hWnd, IDT_PINGTIMER, 10000, NULL);		// make a 10 seconds timer to send ping messages to keep connection alive
			bPingTimerSet = (uResult!=0);
		}
		break;

	case 4: // "inv" received and processed and sent getdata to get missing blocks, we wait for "block" messages
		break;

	default:
		break;
	}
	if (iPrevStat != iBTCNodeStatus)								// did node status change ?
		PostMessage(hWnd, myMsg_ProcessNodeStatus, 2, 0);			// yes -> post a message to call this function again
	if (iPrevStat != iBTCNodeStatus	|| (lParam && MsgREPAINT)) {	// status change or forced repaint
		LoadString(hInst, IDS_NS_INIT+iBTCNodeStatus, szText, MAX_LOADSTRING);
		SetWindowText(hStaticNodeStatus, szText);
		InvalidateRect(hWndMain, &rcNodeStatus, true);
	}
	return 0;
}

// WORKER FUNCTIONS in WORKER THREADS
//
// Process "inv", "block" and "tx" messages from the concurrent queue
//
bool ProcessInvMsg(BTCMessage& msg, bool bProcessTx)
{
	if (!msg.VerifyChecksum())	// something wrong with checksum, can't process it
		return false;
	// we received a list of blocks from safe height and above
	// step through list and each block not in ChainBlocks table should be asked for in a getdata msg
	std::vector<unsigned char>::iterator it = msg.vPayload.begin();
	unsigned int iNrInvRcvd = (unsigned int)msg.GetVarInt(it);
	if (msg.vPayload.size()<(iNrInvRcvd*36+(it - msg.vPayload.begin())))
		return false;					// something wrong with length of msg
	unsigned int i, invType;
	uint256 invHash;
	std::vector<uint256> vUnknownBlockHashes;				// save all block hashes we don't know yet
	std::vector<uint256> vUnknownTxHashes;					// save all tx hashes we don't know yet
	mysqlpp::Connection *my_conn = mydb.GrabConnection();	// get a database connection
	for (i=0 ; i<iNrInvRcvd; i++) {	// loop through all vectors received
		memcpy(&invType, &it[i*36], 4);
		memcpy(invHash.begin(), &it[i*36+4], 32);	// get the hash
		switch(invType)
		{
		case MSG_TX:  // it is a transaction
			if (bProcessTx && !mydb.IsTxHashKnown(my_conn, &invHash))
				vUnknownTxHashes.push_back(invHash);
			break;
		case MSG_BLOCK: // it is a block
			if (!mydb.IsBlockHashKnown(my_conn, &invHash)) 		// if hash is not yet known, add it to our list of unknown hashes
				vUnknownBlockHashes.push_back(invHash);
			break;
		}
	}
	mydb.ReleaseConnection(my_conn);
	// vUnknown___Hashes now contains all unknown block/tx hashes from received inv message
	// make a getdata message to request the block data for all of these
	return BTCnode.SendMsg_GetData(vUnknownTxHashes, MSG_TX) && BTCnode.SendMsg_GetData(vUnknownBlockHashes, MSG_BLOCK);
}

bool ProcessBlockMsg(BTCMessage &msg)
{
	if (!msg.VerifyChecksum() || msg.vPayload.size()<=80)	// something wrong with checksum or length, can't process it
		return false;

	// calculate hash of this block
	uint256 curBlockHash = Hash(msg.vPayload.begin(), msg.vPayload.begin()+80);

	mysqlpp::Connection *my_conn = mydb.GrabConnection();	// get a database connection
	if (mydb.IsBlockHashKnown(my_conn, &curBlockHash)) {
		mydb.ReleaseConnection(my_conn);
		return true;			// nothing to do, as we got already this block somehow (from previous download or unsollicited message)
	}
	// now do the real stuff: block is not known yet
	ChainBlocks newBlock(0);	// create new block to add, we don't know ID yet
	newBlock.hash.assign((char *)curBlockHash.begin(), 32);
	newBlock.prevhash.assign((char *)&msg.vPayload[4], 32);	// first 4 bytes are version number, not checked for now
	newBlock.height=-2;			// height not yet known

	// --> see if prevBlockHash exists in existing blocks in ChainBlocks
	int iPrevHeight = mydb.GetBlockHeightFromHash(my_conn, newBlock.prevhash);
	if (iPrevHeight>=-1) {					// this new block has a previous hash of a temporary block with known height
		newBlock.height=iPrevHeight+1;		// so height of this block is known
		int iBestHeight=mydb.GetBestHeightKnown(my_conn);
		if (newBlock.height<=iBestHeight) {	// height of new block is less or equal then best known temporary --> discard all temp blocks with this height and above
			for (iPrevHeight=iBestHeight; iPrevHeight>=newBlock.height; iPrevHeight--)
				mydb.DeleteBlockDataOfHeight(my_conn, iPrevHeight);
		}
	}
	else {									// this new block has unknown height
		if (BTCnode.GetNodeStatus()>4) {				// we have an up-to-date block chain, so this is unusual, probably a block fork happened
			int iSafeHeight = mydb.GetSafeHeight(my_conn);
			if (iSafeHeight>0) {
				uint256 hash_start;
				if (mydb.GetBlockHashFromHeight(my_conn, iSafeHeight, hash_start)) {
					if (BTCnode.SendMsg_GetBlocks(hash_start, uint256(0))) {
						BTCnode.Peer_AskedBlockHeight = iSafeHeight + 500;  // we asked up to 500 new blocks
					}
				}
			}
		}
	}
	newBlock.status = (newBlock.height>=0)? 1 : 0;

	// --> add it to ChainBlocks
	if (mydb.AddBlockToChain(my_conn, newBlock)) {	// block added successfully
		// --> add tx's to ChainTxIns and ChainTxOuts
		std::vector<unsigned char>::iterator itTxBegin;
		std::vector<unsigned char>::iterator it=msg.vPayload.begin()+80;	// we start at 80 offset with TX data
		int iNrTransactions = (int)msg.GetVarInt(it);						// retrieve the varint indicating number of transactions
		int iEachTx;
		mysqlpp::Transaction myTrans(*my_conn);
		for (iEachTx=0; iEachTx < iNrTransactions; iEachTx++) {		// loop through each transaction
			itTxBegin = it;											// remember where current transaction starts for hash calculation later on
			ChainTxs newTx(newBlock.ID, iEachTx);					// insert incomplete Tx as we need referencial integrity on depending TxIns and TxOuts
			if (mydb.InsertChainTx(my_conn, newTx)) {
				it +=4;		// skip version number
				int iNrTxIO = (int)msg.GetVarInt(it);				// number of input transactions
				int iEachTxIO;
				for (iEachTxIO=0; iEachTxIO < iNrTxIO; iEachTxIO++) {
					// loop through each "in" transaction
					// we retain only the "OutPoint" Structure, we expect signature to be valid (otherwise it wouldn't be in a block)
					ChainTxIns newTxIn(newBlock.ID, iEachTx, iEachTxIO);		// create record data variable
					newTxIn.opHash.assign((char *)&it[0],32);	// OutPoint hash
					memcpy(&newTxIn.opN, &it[32], 4);			// OutPoint index number
					it+=36;		// skip OutPoint
					int iVI = (int)msg.GetVarInt(it);			// length of script
					it+=iVI;	// skip script
					it+=4;		// skip sequence
					mydb.InsertChainTxIn(my_conn, newTxIn);
				}
				iNrTxIO = (int)msg.GetVarInt(it);				// number of output transactions
				for (iEachTxIO=0; iEachTxIO < iNrTxIO; iEachTxIO++) {
					// loop through each "out" transaction
					// we examine the script and extract: value, type and hash(es)
					ChainTxOuts newTxOut(newBlock.ID, iEachTx, iEachTxIO);		// create record data variable
					memcpy(&newTxOut.value, &it[0], 8);			// value of output
					it+=8;		// skip the value
					newTxOut.txType=0;
					int iVI = (int)msg.GetVarInt(it);			// length of script
					// examine script to find out the type of transactions
					if (it[0]<OP_PUSHDATA1) {	// script starts with immediate data
						if (it[0]==65 && it[66]==OP_CHECKSIG) {		// transaction is "Transaction to IP address/ Generation"
							vector<unsigned char> vPubKey(it+1, it+66);	// extract Public Key from Msg
							uint160 uKeyHash = Hash160(vPubKey);
							newTxOut.smartID.assign((const char *)&uKeyHash, 20);		// copy it into record
							newTxOut.smartIDAdr = Hash160ToAddress(uKeyHash);			// store base58 address too
							newTxOut.storeID.it_is_null();								// storeID is not used
							newTxOut.txType=2;
						}
					}
					else {
						if (it[0]==OP_DUP && it[1]==OP_HASH160 && it[2]==20 && it[23]==OP_EQUALVERIFY) { // transaction start = std Tx to BitcoinAddress
							if (it[24]==OP_CHECKSIG) {	// it is standard transaction
								vector<unsigned char> vKeyHash(it+3, it+23);			// extract hash from Msg
								newTxOut.smartID.assign((const char *)&it[3], 20);		// extract hash from Msg
								newTxOut.smartIDAdr = Hash160ToAddress( uint160(vKeyHash) );
								newTxOut.storeID.it_is_null();
								newTxOut.txType=1;
							}
							else
								if (1==0) {	// our new type of transaction
									newTxOut.txType=3;
								}
						}
					}
					it+=iVI;	// skip script
					if (newTxOut.txType!=0)
						mydb.InsertChainTxOut(my_conn, newTxOut);
				} // END for each TxOut
				it+=4;		// skip lock time
			} // END if insert chain ok
			// iterator it points now to the end of the transaction, now we can calculate the hash of it
			curBlockHash = Hash(itTxBegin, it);						// calculate it
			newTx.txHash.assign((const char *)&curBlockHash, 32);	// transfer to record
			mydb.UpdateChainTx(my_conn, newTx);						// update the already inserted record
			mydb.TxUnconfirmedDies(my_conn, newTx.txHash);			// set life=0 for this unconfirmed tx
		} // END loop Tx
		myTrans.commit();
	} // END add block
	mydb.TxUnconfirmedAges(my_conn);
	mydb.ReleaseConnection(my_conn);
	return true;
}

bool ProcessTxMsg(BTCMessage &msg)
{
	if (!msg.VerifyChecksum() || msg.vPayload.size()<=55)				// something wrong with checksum or length, can't process it
		return false;
	uint256 curHash = Hash(msg.vPayload.begin(), msg.vPayload.end());	// compute tx hash
	mysqlpp::Connection* my_conn = mydb.GrabConnection();
	if (mydb.IsTxHashKnown(my_conn, &curHash)) {
		mydb.ReleaseConnection(my_conn);
		return true;				// nothing to do, hash already known
	}
	TxUnconfirmed newTx(0);							// ID will AUTO_INCREMENT
	newTx.hash.assign((const char *)&curHash, 32);	// hash of tx
	newTx.life=100;									// maximum life of tx is 100 blocks
	if (mydb.InsertTxUnconfirmed(my_conn, newTx)) {			// insert Tx into TxUnconfirmed table
		// inputs of this transaction = outputs that are no longer available --> we need to store these so clients can deny a double spend attempt
		std::vector<unsigned char>::iterator it=msg.vPayload.begin()+4;  // skip version for the moment
		int iNrTxIO = (int)msg.GetVarInt(it);				// number of input transactions
		int iEachTxIO;
		for (iEachTxIO=0; iEachTxIO < iNrTxIO; iEachTxIO++) {
			// loop through each "in" transaction
			// we retain only the "OutPoint" Structure, we expect signature to be valid (otherwise it wouldn't be transmitted by a regular client)
			TxInUnconfirmed newTxIn(newTx.ID, iEachTxIO);	// create record data variable
			newTxIn.hash.assign((char *)&it[0],32);			// OutPoint hash
			memcpy(&newTxIn.txindex, &it[32], 4);			// OutPoint index number
			it+=36;		// skip OutPoint
			int iVI = (int)msg.GetVarInt(it);			// length of script
			it+=iVI;	// skip script
			it+=4;		// skip sequence
			mydb.InsertTxInUnconfirmed(my_conn, newTxIn);
		}
	}
	TCHAR szStaticText[10];
	int i=mydb.NrTxUnconfirmed(my_conn);
	_itow_s(i, szStaticText, 10, 10);
	SetWindowText(hStaticUnconfirmedTxs,szStaticText);
	mydb.ReleaseConnection(my_conn);
	return true;
}

LRESULT BTCtcp::ProcessReadMessage(HWND hWnd, LPARAM lParam)
{
	if (msgIn.iMsgStatus != btcmsgstat_all)
		return -2;		// can't process message, as it is not complete
	bool bFlag=false;
	switch (msgIn.iCommand) {
	case btcmsg_version:
		{		// received a version msg  --> send verack message back
			unsigned int iRcvdVer;
			std::vector<unsigned char>::iterator it=msgIn.vPayload.begin();
			memcpy(&iRcvdVer, &it[0], sizeof(int));	// get version
			if (iRcvdVer>=209) {	// node has sent best height information
				it +=80;			// skip fixed length message data		
				uint64 uil=msgIn.GetVarInt(it);  // now a varlength string, uil=its length
				it +=uil;
				memcpy(&Peer_BlockHeight,&it[0],sizeof(int));
			}
			else {
				mysqlpp::Connection * c = mydb.GrabConnection();
				Peer_BlockHeight = mydb.GetBestHeightKnown(c);
				mydb.ReleaseConnection(c);
			}
			bFlag = SendMsg_Verack();
		}
		break;
	case btcmsg_verack:				// received a verack msg --> check if this was indeed in response to our version command
		if (iBTCNodeStatus==1) {	// sent indeed a version command
			iBTCNodeStatus++;		// go to next lvl :)
			PostMessage(hWnd, myMsg_ProcessNodeStatus, 0, MsgREPAINT);	// change in node status, do next things
		}
		bFlag=true;		// consider it always processed
		break;
	case btcmsg_inv:											// received a inv msg --> check if it was asked for, or unsollicited
		if (iBTCNodeStatus>=3) {
			BTCMessage *btcmsgcopy = new BTCMessage(msgIn);		// copy the message
			cqInvMsg.push(*btcmsgcopy);							// push it on queue for worker thread
		}
		if (iBTCNodeStatus==3) {								// we asked for blocks, here is our response probably
			iBTCNodeStatus++;									// go to next lvl :)
			PostMessage(hWnd, myMsg_ProcessNodeStatus, 0, MsgREPAINT);
		}
		bFlag=true;
		break;

	case btcmsg_block:											// receiving block data
		if (iBTCNodeStatus>3) {									// we accept "block" messages
			BTCMessage *btcmsgcopy = new BTCMessage(msgIn);		// copy the message
			cqBlockMsg.push(*btcmsgcopy);						// push it on queue for worker thread
		}
		bFlag=true;
		break;

	case btcmsg_tx:		// receiving a transaction
		if (iBTCNodeStatus>4) {		// we accept tx messages
			BTCMessage *btcmsgcopy = new BTCMessage(msgIn);		// copy the message
			cqTxMsg.push(*btcmsgcopy);							// push it on stack for worker thread
		}
		bFlag=true;
		break;

	default:			// digest unknown messages 
		bFlag=true;
	}
	if (bFlag) {						// everything went well in switch --> msgIn is processed
		msgIn.Init(BTC_TestNetwork);	// reinitilize it
		PostMessage(hWnd, myMsg_ProcessReadBuffer, 1, 0);			// see if any other message is already waiting in Buffer !
	}
	return (bFlag? 0 : -1);
}

bool BTCtcp::SendMsg_Version(void)
{
	if (iBTCNodeStatus>0)
		return false;		// we already sent a version message
	char *p;
	BTCMessage msgVersion(btcmsg_version, BTC_TestNetwork);	// construct message with "version" command
	// make payload
	p = (char *)&iProtocolVersion;
	msgVersion.vPayload.insert(msgVersion.vPayload.end(),p,p+sizeof(int));
	// services (uint64 = 8 bytes)
	msgVersion.vPayload.push_back(1);
	msgVersion.vPayload.insert(msgVersion.vPayload.end(),7,0);
	// timestamp (8 bytes)
	__int64 unixts = GetUnixTimeStamp();
	p = (char *)&unixts;
	msgVersion.vPayload.insert(msgVersion.vPayload.end(),p,p+sizeof(__int64));
	// addrme
	msgVersion.vPayload.push_back(1);
	msgVersion.vPayload.insert(msgVersion.vPayload.end(),17,0);		// 10 bytes 0x00
	msgVersion.vPayload.insert(msgVersion.vPayload.end(),2,255);	// 2 bytes 0xFF
	p = (char *)&myIPAddress;
	msgVersion.vPayload.insert(msgVersion.vPayload.end(),p,p+4);	// 4 bytes IPv4 address
	u_short j=htons(BTC_PortNo);
	p = (char *)&j;
	msgVersion.vPayload.insert(msgVersion.vPayload.end(),p,p+sizeof(u_short));
	//addryou
	msgVersion.vPayload.push_back(1);
	msgVersion.vPayload.insert(msgVersion.vPayload.end(),17,0);		// 10 bytes 0x00
	msgVersion.vPayload.insert(msgVersion.vPayload.end(),2,255);	// 2 bytes 0xFF
	p = (char *)&(dest.sin_addr);
	msgVersion.vPayload.insert(msgVersion.vPayload.end(),p,p+4);	// 4 bytes IPv4 address
	p = (char *)&(dest.sin_port);
	msgVersion.vPayload.insert(msgVersion.vPayload.end(),p,p+sizeof(u_short));
	//nonce  - 8 random bytes
	p = (char *)&nonce;
	msgVersion.vPayload.insert(msgVersion.vPayload.end(),p,p+sizeof(nonce));
	//User Agent (BIP0014)
	msgVersion.vPayload.push_back(BTCMsgUserAgent.length());		// length should be limited so that it fits 1 byte varint !
	msgVersion.vPayload.insert(msgVersion.vPayload.end(),BTCMsgUserAgent.data(),BTCMsgUserAgent.data()+BTCMsgUserAgent.length());
/*	Old UserAgent = null string:
    msgVersion.vPayload.push_back(0);
*/
	//highest block known
	mysqlpp::Connection *my_conn = mydb.GrabConnection();
	int i = mydb.GetBestHeightKnown(my_conn);
	p = (char *)&i;
	msgVersion.vPayload.insert(msgVersion.vPayload.end(),p,p+sizeof(int));
	mydb.ReleaseConnection(my_conn);

	return WriteMessage(msgVersion);
}

bool BTCtcp::SendMsg_Verack(void)
{
	BTCMessage msgVerack(btcmsg_verack, BTC_TestNetwork);		// construct message with "verack" command
	// no payload
	return WriteMessage(msgVerack);
}

// construct and send a "getblocks" message requesting blocks between start & end hash
bool BTCtcp::SendMsg_GetBlocks(uint256 hash_start, uint256 hash_end)
{
	BTCMessage msgGetBlocks(btcmsg_getblocks, BTC_TestNetwork);
	// make payload
	// version 0.3.20
	char *p = (char *)&iProtocolVersion;
	msgGetBlocks.vPayload.insert(msgGetBlocks.vPayload.end(),p,p+sizeof(int));
	msgGetBlocks.AppendVarInt(1);			// only one hash_start follows
	p = (char *)&hash_start;
	msgGetBlocks.vPayload.insert(msgGetBlocks.vPayload.end(),p,p+32);
	p = (char *)&hash_end;
	msgGetBlocks.vPayload.insert(msgGetBlocks.vPayload.end(),p,p+32);
	return WriteMessage(msgGetBlocks);
}

bool BTCtcp::SendMsg_GetData(std::vector<uint256> &vHashes, int HashType)
{
	BTCMessage msgGetData(btcmsg_getdata, BTC_TestNetwork);
	// payload= count + inv vectors
	msgGetData.AppendVarInt(vHashes.size());
	// loop through each hash
	BOOST_FOREACH(uint256 &ui, vHashes)
	{
		msgGetData.AppendInvVector(HashType, ui);
	}
	return WriteMessage(msgGetData);
}

bool BTCtcp::SendMsg_Ping(void)
{
	BTCMessage msgVerack(btcmsg_ping, BTC_TestNetwork);		// construct message with "ping" command
	// no payload
	return WriteMessage(msgVerack);
}

bool BTCtcp::SendMsg_Tx(std::vector<unsigned char> &vtx)
{
	BTCMessage msgTx(btcmsg_tx, BTC_TestNetwork);				// construct message with "tx command
	// set payload
	msgTx.vPayload = vtx;
	return WriteMessage(msgTx);
}

//
// Worker threads to process "inv", "block" and "tx" messages
//
//  ProcessReadMessage will push messages into relevant queue
//	Worker thread will pop messages from the queue and process them.
//	Each message type will still have serial processing,
//	but processing runs in parallel to main (UI) thread.
//
// Thread_DoProcessInvMsg: process the queue "InvMessages"
//
//
DWORD WINAPI Thread_DoProcessInvMsg(LPVOID lParam)
{
	mysqlpp::Connection::thread_start();
	while (!bThreadAbort[WT_InvMsg]) {		// loop until thread abort signal comes
		BTCMessage msg;
		if (cqInvMsg.try_pop(msg)) {		// successfully popped an "inv" message from the queue
			bool bCheckTx=false;
			if (BTCnode.GetNodeStatus()>4) {    // chain is up to date
				mysqlpp::Connection * mc = mydb.GrabConnection();
				int iBest = mydb.GetBestHeightKnown(mc);
				int iSafe = mydb.GetSafeHeight(mc);
				mydb.ReleaseConnection(mc);
				if (iBest>0 && iSafe>0)
					bCheckTx = (iBest <= iSafe+20);    // and less then 20 blocks unconfirmed
			}
			ProcessInvMsg(msg, BTCnode.GetNodeStatus()>4);
		}
		else
			Sleep(499);						// nothing to do, wait 0.5 seconds before checking again
	}
	PostMessage(hWndMain, myMsg_ThreadFinished, 0, WT_InvMsg);
	mysqlpp::Connection::thread_end();
	return 0;
}

DWORD WINAPI Thread_DoProcessBlockMsg(LPVOID lParam)
{
	mysqlpp::Connection::thread_start();
	OutputDebugString(L"DoProcessBlockMsg thread started\n");
	while (!bThreadAbort[WT_BlockMsg]) {	// loop until thread abort signal comes
		BTCMessage msg;
		OutputDebugString(L"DoProcessBlockMsg try pop\n");
		if (cqBlockMsg.try_pop(msg)) {		// successfully popped an "block" message from the queue
			OutputDebugString(L"DoProcessBlockMsg processBlockMsg\n");
			if (ProcessBlockMsg(msg)) {		// sucessfully processed the block
				OutputDebugString(L"BlockMsg finished\n");
				PostMessage(hWndMain, myMsg_BlockProcessed, 0, 0);	// inform main thread 
			}
		}
		else {		// not possible to retrieve a msg from the queue (probably empty)
			OutputDebugString(L"DoProcessBlockMsg sleeping\n");
			DWORD dwSleep;
			if (BTCnode.GetNodeStatus()>4) {
				dwSleep=521;		// up-to-date chain will only receive 1 block each 10 minutes
			}
			else {
				if (BTCnode.GetNodeStatus()>2)
					dwSleep=149;	// downloading chain now, should get blocks frequently
				else
					dwSleep=8000;  // setting up node, don't need frequent check for the moment
			}
			Sleep(dwSleep);			// queue got empty, wait before checking again
		}
	}
	PostMessage(hWndMain, myMsg_ThreadFinished, 0, WT_BlockMsg);
	mysqlpp::Connection::thread_end();
	return 0;
}

DWORD WINAPI Thread_DoProcessTxMsg(LPVOID lParam)
{
	mysqlpp::Connection::thread_start();
	OutputDebugString(L"DoProcessTxMsg thread started\n");
	while (!bThreadAbort[WT_TxMsg]) {
		BTCMessage msg;
		if (cqTxMsg.try_pop(msg)) {			// successfully popped an "tx" message from the queue
			ProcessTxMsg(msg);				// process it
		}
		else		// not possible to retrieve a msg from the queue (probably empty)
			Sleep(541);
	}
	PostMessage(hWndMain, myMsg_ThreadFinished, 0, WT_TxMsg);
	mysqlpp::Connection::thread_end();
	return 0;
}

DWORD WINAPI Thread_DoBlockConfirms(LPVOID lParam)
{
	mysqlpp::Connection::thread_start();
	while(!bThreadAbort[WT_DBConfirm]) {
		mysqlpp::Connection * my_conn = mydb.GrabConnection();	// grab a database connection
		int iBestHeight;
		if (my_conn!=NULL) {
			mydb.ConfirmTempBlocks(my_conn);				    // process blocks more then 10 blocks deep.
			TCHAR szStaticText[10];
			iBestHeight = mydb.GetSafeHeight(my_conn);
			mydb.ReleaseConnection(my_conn);
			_itow_s(iBestHeight, szStaticText, 10, 10);
			SetWindowText(hStaticProcessedBlock,szStaticText);   
		}
		DWORD dwSleep;
		if (BTCnode.GetNodeStatus()>4 && (iBestHeight+15>BTCnode.Peer_BlockHeight)) {
			dwSleep=4999;		// up-to-date chain will only receive 1 block each 10 minutes
		}
		else {
			if (BTCnode.GetNodeStatus()>2)
				dwSleep=13;		// downloading chain now, blocks should be waiting
			else
				dwSleep=10007;  // setting up node, don't need frequent check for the moment
		}
		Sleep(dwSleep);			// queue got empty, wait before checking again
	}
	PostMessage(hWndMain, myMsg_ThreadFinished, 0, WT_DBConfirm);
	mysqlpp::Connection::thread_end();
	return 0;
}