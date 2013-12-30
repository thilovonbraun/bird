// Copyright (c) 2011-2013 Thilo von Braun
// Distributed under the EUPL v1.1 software license, see the accompanying
// file license.txt or http://www.osor.eu/eupl/european-union-public-licence-eupl-v.1.1
//
// Version: 0.2.0
//   * added define for database backend selection
//   * added logging features

#pragma once

#define BTC_TestNetwork false
#define TRACE

// define how to store reuseable IDs
//	1 = use database tables (slow, never IDs lost)
//	0 = use memory vectors (fast, info lost on interruption)
#define REUSETABLES 0									

// database back-end support
// Firebird database
// #define BIRDDB_FIREBIRD		
// MySQL database
#define BIRDDB_MYSQL

#ifdef BIRDDB_FIREBIRD
#define BIRDDB IBPP
#endif
#ifdef BIRDDB_MYSQL
#define BIRDDB mysqlpp
#endif

#define BTC_CommandLength 12								// commands have up to 12 chars
#define BTC_PortNo (BTC_TestNetwork? 18333 : 8333)			// TCP port to connect to
#define MAX_LOADSTRING 100									// max length of strings in resource file
#define MsgREPAINT (1<<5)									// always repaint

#include "resource.h"

#define IDC_BTNSQLCONNECT	9000
#define IDC_EDITSQL			9001
#define IDC_BTNBTCCONNECT	9005
#define IDC_EDITIP			9006
#define IDC_STATICSOCKSTAT	9010
#define IDC_STATICCURBLOCK	9011
#define IDC_STATICMAXBLOCK	9012
#define IDC_STATICPROCESSBLOCK	9013
#define IDC_STATICUNCONFIRMEDTXS 9014
#define IDC_STATICNODESTAT	9015
#define IDC_STATICDBSTAT	9016
#define IDT_PINGTIMER		10

#define socketMaxReadLength 49152		// maximum characters when reading from socket

// global variables needed in several cpp files
extern HWND hWndMain, hStaticSocketStatus, hStaticNodeStatus, hStaticSQLStatus;		// handles to windows and controls
extern HWND hStaticCurBlock, hStaticMaxBlock, hStaticProcessedBlock;
extern HWND hStaticUnconfirmedTxs;
extern RECT rcSocketStatus, rcNodeStatus, rcChain, rcSQLStatus;
extern HINSTANCE hInst;
extern const unsigned char pMessageStartStd[4];
extern const unsigned char pMessageStartTst[4];
extern HANDLE bThreadRunning[6];
extern bool bThreadAbort[6];
extern DWORD dwSleepProcessBlock;
extern void ShowError(int iError);
extern void ShowError(int iError, int iInformation);
extern void ShowError(int iError, TCHAR *szInfo);
extern void ShowError(int iError, const char *szInfo);
extern TCHAR szINIFile[];

// additional messages for Window Message Loop
enum BirdMsgType
{
	myTCP_Messages = WM_USER+20,		// notify messages from socket
	myMsg_ProcessReadMessage,			// process a completely read message
    myMsg_ProcessNodeStatus,			// see if node has something to do
	myMsg_ProcessReadBuffer,			// convert raw read buffer into (partial) message
	myMsg_AppendOutBuffer,				// append some data to output buffer of socket
	myMsg_WriteToSocket,				// data should be written out
	myMsg_ThreadFinished,				// thread is done
	myMsg_WaitForDBDisconnect,			// wait for safe database disconnect
	myMsg_BlockProcessed,				// a block message was processed in the worker thread
//	myMsg_BlockConfirmed				// a block was pruned from temporary tables to UTXO
};

enum wthreadtype	// our worker threads
{
	WT_InvMsg=0,
	WT_BlockMsg,
	WT_TxMsg,
	WT_DBConnect,
	WT_DBConfirm,
	WT_DBMultiPrune,
	WT_LAST			// for bounds checking
};

enum opcodetype
{
    // push value
    OP_0=0,
    OP_FALSE=OP_0,
    OP_PUSHDATA1=76,
    OP_PUSHDATA2,
    OP_PUSHDATA4,
    OP_1NEGATE,
    OP_RESERVED,
    OP_1,
    OP_TRUE=OP_1,
    OP_2,
    OP_3,
    OP_4,
    OP_5,
    OP_6,
    OP_7,
    OP_8,
    OP_9,
    OP_10,
    OP_11,
    OP_12,
    OP_13,
    OP_14,
    OP_15,
    OP_16,

    // control
    OP_NOP,
    OP_VER,
    OP_IF,
    OP_NOTIF,
    OP_VERIF,
    OP_VERNOTIF,
    OP_ELSE,
    OP_ENDIF,
    OP_VERIFY,
    OP_RETURN,

    // stack ops
    OP_TOALTSTACK,
    OP_FROMALTSTACK,
    OP_2DROP,
    OP_2DUP,
    OP_3DUP,
    OP_2OVER,
    OP_2ROT,
    OP_2SWAP,
    OP_IFDUP,
    OP_DEPTH,
    OP_DROP,
    OP_DUP,
    OP_NIP,
    OP_OVER,
    OP_PICK,
    OP_ROLL,
    OP_ROT,
    OP_SWAP,
    OP_TUCK,

    // splice ops
    OP_CAT,
    OP_SUBSTR,
    OP_LEFT,
    OP_RIGHT,
    OP_SIZE,

    // bit logic
    OP_INVERT,
    OP_AND,
    OP_OR,
    OP_XOR,
    OP_EQUAL,
    OP_EQUALVERIFY,
    OP_RESERVED1,
    OP_RESERVED2,

    // numeric
    OP_1ADD,
    OP_1SUB,
    OP_2MUL,
    OP_2DIV,
    OP_NEGATE,
    OP_ABS,
    OP_NOT,
    OP_0NOTEQUAL,

    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_LSHIFT,
    OP_RSHIFT,

    OP_BOOLAND,
    OP_BOOLOR,
    OP_NUMEQUAL,
    OP_NUMEQUALVERIFY,
    OP_NUMNOTEQUAL,
    OP_LESSTHAN,
    OP_GREATERTHAN,
    OP_LESSTHANOREQUAL,
    OP_GREATERTHANOREQUAL,
    OP_MIN,
    OP_MAX,

    OP_WITHIN,

    // crypto
    OP_RIPEMD160,
    OP_SHA1,
    OP_SHA256,
    OP_HASH160,
    OP_HASH256,
    OP_CODESEPARATOR,
    OP_CHECKSIG,
    OP_CHECKSIGVERIFY,
    OP_CHECKMULTISIG,
    OP_CHECKMULTISIGVERIFY,

    // expansion
    OP_NOP1,
    OP_NOP2,
    OP_NOP3,
    OP_NOP4,
    OP_NOP5,
    OP_NOP6,
    OP_NOP7,
    OP_NOP8,
    OP_NOP9,
    OP_NOP10,



    // template matching params
    OP_PUBKEYHASH = 0xfd,
    OP_PUBKEY = 0xfe,

    OP_INVALIDOPCODE = 0xff,
};

enum invtype
{
	MSG_ERROR=0,
	MSG_TX,
	MSG_BLOCK,
};

// #define BOOST_THREAD_USE_DLL

#include <openssl/buffer.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/ripemd.h>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/tuple/tuple.hpp>
#include <boost/tuple/tuple_comparison.hpp>
#include <boost/tuple/tuple_io.hpp>
#include <boost/array.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/config.hpp>
#include <boost/program_options/detail/config_file.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/filesystem.hpp>
#include <boost/thread.hpp>
#include <boost/log/core.hpp>
#include <boost/log/trivial.hpp>
//#include <boost/log/expressions.hpp>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/sources/severity_feature.hpp>
#include <boost/log/sources/severity_logger.hpp>


#include <WinSock2.h>
#include <iterator>
#include <vector>

using namespace std;

#include "uint256.h"
extern uint256 hashGenesisBlock;

#include "util.h"
#include <strsafe.h>
#include <concurrent_queue.h>
