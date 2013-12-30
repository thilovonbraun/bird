// Copyright (c) 2011 Thilo von Braun
// Distributed under the EUPL v1.1 software license, see the accompanying
// file license.txt or http://www.osor.eu/eupl/european-union-public-licence-eupl-v.1.1

// Header file for extra TCP functions & classes

enum BTCMsgCommands
{ btcmsg_unknown = 0,
  btcmsg_version,
  btcmsg_verack,
  btcmsg_addr,
  btcmsg_inv,
  btcmsg_getdata,
  btcmsg_getblocks,
  btcmsg_getheaders,
  btcmsg_tx,
  btcmsg_block,
  btcmsg_getaddr,
  btcmsg_checkorder,
  btcmsg_submitorder,
  btcmsg_reply,
  btcmsg_ping,
  btcmsg_alert
};

// INCOMING message status:
//	0 : init only, nothing received yet
//	1 : messageheader found
//  2 : message command read
//  3 : payload length read
//  4 : checksum read (skipped in some msg)
//  5 : reading payload (iPayloadLenRcvd < iPayloadLen)
//  6 : payload read
// OUTGOING message status:
//  0 : init only, nothing set yet
//  2 : message command set
//  5 : setting payload
//  6 : ready to trnsmit

enum BTCMsgStatus
{
	btcmsgstat_init = 0,		// message variable initialized, nothing received yet
	btcmsgstat_header,			// header found
	btcmsgstat_command,			// command found
	btcmsgstat_paylen,			// payload length found
	btcmsgstat_chksum,			// checksum found
	btcmsgstat_payload,			// reading payload
	btcmsgstat_all				// complete message found
};

uint64 GetVarInt(std::vector<unsigned char>::iterator it, short *s);	// retrieve the var int value at it, return in s the length of the varint
DWORD WINAPI Thread_DoProcessInvMsg(LPVOID lParam);
DWORD WINAPI Thread_DoProcessBlockMsg(LPVOID lParam);
DWORD WINAPI Thread_DoProcessTxMsg(LPVOID lParam);
DWORD WINAPI Thread_DoBlockConfirms(LPVOID lParam);
DWORD WINAPI Thread_DoMultiBlockPrunes(LPVOID lParam);

// forward declarations of our class names
class BTCMessage;	// one message of the BTC protocol
class BTCtcp;       // interface to connect to the bitcoin network

// class to hold 1 message of the BTC protocol
class BTCMessage
{
private:
//STATUS variables
	enum BTCMsgStatus iMsgStatus;	// internal status of message

	bool bPayloadChk;				// TRUE if message contains checksum of payload (all msg except "version" and "verack")
	unsigned int iPayloadLenRcvd;	// on incoming message, actual payload length already received

	unsigned char chStart[4];			// 4 magic bytes to indicate start of message
	char chCommand[BTC_CommandLength];	// 12 chars indicating command, zero padded
	enum BTCMsgCommands iCommand;		// command text converted to enum type for faster processing once we get the text or vice-versa
	// next 2 variables only to buffer values of incoming messages, outgoing messages have it calculated on sending it
	unsigned int iPayloadLen;			// length of actual payload (without checksum length)
	unsigned int iPayloadChk;	// checksum of payload = first 4 bytes of sha256(sha256(payload))

// private functions as they should be called only internally
	void SetPayloadChkStatus(void);			// checks the message command to see if we need checksum

public:
	std::vector<unsigned char> vPayload;				// our payload
	// constructors
	BTCMessage(bool bTestNetwork = false);							// for incoming messages
	BTCMessage(const char *szCommand, bool bTestNetwork = false);	// for outgoing messages, with command string
	BTCMessage(const enum BTCMsgCommands iCommand, bool bTestNetwork = false);	// for outgoing messages, with enumerator
	void Init(bool bTestNetwork = false);					// initializes our private data (to start over with a new message without destroying/constructing it
	void SetCommand(const char *szCommand);					// sets the command for the message using a text (incoming msg)
	void SetCommand(const enum BTCMsgCommands i);			// sets the command for the message with enumeration (outgoing msg)
	void SetPayloadLength(void *sBufferInt);				// sets the payload length read from the incoming msg (little endian byte order)
	bool PayloadChkStatus(void);							// true if message should contain payload checksum, false otherwise
	void SetPayloadChecksum(void *sBufferInt);				// sets the payload checksum read from incoming msg
	void AppendVarInt(uint64 i);							// appends the uint64 as a "varint" to the payload
	uint64 GetVarInt(std::vector<unsigned char>::iterator &it);		// returns uint64 that was saved as a varint at the indicated iterator position, iterator updated to point after varint
	void AppendInvVector(int HashType, uint256 &ui);		// appends an inv vector to the payload
	bool VerifyChecksum(void);								// see if received checksum is valid
	int GetBlockHeight(void);								// if Msg is 'block' and it is v2, get block height from coinbase info (BIP0034) otherwise set -2
	friend class BTCtcp;
};


class BTCtcp
{
private:
	WSADATA wsaData;		// WSA interface data
	SOCKET skBTC;			// the socket for our TCP connection to the Bitcoin Network
	IN_ADDR myIPAddress;	// our IPv4 address
	SOCKADDR_IN dest;		// destination address+port
	unsigned __int64 nonce;	// random nonce
	int iBTCError;			// contains last error for debugging/troubleshooting

	int iBTCSocketStatus;
// Our socket status:
//	0 : nothing done;
//  1 : Startup called;
//  2 : socket constructed;
//  3 : async socket enabled;
//  4 : connecting...
//  5 : connected
	int iBTCNodeStatus;
// Our node status:
//	0 : startup
//	1 : version command sent
//  2 : verack msg received
//  3 : getblocks sent to get up-to-date blockchain
//  4 : received inv vectors, getdata msg sent
//  5 : block chain is up to date

	std::vector<unsigned char> inBuffer;	// our receive buffer
	unsigned int inPos;						// keep track of already processed chars in inBuffer
	BTCMessage msgIn;						// current incoming message

	bool bWriteReady;						// socket is ready to accept send() commands
	std::vector<char> outBuffer;			// our send buffer
	unsigned int outPos;					// points to first not transmitted char
	SYSTEMTIME stPing;						// time of last ping message

public:
	bool bPingTimerSet;
	int Peer_BlockHeight;					// block height known to other peer (from incoming version msg)
	int Peer_ProtoVersion;					// protocol version of peer (from incoming version msg)
	int Peer_AskedBlockHeight;				// highest block height asked to other peer
	int iTimerPreviousBlockHeight;			// remember blockheight of previous timer event
	BTCtcp(void);
	~BTCtcp(void);
	int GetSocketStatus(void) { return iBTCSocketStatus; };
	int GetNodeStatus(void) { return iBTCNodeStatus; };
	void IncNodeStatus(void) { iBTCNodeStatus++; };
	void ClearNodeStatus(void) { iBTCNodeStatus=0; };
	bool ConnectToHost(int PortNo, const char *szIPAddress);
	bool ConnectToHost(int PortNo, ULONG addr);
	bool Disconnect();
	LRESULT ProcessNotifications(HWND hWnd, LPARAM lParam);				// process the notifications received from async socket
	LRESULT ProcessNodeStatus(HWND hWnd, LPARAM lParam);				// see if node has some work to do
	LRESULT ProcessReadMessage(HWND hWnd, LPARAM lParam);				// process a MsgIn that is complete
	void AppendOutBuffer(std::vector<char>* tBuf);						// append some data to output buffer
	void WriteToSocket(void);											// try to send waiting chars in our outBuffer
	void ReadFromSocket(HWND hWnd);										// try to read from socket into our rcvBuffer
	void ProcessReadBuffer(void);										// try to evaluate read buffer into a message

	// high level flow
	bool WriteMessage(BTCMessage& msgOut);								// write a message to our outBuffer
	
	bool SendMsg_Version(void);											// construct and send a "version" message, returns true on success
	bool SendMsg_Verack(void);											// idem for "verack" message
	bool SendMsg_GetBlocks(uint256 hash_start, uint256 hash_end);		// construct and send a "getblocks" message requesting blocks between start & end hash
	bool SendMsg_GetData(std::vector<uint256> &v, int typeHash);		// construct and send a "getdata" message requesting the data corresponding to supplied hashes
	bool SendMsg_Ping(void);											// construct and send a "ping" message to keep the connection alive
	bool SendMsg_Tx(std::vector<unsigned char> &vtx);					// construct and send a "tx" message, vtx contains raw data of the transaction

//	bool ProcessInvMsg(bool bProcessTx);								// process received inventory vectors (blocks always, tx depending on flag)
//	bool ProcessBlockMsg(void);											// process received block message: store it in temporary ChainXXX tables
//	bool ProcessTxMsg(void);											// process received transactions: store it in TxUnconfirmed tables
//	bool ProcessBlockChain(void);										// check and process ChainXXX table blocks/tx's if more then 10 temporary blocks are present
};

