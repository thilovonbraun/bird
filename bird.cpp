// Copyright (c) 2012 Thilo von Braun
// Distributed under the EUPL v1.1 software license, see the accompanying
// file license.txt or http://www.osor.eu/eupl/european-union-public-licence-eupl-v.1.1

#include "StdAfx.h"
#include "bird.h"
#include "bird_TCP.h"
#define MYSQLPP_SSQLS_NO_STATICS
#include "dbBTC.h"
#include "Shlwapi.h"
#include "Shobjidl.h"
#pragma comment (lib, "shlwapi.lib")

// #include "Commdlg.h"

// Global Variables:
HINSTANCE hInst;								// current instance
HANDLE hMutexDBLock;							// mutex database locks
HACCEL hAccelTable;
TCHAR szTitle[MAX_LOADSTRING];					// The title bar text
TCHAR szWindowClass[MAX_LOADSTRING];			// the main window class name

HWND hWndMain;
HWND hStaticSocketStatus, hStaticNodeStatus, hStaticSQLStatus;
HWND hStaticProcessedBlock, hStaticMaxBlock, hStaticCurBlock;
HWND hStaticUnconfirmedTxs;

RECT rcSQLStatus={130,70,130+220,70+29};
RECT rcSocketStatus={110,70+70, 110+204, 70+70+29};
RECT rcNodeStatus={110,183+70, 110+204, 183+70+29};

BTCtcp BTCnode;
dbBTC mydb;

TCHAR szINIFile[MAX_PATH];

// Worker threads communication variables
Concurrency::concurrent_queue<BTCMessage> cqInvMsg, cqBlockMsg, cqTxMsg;

// Memorize if threads are running
HANDLE bThreadRunning[5];  // 0: InvMsg, 1: BlockMsg, 2: TxMsg, 3: dbConnect, 4: SQLconfirm
// Flags to abort thread
bool bThreadAbort[5];

// Forward declarations of functions included in this code module:
ATOM				MyRegisterClass(HINSTANCE hInstance);
BOOL				InitInstance(HINSTANCE, int);
LRESULT				CreateChildsInMain(HWND hWnd);
LRESULT CALLBACK	WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK	About(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK	DumpDB_Dlg(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK	LoadDB_Dlg(HWND, UINT, WPARAM, LPARAM);

int APIENTRY _tWinMain(HINSTANCE hInstance,
                     HINSTANCE hPrevInstance,
                     LPTSTR    lpCmdLine,
                     int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	MSG msg;

	// Initialize global strings
	LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadString(hInstance, IDC_UBICARD, szWindowClass, MAX_LOADSTRING);
	GetModuleFileName(NULL, szINIFile, MAX_PATH);
	PTCHAR p = _tcsrchr(szINIFile, '\\');	// last backslash
	if (p!=NULL) {	// found a backslash
		p++;
		*p=0;		// cut off file name
	}
	_tcscat_s(szINIFile, MAX_PATH, L"bird.conf");
	MyRegisterClass(hInstance);

	// Perform application initialization:
	if (!InitInstance (hInstance, nCmdShow))
	{
		return FALSE;
	}

	hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_UBICARD));

	// Main message loop:
	BOOL bgMsg;
	while ( (bgMsg=GetMessage(&msg, NULL, 0, 0)) != 0)
	{
		if (bgMsg==-1)
			break;		// error occured in GetMessage
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg) && !IsDialogMessage(hWndMain, &msg) )
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	mysqlpp::Connection::thread_end();
	CloseHandle(hMutexDBLock);
	if (bgMsg==-1)
		return (int)GetLastError();
	else
	    return (int) msg.wParam;
}

//
// DoNonUserMessages
//
// Process all awaiting non WM_USER messages using PeekMessage
// Returns: true if WM_QUIT message retrieved
//
bool DoNonUserMessages(void)
{
	MSG msg;
	while ( PeekMessage(&msg, NULL, 0, WM_USER-1, PM_REMOVE|PM_NOYIELD))
	{
		if (msg.message == WM_QUIT)
			return true;
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg) && !IsDialogMessage(hWndMain, &msg) )
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return false;
}

//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
//  COMMENTS:
//
//    This function and its usage are only necessary if you want this code
//    to be compatible with Win32 systems prior to the 'RegisterClassEx'
//    function that was added to Windows 95. It is important to call this function
//    so that the application will get 'well formed' small icons associated
//    with it.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style			= CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc	= WndProc;
	wcex.cbClsExtra		= 0;
	wcex.cbWndExtra		= 0;
	wcex.hInstance		= hInstance;
	wcex.hIcon			= LoadIcon(hInstance, MAKEINTRESOURCE(IDI_UBICARD));
	wcex.hCursor		= LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground	= (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName	= MAKEINTRESOURCE(IDC_UBICARD);
	wcex.lpszClassName	= szWindowClass;
	wcex.hIconSm		= LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassEx(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable
   int i;
   for (i=0; i<WT_LAST; i++) {
	   bThreadRunning[i] = NULL;
	   bThreadAbort[i] = false;
   }
   // Create mutex object for database locks
   //hMutexDBLock = CreateMutex(NULL, FALSE, NULL);
   //if (hMutexDBLock == NULL) {
	  // OutputDebugString(L"CreateMutex error");
	  // return FALSE;
   //}

   hWndMain = CreateWindow(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, 435, 405, NULL, NULL, hInstance, NULL);

   if (!hWndMain)
      return FALSE;
   if (CreateChildsInMain(hWndMain)<0)
	   return FALSE;
   ShowWindow(hWndMain, nCmdShow);
   // Now initialize child windows to first state
   // TODO: read previous state from registry or something
   SetFocus(GetDlgItem(hWndMain, IDC_EDITSQL));
   EnableWindow(GetDlgItem(hWndMain, IDC_BTNBTCCONNECT), FALSE);
   UpdateWindow(hWndMain);

   return TRUE;
}

//
//  FUNCTION: PaintMainWindow(HWND)
//
//  PURPOSE:  Paints the main window
//
void PaintMainWindow(HWND hWnd)
{
	PAINTSTRUCT ps;
	HDC hdc;
	size_t stlen;

	hdc = BeginPaint(hWnd, &ps);
	TCHAR sz1[MAX_LOADSTRING];
	SetTextAlign(hdc, TA_LEFT | TA_BASELINE);
	LoadString(hInst, IDS_IPSQL, sz1, MAX_LOADSTRING);
	if (SUCCEEDED(StringCchLength(sz1, MAX_LOADSTRING, &stlen)))
		TextOut(hdc, 15, 35+15, sz1, stlen);
	LoadString(hInst, IDS_IP, sz1, MAX_LOADSTRING);
	if (SUCCEEDED(StringCchLength(sz1, MAX_LOADSTRING, &stlen)))
		TextOut(hdc, 15, 35+15+70, sz1, stlen);
	LoadString(hInst, IDS_DBSTATUS, sz1, MAX_LOADSTRING);
	if (SUCCEEDED(StringCchLength(sz1, MAX_LOADSTRING, &stlen)))
		TextOut(hdc, 15, 35+15+35, sz1, stlen);
	LoadString(hInst, IDS_SOCKETSTATUS, sz1, MAX_LOADSTRING);
	if (SUCCEEDED(StringCchLength(sz1, MAX_LOADSTRING, &stlen)))
		TextOut(hdc, 15, 35+15+105, sz1, stlen);
	LoadString(hInst, IDS_COUNT, sz1, MAX_LOADSTRING);
	if (SUCCEEDED(StringCchLength(sz1, MAX_LOADSTRING, &stlen)))
		TextOut(hdc, 15, 155+70+15, sz1, stlen);
	LoadString(hInst, IDS_SLASH, sz1, MAX_LOADSTRING);
	if (SUCCEEDED(StringCchLength(sz1, MAX_LOADSTRING, &stlen)))
		TextOut(hdc, 140-7, 155+70+15, sz1, stlen);
	LoadString(hInst, IDS_BLOCKS, sz1, MAX_LOADSTRING);
	if (SUCCEEDED(StringCchLength(sz1, MAX_LOADSTRING, &stlen)))
		TextOut(hdc, 202, 155+70+15, sz1, stlen);
	LoadString(hInst, IDS_PROCESSED, sz1, MAX_LOADSTRING);
	if (SUCCEEDED(StringCchLength(sz1, MAX_LOADSTRING, &stlen)))
		TextOut(hdc, 15, 215+70+15, sz1, stlen);
	LoadString(hInst, IDS_NODESTATUS, sz1, MAX_LOADSTRING);
	if (SUCCEEDED(StringCchLength(sz1, MAX_LOADSTRING, &stlen)))
		TextOut(hdc, 15, 183+70+15, sz1, stlen);
	LoadString(hInst, IDS_TXUNCONFIRMED, sz1, MAX_LOADSTRING);
	if (SUCCEEDED(StringCchLength(sz1, MAX_LOADSTRING, &stlen)))
		TextOut(hdc, 15, 238+70+15, sz1, stlen);

	// TODO: Add any drawing code here...
	EndPaint(hWnd, &ps);
}

//
// Thread_DoDBConnect: do the initial database connect
//
//		Separate thread as it will load "reusable ID's" vectors, which can take several tens of seconds to complete
//
DWORD WINAPI Thread_DoDBConnect(LPVOID lParam)
{
	PostMessage(hWndMain, myMsg_ThreadFinished, mydb.SetupConnectionPool(), WT_DBConnect);	// post message when ConnectionPool setup returns
	return 0;
}

bool ProcessBtnSQLConnect(WPARAM wParam)
{
	HWND hCtrl = GetDlgItem(hWndMain, IDC_BTNSQLCONNECT);
	if (mydb.IsConnected()) {	// user wants to disconnect now
		bThreadAbort[WT_InvMsg]=bThreadAbort[WT_BlockMsg]=true;
		bThreadAbort[WT_TxMsg]=bThreadAbort[WT_DBConfirm]=true;		// signal thread abortion
		EnableWindow(hCtrl, FALSE);									// disable button as long as we wait for threads to finish
		PostMessage(hWndMain, myMsg_WaitForDBDisconnect, wParam, 0);
	}
	else {	// user wants to connect
		TCHAR sTLoad[MAX_LOADSTRING];
		char sServer[MAX_LOADSTRING*2];
		GetWindowText(GetDlgItem(hWndMain, IDC_EDITSQL), sTLoad, MAX_LOADSTRING);	// get text from control
		WideCharToMultiByte(CP_ACP, 0, sTLoad, -1, sServer, MAX_LOADSTRING*2, NULL, NULL);
		mydb.SetServer(sServer);
		// lengthy operation can follow, do it in a worker thread
		if (bThreadRunning[WT_DBConnect]=CreateThread(NULL, 0, Thread_DoDBConnect, 0, 0, NULL)) {
			EnableWindow(hCtrl, FALSE);	// disable button as long as thread runs
		}
	}
	return true;
}

//
// ProcessDbConnectFinished
//
// Returns true if processing could be done, false otherwise (e.g. thread creation failed)
bool ProcessDbConnectFinished(BOOL bConnected)
{
	if (bConnected) {
	try {
		mysqlpp::Connection* cp = mydb.GrabConnection();
		if (cp==NULL) {
			cerr << "MySQL++ grab connection failed!\n";
			bConnected=false;
		}
        if (!cp->thread_aware()) {
            cerr << "MySQL++ wasn't built with thread awareness!\n";
            bConnected=false;
        }
		else
			cerr << "MySQL++ is thread aware.\n";
        mydb.ReleaseConnection(cp);
    }
    catch (mysqlpp::Exception& e) {
        cerr << "Failed to set up initial pooled connection: " <<
                e.what() << endl;
        bConnected=false;
    }
	}
	TCHAR sz1[MAX_LOADSTRING];
	HWND hCtrlSQL = GetDlgItem(hWndMain, IDC_BTNSQLCONNECT);
	EnableWindow(hCtrlSQL, TRUE);					// enable the button again to be able to retry or to disconnect
	InvalidateRect(hWndMain, &rcSQLStatus, TRUE);	// repaint SQL status
	if (bConnected) {  // able to connect to database
		LoadString(hInst, IDS_DISCONNECT, sz1, MAX_LOADSTRING);
		SetWindowText(hCtrlSQL, sz1);									// button is now used to disconnect
		EnableWindow(GetDlgItem(hWndMain, IDC_BTNBTCCONNECT), TRUE);	// bitcoin connection possible now
		LoadString(hInst, IDS_SS_CONNECTED, sz1, MAX_LOADSTRING);
		SetWindowText(hStaticSQLStatus, sz1);
		// database connection active --> startup worker threads
		// Processing of "inv", "block" and "tx" messages
		if (!bThreadRunning[WT_InvMsg]) {
			cqInvMsg.clear();
			bThreadAbort[WT_InvMsg]=false;
			if (bThreadRunning[WT_InvMsg] = CreateThread(NULL, 0, Thread_DoProcessInvMsg, 0, 0, NULL))
				SetThreadPriority(bThreadRunning[WT_InvMsg], THREAD_PRIORITY_NORMAL);
		}
		if (!bThreadRunning[WT_BlockMsg]) {
			cqBlockMsg.clear();
			bThreadAbort[WT_BlockMsg]=false;
			if (bThreadRunning[WT_BlockMsg] = CreateThread(NULL, 0, Thread_DoProcessBlockMsg, 0, 0, NULL))
				SetThreadPriority(bThreadRunning[WT_BlockMsg], THREAD_PRIORITY_NORMAL);
		}
		if (!bThreadRunning[WT_TxMsg]) {
			cqTxMsg.clear();
			bThreadAbort[WT_TxMsg]=false;
			if (bThreadRunning[WT_TxMsg] = CreateThread(NULL, 0, Thread_DoProcessTxMsg, 0, 0, NULL))
				SetThreadPriority(bThreadRunning[WT_TxMsg], THREAD_PRIORITY_NORMAL);
		}
		if (!bThreadRunning[WT_DBConfirm]) {
			bThreadAbort[WT_DBConfirm]=false;
			if (bThreadRunning[WT_DBConfirm] = CreateThread(NULL, 0, Thread_DoBlockConfirms, 0, 0, NULL))
				SetThreadPriority(bThreadRunning[WT_DBConfirm], THREAD_PRIORITY_NORMAL);
		}
		return bThreadRunning[WT_InvMsg] && bThreadRunning[WT_BlockMsg] && bThreadRunning[WT_TxMsg] && bThreadRunning[WT_DBConfirm];
	}
	else {	// unable to connect to database
		LoadString(hInst, IDS_SS_FAILED, sz1, MAX_LOADSTRING);		// report to user about failed connection attempt
		SetWindowText(hStaticSQLStatus, sz1);
		if (mydb.IsConnected()) {					// database thinks it is connected
			ProcessBtnSQLConnect(0);				// simulate a "disconnect" click
		}
	}
	return true;
}

void ProcessBtnBTCConnect()
{
	if (BTCnode.GetSocketStatus()<5) { // not connected
		DWORD dwIP=0;
		SendMessage(GetDlgItem(hWndMain, IDC_EDITIP), IPM_GETADDRESS, 0, (LPARAM) &dwIP);	// get IP address from control
		BTCnode.ConnectToHost(BTC_PortNo, htonl(dwIP));
	}
	else {	// connected
		BTCnode.Disconnect();
	}
	// Set socket status
	TCHAR sz1[MAX_LOADSTRING];
	int i;
	if (BTCnode.GetSocketStatus()==5) {	// fully connected
		i=IDS_SS_CONNECTED;
	}
	else {
		if (BTCnode.GetSocketStatus()>3) {  // connecting...
			i=IDS_SS_CONNECTING;
		}
		else
			i=IDS_SS_NOTHING;
	}
	LoadString(hInst, i, sz1, MAX_LOADSTRING);
	SetWindowText(hStaticSocketStatus, sz1);
	InvalidateRect(hWndMain, &rcSocketStatus, TRUE);
	// Set database connect button
	HWND hCtrl = GetDlgItem(hWndMain, IDC_BTNSQLCONNECT);
	EnableWindow(hCtrl, (BTCnode.GetSocketStatus()<4));
	// Set socket connect button
	hCtrl = GetDlgItem(hWndMain, IDC_BTNBTCCONNECT);
	if (BTCnode.GetSocketStatus()>3)
		i=IDS_DISCONNECT;
	else
		i=IDS_CONNECT;
	LoadString(hInst, i, sz1, MAX_LOADSTRING);
	SetWindowText(hCtrl, sz1);
	PostMessage(hWndMain, myMsg_ProcessNodeStatus, 0, 0);
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND	- process the application menu & buttons
//  WM_PAINT	- Paint the main window
//  WM_DESTROY	- post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId, wmEvent;

	switch (message)
	{
	case myTCP_Messages:
		BTCnode.ProcessNotifications(hWnd, lParam);
		PostMessage(hWnd, myMsg_ProcessNodeStatus, 1, 0);	// node status could have changed
		break;

	case myMsg_ProcessReadMessage:
		BTCnode.ProcessReadMessage(hWnd, lParam);
		if (DoNonUserMessages())
			PostQuitMessage(myMsg_ProcessReadMessage);
		break;

	case myMsg_ProcessNodeStatus:
		BTCnode.ProcessNodeStatus(hWnd, lParam);
		if (DoNonUserMessages())
			PostQuitMessage(myMsg_ProcessNodeStatus);
		break;

	case myMsg_ProcessReadBuffer:
		BTCnode.ProcessReadBuffer();
		if (DoNonUserMessages())
			PostQuitMessage(myMsg_ProcessReadBuffer);
		break;

	case myMsg_AppendOutBuffer:
		if (lParam!=NULL) {		// lParam should be pointer to a vector
			BTCnode.AppendOutBuffer(reinterpret_cast<std::vector<char>*>(lParam));	// append data coming from thread
			BTCnode.WriteToSocket();												// try to effectively send out data
		}
		break;

	case myMsg_ThreadFinished:		// Some worker thread signals completion
		if (lParam<0 || lParam>=WT_LAST)
			break;	// invalid lParam passed, ignore this message
		bThreadRunning[lParam]=NULL;
		bThreadAbort[lParam]=false;

		switch (lParam)
		{
		case WT_DBConnect:
			mysqlpp::Connection::thread_start();		// main thread did not make connection pool
			ProcessDbConnectFinished(wParam);
			break;
		}
		break;

	case myMsg_WaitForDBDisconnect:	// wParam=0: user wanted to disconnect from database, wParam=hDlg: load database commanded
		{
			bool b1=false;
			int i;
			// see if all worker threads finished
			for (i=WT_InvMsg; (i<WT_LAST) && (!b1) ; i++)
				b1 = b1 || bThreadRunning[i];
			if (b1) {	// some thread is still running
				DoNonUserMessages();
				PostMessage(hWnd, myMsg_WaitForDBDisconnect, wParam, lParam);
			}
			else {		// all threads finished
				if (wParam!=0) {	// extra to do: load database !
					bool bLocal = ( SendDlgItemMessage((HWND)wParam, IDC_CHECKBOX1, BM_GETSTATE, 0, 0) == BST_CHECKED);
					TCHAR sztPath[MAX_PATH];
					GetDlgItemText((HWND)wParam, IDC_PATH, sztPath, MAX_PATH);
					char szPath[MAX_PATH*2];
					WideCharToMultiByte(CP_ACP, NULL, sztPath, -1, szPath, MAX_PATH*2, NULL, NULL);
					mydb.LoadDatabase((HWND)wParam, szPath, bLocal);
					EndDialog((HWND)wParam, IDOK);
				}
				// standard stuff for disconnection (so that everything re-initialises after a database load)
				mydb.DropConnectionPool();
				mysqlpp::Connection::thread_end();
				HWND hCtrl = GetDlgItem(hWndMain, IDC_BTNSQLCONNECT);
				TCHAR sz1[MAX_LOADSTRING];
				LoadString(hInst, IDS_CONNECT, sz1, MAX_LOADSTRING);
				SetWindowText(hCtrl, sz1);
				EnableWindow(hCtrl, TRUE);								// enable button again
				LoadString(hInst, IDS_SS_NOTHING, sz1, MAX_LOADSTRING);
				SetWindowText(hStaticSQLStatus, sz1);
			}
		}
		break;

	case myMsg_BlockProcessed: // worker thread finished processing a block
		{	mysqlpp::Connection* my_conn = mydb.GrabConnection();
			mydb.CheckHeights(my_conn);													// check blocks with unknown heights
			int iBestHeight=mydb.GetBestHeightKnown(my_conn);
			if (BTCnode.GetNodeStatus()==4 && iBestHeight>=BTCnode.Peer_BlockHeight) {	// we were uploading block chain and now get to same height as peer --> we're now "up-to-date"
				BTCnode.IncNodeStatus();
				PostMessage(hWndMain, myMsg_ProcessNodeStatus, 0, MsgREPAINT);
			}
			if (iBestHeight>=BTCnode.Peer_AskedBlockHeight) {	// ask next set of blocks if previous set is finished (also when we're "up-to-date", cause long download delay can cause missing blocks)
				uint256 hash_start;
				if (mydb.GetBlockHashFromHeight(my_conn, iBestHeight, hash_start)) {
					if (BTCnode.SendMsg_GetBlocks(hash_start, uint256(0)))
						BTCnode.Peer_AskedBlockHeight+=500;			// getblocks msg succeeded to ask up to 500 blocks more
				}
			}
			TCHAR szStaticText[10];
			_itow_s(iBestHeight, szStaticText, 10, 10);
			SetWindowText(hStaticCurBlock,szStaticText);
			if (iBestHeight>BTCnode.Peer_BlockHeight)
				BTCnode.Peer_BlockHeight=iBestHeight;					// we got better block then initial block height in version msg
			_itow_s(BTCnode.Peer_BlockHeight, szStaticText, 10, 10);
			SetWindowText(hStaticMaxBlock,szStaticText);
			iBestHeight = mydb.GetSafeHeight(my_conn);
			_itow_s(iBestHeight, szStaticText, 10, 10);
			SetWindowText(hStaticProcessedBlock,szStaticText);
			iBestHeight = mydb.NrTxUnconfirmed(my_conn);
			_itow_s(iBestHeight, szStaticText, 10, 10);
			SetWindowText(hStaticUnconfirmedTxs,szStaticText);
			mydb.ReleaseConnection(my_conn);
		}
		break;

	case WM_COMMAND:
		wmId    = LOWORD(wParam);
		wmEvent = HIWORD(wParam);
		// Parse the menu selections & child controls:
		switch (wmId)
		{
		case IDM_ABOUT:
			DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
			break;
		case IDM_DUMPDB:
			DialogBox(hInst, MAKEINTRESOURCE(IDD_DUMPDB), hWnd, DumpDB_Dlg);
			break;
		case IDM_LOADDB:
			DialogBox(hInst, MAKEINTRESOURCE(IDD_LOADDB), hWnd, LoadDB_Dlg);
			break;
		case IDM_EXIT:
			DestroyWindow(hWnd);
			break;
		case IDC_BTNSQLCONNECT:		// user clicked our (dis)connect button for the database
			ProcessBtnSQLConnect(0);
			break;
		case IDC_BTNBTCCONNECT:		// user clicked out (dis)connect button for the bitcoin network
			ProcessBtnBTCConnect();
			break;

		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		break;

	case WM_TIMER:	// ping timer or stalled connection check
		{
			mysqlpp::Connection* my_conn = mydb.GrabConnection();
			int iBestHeight=mydb.GetBestHeightKnown(my_conn);
			if (iBestHeight==BTCnode.iTimerPreviousBlockHeight) {	// stalled connection
				if (BTCnode.GetNodeStatus()>4) 		// up-to-date chain ? 
					iBestHeight=mydb.GetSafeHeight(my_conn);	// re-ask everything from safe height (to prevent stall in block fork)
				if (iBestHeight>0) {
					uint256 h1;
					mydb.GetBlockHashFromHeight(my_conn, iBestHeight, h1);
					BTCnode.SendMsg_GetBlocks(h1, uint256(0) );
				}
			}
			else {	// everything is downloading normally, give a sign of life
				BTCnode.iTimerPreviousBlockHeight=iBestHeight;		// keep track of previous height for next WM_TIMER
				BTCnode.SendMsg_Ping();
			}
			mydb.ReleaseConnection(my_conn);
		}
		break;
	case WM_CTLCOLORSTATIC:		// about to draw static control
		if ( (HWND)lParam == hStaticNodeStatus) {	// node status will be drawn
			SetBkColor((HDC) wParam, (COLORREF) GetSysColor(COLOR_WINDOW));
			SetBkMode((HDC) wParam, TRANSPARENT);
			int i1 = BTCnode.GetNodeStatus();
			if (i1<3)
				SetTextColor((HDC) wParam, RGB(255,0,0));
			else
				if (i1<5)
					SetTextColor((HDC) wParam, RGB(255,165,0));
				else
					SetTextColor((HDC) wParam, RGB(0,0,255));
		}
		else
			return DefWindowProc(hWnd, message, wParam, lParam);
		break;
	case WM_INITMENUPOPUP:
		if (HIWORD(lParam)==FALSE && LOWORD(lParam)==0 ) {	// File submenu
			UINT uiState = ( mydb.IsConnected() && BTCnode.GetSocketStatus()<4 ) ? MF_BYCOMMAND|MF_ENABLED : MF_BYCOMMAND|MF_GRAYED;
			EnableMenuItem((HMENU)wParam, IDM_DUMPDB, uiState);
			EnableMenuItem((HMENU)wParam, IDM_LOADDB, uiState);
		}
		break;
	case WM_PAINT:
		PaintMainWindow(hWnd);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

INT_PTR CALLBACK DumpDB_Dlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	TCHAR szt[MAX_LOADSTRING];
	switch (message)
	{
	case WM_INITDIALOG:
		GetPrivateProfileString(L"mysql", L"path", L"c:\\", szt, MAX_LOADSTRING, szINIFile);
		SetDlgItemText(hDlg, IDC_PATH, szt);
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_BROWSE:	// dialog to browse for path
			{
				IFileOpenDialog *fDlg;
				HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fDlg));
				if (SUCCEEDED(hr)) {
					hr = fDlg->SetOptions(FOS_NOCHANGEDIR | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
					if (SUCCEEDED(hr)) {
						hr = fDlg->Show(hDlg);
						if (SUCCEEDED(hr)) {	// got path
							IShellItem *psiResult;
                            hr = fDlg->GetResult(&psiResult);
                            if (SUCCEEDED(hr)) {  // extract path
								PWSTR pszFilePath = NULL;
								hr = psiResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                                if (SUCCEEDED(hr)) {
									wchar_t *wi = _tcsrchr(pszFilePath, '\\');
									if (wi!=NULL)
										*wi=0;
									SetDlgItemText(hDlg, IDC_PATH, pszFilePath);
                                    CoTaskMemFree(pszFilePath);
                                }
                                psiResult->Release();
							}
						}
					}
				}
				return (INT_PTR)TRUE;
			}

		case IDOK:	// this is actually the "dump" button
			{
				TCHAR sztPath[MAX_PATH];
				GetDlgItemText(hDlg, IDC_PATH, sztPath, MAX_PATH);
				if (PathIsDirectory(sztPath)==FILE_ATTRIBUTE_DIRECTORY) {
					char szPath[MAX_PATH*2];
					WideCharToMultiByte(CP_ACP, NULL, sztPath, -1, szPath, MAX_PATH*2, NULL, NULL);
					mydb.DumpDatabase(hDlg, szPath);
					EndDialog(hDlg, LOWORD(wParam));
				}
				else
					ShowError(IDS_Err_PathNotFound);
			}
			return (INT_PTR)TRUE;

		case IDCANCEL:
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

INT_PTR CALLBACK LoadDB_Dlg(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	TCHAR szt[MAX_LOADSTRING];
	switch (message)
	{
	case WM_INITDIALOG:
		GetPrivateProfileString(L"mysql", L"path", L"c:\\", szt, MAX_LOADSTRING, szINIFile);
		SetDlgItemText(hDlg, IDC_PATH, szt);
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDC_BROWSE:	// dialog to browse for path
			{
				IFileOpenDialog *fDlg;
				HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fDlg));
				if (SUCCEEDED(hr)) {
					hr = fDlg->SetOptions(FOS_NOCHANGEDIR | FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
					if (SUCCEEDED(hr)) {
						hr = fDlg->Show(hDlg);
						if (SUCCEEDED(hr)) {	// got path
							IShellItem *psiResult;
                            hr = fDlg->GetResult(&psiResult);
                            if (SUCCEEDED(hr)) {  // extract path
								PWSTR pszFilePath = NULL;
								hr = psiResult->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);
                                if (SUCCEEDED(hr)) {
									wchar_t *wi = _tcsrchr(pszFilePath, '\\');
									if (wi!=NULL)
										*wi=0;
									SetDlgItemText(hDlg, IDC_PATH, pszFilePath);
                                    CoTaskMemFree(pszFilePath);
                                }
                                psiResult->Release();
							}
						}
					}
				}
				return (INT_PTR)TRUE;
			}

		case IDOK:	// this is actually the "load" button
			{
				TCHAR sztPath[MAX_PATH];
				GetDlgItemText(hDlg, IDC_PATH, sztPath, MAX_PATH);
				if (PathIsDirectory(sztPath)==FILE_ATTRIBUTE_DIRECTORY) {
					ProcessBtnSQLConnect((WPARAM)hDlg);	// simulate a database disconnect, pass handle to load database when threads finish
				}
				else
					ShowError(IDS_Err_PathNotFound);
			}
			return (INT_PTR)TRUE;

		case IDCANCEL:
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

// Creating child windows in main window
LRESULT CreateChildsInMain(HWND hWnd)
{
	// string buffers for each child window
	TCHAR szChildText[MAX_LOADSTRING];
	// "connection" group button
	LoadString(hInst, IDS_CONNECTION, szChildText, MAX_LOADSTRING);
	if (!CreateWindow(WC_BUTTON, szChildText, WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 10, 10, 400, 100+70, hWnd, 0, hInst, 0))
		return -1;
    // Create the edit control for database
	GetPrivateProfileString(L"mysql", L"server", L"127.0.0.1:3306", szChildText, MAX_LOADSTRING, szINIFile);
	if (!CreateWindow(WC_EDIT, szChildText, WS_CHILD | WS_TABSTOP | WS_BORDER | WS_VISIBLE, 120, 34, 180, 24, hWnd, (HMENU)IDC_EDITSQL, hInst, 0))
		return -1;

    INITCOMMONCONTROLSEX icex;
	HWND hwndCtrl;
    // Ensure that the common control DLL is loaded. 
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC  = ICC_INTERNET_CLASSES ;
    InitCommonControlsEx(&icex);
	// our button "Connect"
	LoadString(hInst, IDS_CONNECT, szChildText, MAX_LOADSTRING);	
	if (!CreateWindow(WC_BUTTON, szChildText, WS_CHILD | WS_TABSTOP | WS_VISIBLE | BS_PUSHBUTTON, 310, 34, 90, 25, hWnd, (HMENU)IDC_BTNSQLCONNECT, hInst, 0))
		return -1;
	if (!CreateWindow(WC_BUTTON, szChildText, WS_CHILD | WS_TABSTOP | WS_VISIBLE | BS_PUSHBUTTON, 280, 34+70, 90, 25, hWnd, (HMENU)IDC_BTNBTCCONNECT, hInst, 0))
		return -1;
	// IPAddress control for bitcoin network node
	hwndCtrl=CreateWindow(WC_IPADDRESS, L"", WS_CHILD | WS_TABSTOP | WS_BORDER | WS_VISIBLE, 40+50, 34+70, 150, 24, hWnd, (HMENU)IDC_EDITIP, hInst, 0);
	if (!hwndCtrl)
		return -1;
	// read IP address from configuration file and set it
	GetPrivateProfileString(L"bitcoin", L"node", L"127.0.0.1", szChildText, MAX_LOADSTRING, szINIFile);
	char szIP[MAX_LOADSTRING*2];
	WideCharToMultiByte(CP_ACP, 0, szChildText, -1, szIP, MAX_LOADSTRING*2, NULL, NULL);
	unsigned long uiIP = inet_addr(szIP);
	if (uiIP==INADDR_NONE || uiIP==INADDR_ANY)
		uiIP = MAKEIPADDRESS(127,0,0,1);
	else
		uiIP = htonl(uiIP);					// reverse endianness
	SendMessage(hwndCtrl, IPM_SETADDRESS, 0, uiIP);

	LoadString(hInst, IDS_SS_NOTHING, szChildText, MAX_LOADSTRING);
	hStaticSQLStatus = CreateWindow(WC_STATIC, szChildText, WS_CHILD | WS_VISIBLE | SS_SIMPLE, 132, 72, 180, 25, hWnd, (HMENU)IDC_STATICDBSTAT, hInst, 0);
	if (!hStaticSQLStatus)
		return -1;
	hStaticSocketStatus = CreateWindow(WC_STATIC, szChildText, WS_CHILD | WS_VISIBLE | SS_SIMPLE, 112, 72+70, 200, 25, hWnd, (HMENU)IDC_STATICDBSTAT, hInst, 0);
	if (!hStaticSocketStatus)
		return -1;
	LoadString(hInst, IDS_BLOCKCHAIN, szChildText, MAX_LOADSTRING);
	if (!CreateWindow(WC_BUTTON, szChildText, WS_CHILD | WS_VISIBLE | BS_GROUPBOX, 10, 130+70, 400, 145, hWnd, 0, hInst, 0))
		return -1;
	hStaticCurBlock = CreateWindow(WC_STATIC, NULL, WS_CHILD | WS_VISIBLE | SS_RIGHT, 68, 155+70, 60, 25, hWnd, (HMENU)IDC_STATICCURBLOCK, hInst, 0);
	if (!hStaticCurBlock)
		return -1;
	hStaticMaxBlock = CreateWindow(WC_STATIC, NULL, WS_CHILD | WS_VISIBLE | SS_LEFT, 140, 155+70, 60, 25, hWnd, (HMENU)IDC_STATICMAXBLOCK, hInst, 0);
	if (!hStaticMaxBlock)
		return -1;
	hStaticProcessedBlock = CreateWindow(WC_STATIC, NULL, WS_CHILD | WS_VISIBLE | SS_LEFT, 180, 215+68, 60, 25, hWnd, (HMENU)IDC_STATICPROCESSBLOCK, hInst, 0);
	if (!hStaticProcessedBlock)
		return -1;
	hStaticUnconfirmedTxs = CreateWindow(WC_STATIC, NULL, WS_CHILD | WS_VISIBLE | SS_LEFT, 245, 238+68, 60, 25, hWnd, (HMENU)IDC_STATICUNCONFIRMEDTXS, hInst, 0);
	if (!hStaticProcessedBlock)
		return -1;
	LoadString(hInst, IDS_NS_INIT, szChildText, MAX_LOADSTRING);
	hStaticNodeStatus = CreateWindow(WC_STATIC, szChildText, WS_CHILD | WS_VISIBLE | SS_SIMPLE, 112, 185+70, 200, 25, hWnd, (HMENU)IDC_STATICNODESTAT, hInst, 0);
	if (!hStaticNodeStatus)
		return -1;
	return 0;
}

void ShowError(int iError)
{
	TCHAR szText[MAX_LOADSTRING];
	TCHAR szTitle[MAX_LOADSTRING];
	LoadString(hInst, IDS_Err_Caption, szTitle, MAX_LOADSTRING);
	LoadString(hInst, iError, szText, MAX_LOADSTRING);
	MessageBox(hWndMain, szText, szTitle, MB_OK);
}

void ShowError(int iError, int iInformation)
{
	TCHAR szText[MAX_LOADSTRING];
	TCHAR szTextRaw[MAX_LOADSTRING];
	TCHAR szTitle[MAX_LOADSTRING];
	LoadString(hInst, IDS_Err_Caption, szTitle, MAX_LOADSTRING);
	LoadString(hInst, iError, szTextRaw, MAX_LOADSTRING);
	swprintf_s(szText, MAX_LOADSTRING, szTextRaw, iInformation);
	MessageBox(hWndMain, szText, szTitle, MB_OK);
}

void ShowError(int iError, TCHAR *szInfo)
{
	int i=wcslen(szInfo);
	TCHAR *szText = new TCHAR[MAX_LOADSTRING+i];
	TCHAR szTextRaw[MAX_LOADSTRING];
	TCHAR szTitle[MAX_LOADSTRING];
	LoadString(hInst, IDS_Err_Caption, szTitle, MAX_LOADSTRING);
	LoadString(hInst, iError, szTextRaw, MAX_LOADSTRING);
	swprintf_s(szText, MAX_LOADSTRING+i-1, szTextRaw, szInfo);
	MessageBox(hWndMain, szText, szTitle, MB_OK);
	delete[] szText;
}

void ShowError(int iError, const char *szInfo)
{
	size_t newsize = strlen(szInfo)+1;
	TCHAR * wcstring = new TCHAR[newsize];				// buffer to convert to TCHAR
	size_t convertedChars = 0;
    mbstowcs_s(&convertedChars, wcstring, newsize, szInfo, _TRUNCATE);
	TCHAR *szText = new TCHAR[MAX_LOADSTRING+newsize];
	TCHAR szTextRaw[MAX_LOADSTRING];
	TCHAR szTitle[MAX_LOADSTRING];
	LoadString(hInst, IDS_Err_Caption, szTitle, MAX_LOADSTRING);
	LoadString(hInst, iError, szTextRaw, MAX_LOADSTRING);
	swprintf_s(szText, MAX_LOADSTRING, szTextRaw, wcstring);
	MessageBox(hWndMain, szText, szTitle, MB_OK);
	delete[] szText;
	delete[] wcstring;
}
