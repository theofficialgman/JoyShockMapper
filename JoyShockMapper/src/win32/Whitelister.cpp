// Source: https://stackoverflow.com/questions/1011339/how-do-you-make-a-http-request-with-c
#include "Whitelister.h"

#include <Windows.h>
#include <WinSock2.h>
#include <sstream>

#pragma comment(lib,"ws2_32.lib")

constexpr uint16_t HID_GUARDIAN_PORT = 26762;

// Keep windows types outside of h file
static SOCKET connectToServer(const std::string & szServerName, WORD portNum)
{
	struct hostent *hp;
	unsigned int addr;
	struct sockaddr_in server;
	SOCKET conn;

	conn = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (conn == INVALID_SOCKET)
		return NULL;

	if (inet_addr(szServerName.c_str()) == INADDR_NONE)
	{
		hp = gethostbyname(szServerName.c_str());
	}
	else
	{
		addr = inet_addr(szServerName.c_str());
		hp = gethostbyaddr((char*)&addr, sizeof(addr), AF_INET);
	}

	if (hp == NULL)
	{
		closesocket(conn);
		return NULL;
	}

	server.sin_addr.s_addr = *((unsigned long*)hp->h_addr);
	server.sin_family = AF_INET;
	server.sin_port = htons(portNum);
	if (connect(conn, (struct sockaddr*)&server, sizeof(server)))
	{
		closesocket(conn);
		return NULL;
	}
	return conn;
}

bool Whitelister::IsHIDCerberusRunning()
{
	// Source: https://stackoverflow.com/questions/7808085/how-to-get-the-status-of-a-service-programmatically-running-stopped
	SC_HANDLE theService, scm;
	SERVICE_STATUS_PROCESS ssStatus;
	DWORD dwBytesNeeded;

	scm = OpenSCManagerA(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
	if (!scm) {
		return 0;
	}

	theService = OpenService(scm, L"HidCerberus.Srv", SERVICE_QUERY_STATUS);
	if (!theService) {
		CloseServiceHandle(scm);
		return 0;
	}

	auto result = QueryServiceStatusEx(theService, SC_STATUS_PROCESS_INFO,
		reinterpret_cast<LPBYTE>(&ssStatus), sizeof(SERVICE_STATUS_PROCESS),
		&dwBytesNeeded);

	CloseServiceHandle(theService);
	CloseServiceHandle(scm);

	return result == TRUE && ssStatus.dwCurrentState == SERVICE_RUNNING;
}

bool Whitelister::ShowHIDCerberus()
{
	STARTUPINFOA startupInfo;
	PROCESS_INFORMATION procInfo;
	memset(&startupInfo, 0, sizeof(STARTUPINFOA));
	memset(&procInfo, 0, sizeof(PROCESS_INFORMATION));
	auto pid = GetCurrentProcessId();
	auto success = CreateProcessA(NULL, R"(cmd /C "start http://localhost:26762/")", NULL, NULL, FALSE, NORMAL_PRIORITY_CLASS, NULL, NULL, &startupInfo, &procInfo);
	if (success == TRUE)
	{
		CloseHandle(procInfo.hProcess);
		CloseHandle(procInfo.hThread);
		return true;
	}
	auto err = GetLastError();
	return false;
}

bool Whitelister::Add(std::string *optErrMsg)
{
	if (!_whitelisted && IsHIDCerberusRunning())
	{
		UINT64 pid = GetCurrentProcessId();
		std::stringstream ss;
		ss << R"(http://localhost/api/v1/hidguardian/whitelist/add/)" << pid;
		auto result = SendToHIDGuardian(ss.str());

		if (result.compare(R"(["OK"])") == 0)
		{
			_whitelisted = true;
			return true;
		}

		if (optErrMsg)
			*optErrMsg = result;
	}
	return false;
}

bool Whitelister::Remove(std::string *optErrMsg)
{
	if (_whitelisted && IsHIDCerberusRunning())
	{
		UINT64 pid = GetCurrentProcessId();
		std::stringstream ss;
		ss << R"(http://localhost/api/v1/hidguardian/whitelist/remove/)" << pid;
		auto result = SendToHIDGuardian(ss.str());
		if (result.compare(R"(["OK"])") == 0)
		{
			_whitelisted = false;
			return true;
		}

		if (optErrMsg)
			*optErrMsg = result;
	}
	return false;
}

std::string Whitelister::SendToHIDGuardian(std::string command)
{
	long fileSize = -1;
	WSADATA wsaData;
	std::string memBuffer, headerBuffer;
	
	if (WSAStartup(0x101, &wsaData) == 0)
	{
		memBuffer = readUrl2(command, fileSize, &headerBuffer);
	
		WSACleanup();
	}
	return memBuffer;
}

std::string Whitelister::readUrl2(std::string &szUrl, long &bytesReturnedOut, std::string *headerOut)
{
	constexpr size_t bufSize = 512;
	std::string readBuffer(bufSize, '\0');
	std::string sendBuffer(bufSize, '\0');
	std::stringstream tmpBuffer;
	std::string result;
	SOCKET conn;
	std::string server, filepath, filename;
	long totalBytesRead, thisReadSize, headerLen;

	mParseUrl(szUrl, server, filepath, filename);

	///////////// step 1, connect //////////////////////
	conn = connectToServer(server, HID_GUARDIAN_PORT);

	///////////// step 2, send GET request /////////////
	tmpBuffer << "GET " << filepath << " HTTP/1.0" << "\r\n" << "Host: " << server << "\r\n\r\n";
	sendBuffer = tmpBuffer.str();
	send(conn, sendBuffer.c_str(), sendBuffer.length(), 0);
	
	///////////// step 3 - get received bytes ////////////////
	// Receive until the peer closes the connection
	totalBytesRead = 0;
	do
	{
		thisReadSize = recv(conn, &readBuffer[0], readBuffer.size(), 0);

		if (thisReadSize > 0)
		{
			result.append(readBuffer);
			totalBytesRead += thisReadSize;
		}
	} while (thisReadSize > 0);

	headerLen = getHeaderLength(&result[0]);
	if (headerOut)
	{
		*headerOut = result.substr(0, headerLen);
	}
	result.erase(0, headerLen);
	result.resize(strlen(result.c_str()));
		
	bytesReturnedOut = totalBytesRead - headerLen;
	closesocket(conn);
	return result;
}

void Whitelister::mParseUrl(std::string url, std::string &serverName, std::string &filepath, std::string &filename)
{
	std::string::size_type n;

	if (url.substr(0, 7) == "http://")
		url.erase(0, 7);

	if (url.substr(0, 8) == "https://")
		url.erase(0, 8);

	n = url.find('/');

	if (n != std::string::npos)
	{
		serverName = url.substr(0, n);
		filepath = url.substr(n);
		n = filepath.rfind('/');
		filename = filepath.substr(n + 1);
	}
	else
	{
		serverName = url;
		filepath = "/";
		filename = "";
	}
}

int Whitelister::getHeaderLength(char *content)
{
	const char *srchStr1 = "\r\n\r\n", *srchStr2 = "\n\r\n\r";
	char *findPos;
	int ofset = -1;

	findPos = strstr(content, srchStr1);
	if (findPos != NULL)
	{
		ofset = findPos - content;
		ofset += strlen(srchStr1);
	}

	else
	{
		findPos = strstr(content, srchStr2);
		if (findPos != NULL)
		{
			ofset = findPos - content;
			ofset += strlen(srchStr2);
		}
	}
	return ofset;
}
