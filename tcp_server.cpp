#define _CRT_SECURE_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
using namespace std;
#include <winsock2.h>
#include <string.h>
#include <time.h>
#include <string>
#include <dirent.h>
#include <fstream>
#include <assert.h>

#pragma comment(lib,"ws2_32.lib")

struct Header
{
	char code[30];
	char data[255];
	char msg[4];
	int len;
};

struct SocketState
{
	SOCKET id;
	int	recv;
	int	send;
	int sendSubType;
	char buffer[2000];
	int len;
	int reqInd;
	bool isBrowser;
};

const int WEB_PORT = 27015;
const int MAX_SOCKETS = 60;
const int EMPTY = 0;
const int LISTEN = 1;
const int RECEIVE = 2;
const int IDLE = 3;
const int SEND = 4;
const int GET = 1;
const int PUT = 2;
const int HEAD = 3;

bool addSocket(SOCKET id, int what);
void removeSocket(int index);
void acceptConnection(int index);
void receiveMessage(int index);
void sendMessage(int index);
void setRequest(char*, char*);
void getFileName(char*, char*, int);
bool isFileExists(string);
string getFilePath(char*, char*);
void readFile(char*, char[]);
void buildHeader(Header, char*, int);
bool isFromBrowser(char*);
string getHeaderData(char*);
int fileCount = 0;

struct SocketState sockets[MAX_SOCKETS] = { 0 };
int socketsCount = 0;


void main()
{
	// Initialize Winsock.
	WSAData wsaData;

	if (NO_ERROR != WSAStartup(MAKEWORD(2, 2), &wsaData))
	{
		cout << "Web Server: Error at WSAStartup()\n";
		return;
	}

	// Server side:
	// Create and bind a socket to an internet address.
	// Listen through the socket for incoming connections.
	SOCKET listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (INVALID_SOCKET == listenSocket)
	{
		cout << "Web Server: Error at socket(): " << WSAGetLastError() << endl;
		WSACleanup();
		return;
	}

	sockaddr_in serverService;

	serverService.sin_family = AF_INET;
	serverService.sin_addr.s_addr = INADDR_ANY;
	serverService.sin_port = htons(WEB_PORT);

	if (SOCKET_ERROR == bind(listenSocket, (SOCKADDR*)& serverService, sizeof(serverService)))
	{
		cout << "Web Server: Error at bind(): " << WSAGetLastError() << endl;
		closesocket(listenSocket);
		WSACleanup();
		return;
	}

	if (SOCKET_ERROR == listen(listenSocket, 5))
	{
		cout << "Web Server: Error at listen(): " << WSAGetLastError() << endl;
		closesocket(listenSocket);
		WSACleanup();
		return;
	}
	addSocket(listenSocket, LISTEN);

	// Accept connections and handles them one by one.
	while (true)
	{
		fd_set waitRecv;
		FD_ZERO(&waitRecv);
		for (int i = 0; i < MAX_SOCKETS; i++)
		{
			if ((sockets[i].recv == LISTEN) || (sockets[i].recv == RECEIVE))
				FD_SET(sockets[i].id, &waitRecv);
		}

		fd_set waitSend;
		FD_ZERO(&waitSend);
		for (int i = 0; i < MAX_SOCKETS; i++)
		{
			if (sockets[i].send == SEND)
				FD_SET(sockets[i].id, &waitSend);
		}

		int nfd;
		nfd = select(0, &waitRecv, &waitSend, NULL, NULL);
		if (nfd == SOCKET_ERROR)
		{
			cout << "Web Server: Error at select(): " << WSAGetLastError() << endl;
			WSACleanup();
			return;
		}

		for (int i = 0; i < MAX_SOCKETS && nfd > 0; i++)
		{
			if (FD_ISSET(sockets[i].id, &waitRecv))
			{
				nfd--;
				switch (sockets[i].recv)
				{
				case LISTEN:
					acceptConnection(i);
					break;

				case RECEIVE:
					receiveMessage(i);
					break;
				}
			}
		}

		for (int i = 0; i < MAX_SOCKETS && nfd > 0; i++)
		{
			if (FD_ISSET(sockets[i].id, &waitSend))
			{
				nfd--;
				switch (sockets[i].send)
				{
				case SEND:
					sendMessage(i);
					break;
				}
			}
		}
	}

	// Closing connections and Winsock.
	cout << "Web Server: Closing Connection.\n";
	closesocket(listenSocket);
	WSACleanup();
}

bool addSocket(SOCKET id, int what)
{
	for (int i = 0; i < MAX_SOCKETS; i++)
	{
		if (sockets[i].recv == EMPTY)
		{
			sockets[i].id = id;
			sockets[i].recv = what;
			sockets[i].send = IDLE;
			sockets[i].len = 0;
			socketsCount++;
			return (true);
		}
	}
	return (false);
}

void removeSocket(int index)
{
	sockets[index].recv = EMPTY;
	sockets[index].send = EMPTY;
	socketsCount--;
}

void acceptConnection(int index)
{
	SOCKET id = sockets[index].id;
	//Address of sending partner
	struct sockaddr_in from;		
	int fromLen = sizeof(from);

	SOCKET msgSocket = accept(id, (struct sockaddr*) & from, &fromLen);
	if (INVALID_SOCKET == msgSocket)
	{
		cout << "Web Server: Error at accept(): " << WSAGetLastError() << endl;
		return;
	}
	cout << "Web Server: Client " << inet_ntoa(from.sin_addr) << ":" << ntohs(from.sin_port) << " is connected." << endl;

	// Set the socket to be in non-blocking mode.
	unsigned long flag = 1;
	if (ioctlsocket(msgSocket, FIONBIO, &flag) != 0)
	{
		cout << "Web Server: Error at ioctlsocket(): " << WSAGetLastError() << endl;
	}

	if (addSocket(msgSocket, RECEIVE) == false)
	{
		cout << "\t\tToo many connections, dropped!\n";
		closesocket(id);
	}
	return;
}

void receiveMessage(int index)
{
	SOCKET msgSocket = sockets[index].id;
	int len = sockets[index].len;
	int bytesRecv = recv(msgSocket, &sockets[index].buffer[len], sizeof(sockets[index].buffer) - len, 0);

	if (SOCKET_ERROR == bytesRecv)
	{
		cout << "Web Server: Error at recv(): " << WSAGetLastError() << endl;
		closesocket(msgSocket);
		removeSocket(index);
		return;
	}
	if (bytesRecv == 0)
	{
		closesocket(msgSocket);
		removeSocket(index);
		return;
	}
	else
	{
		sockets[index].buffer[len + bytesRecv] = '\0';	
		sockets[index].len += bytesRecv;
		char request[1500];
		strcpy(request, sockets[index].buffer);

		if (sockets[index].len > 0)
		{
			//splits the string into separate words. divided by space
			char *token = strtok(request, " ");
			while (token != NULL)
			{
				token = strtok(NULL, " ");
			}			
			
			if (strcmp(request, "GET") == 0)
			{
				sockets[index].send = SEND;
				sockets[index].sendSubType = GET;
				sockets[index].reqInd = 5;
				return;
			}
			else if (strcmp(request, "PUT") == 0)
			{
				sockets[index].send = SEND;
				sockets[index].sendSubType = PUT;
				sockets[index].reqInd = 5;
				return;
			}
			else if (strcmp(request, "HEAD") == 0)
			{
				sockets[index].send = SEND;
				sockets[index].sendSubType = HEAD;
				sockets[index].reqInd = 6;
			}
			memset(request, 0, sizeof(request));
		}
	}

}

void sendMessage(int index)
{
	Header header;
	int bytesSent = 0;
	char sendBuff[2500];
	char fileName[2500];
	SOCKET msgSocket = sockets[index].id;
	char files[100] = "./Files/";
	
	if (sockets[index].sendSubType == GET)
	{
		char tmpBuffer[1500];
		char fileName[1500];
		char tmp[1500];
		strcpy(tmpBuffer, sockets[index].buffer);
		int count = 0;

		char *token = strtok(tmpBuffer, " ");
		while (token != NULL && count < 2)
		{
			count++;
			strcpy(tmp, token);
			memcpy(fileName, &token[1], sizeof(tmp) - 1);
			token = strtok(NULL, " ");
		}
		
		if (fileName[0] == '\0')
		{			
			strcpy(fileName, "index.html");
		}		

		DIR* dir;
		struct dirent* ent;
		if ((dir = opendir("./Files")) != NULL) {
			/* print all the files and directories within directory */
			while ((ent = readdir(dir)) != NULL) {
				if (strcmp(ent->d_name, fileName) == 0)
				{
					fileCount++;
					strcat(files, fileName);
					FILE *f = fopen(files, "r");
					assert(f);
					fseek(f, 0, SEEK_END);
					long length = ftell(f);
					fseek(f, 0, SEEK_SET);
					char *buffer = (char *)malloc(length + 1);
					buffer[length] = '\0';
					fread(buffer, 1, length, f);
					memset(files, 0, sizeof(files));
					strcpy(files, "./Files/");
					fclose(f);

					strcpy(header.code, "200 OK");
					strcpy(header.data, buffer);
					header.len = strlen(header.data);
					string fullHeader;
					string code = header.code;
					string data = header.data;
					string len = to_string(header.len);
					fullHeader = "HTTP/1.1 " + code + "\nContent-Type: text/plain\nContent-Length: " + len + "\n\n" + data;
					strcpy(sendBuff, fullHeader.c_str());
					int len1 = strlen(sendBuff);
					sendBuff[len1] = '\0';
				}				
			}
		}

		if (fileCount == 0)
		{
			strcpy(header.code, "404 Not Found");
			header.data[0] = '\0';
			header.len = 0;
			string fullHeader;
			string code = header.code;
			string data = header.data;
			string len = to_string(header.len);
			fullHeader = "HTTP/1.1 " + code + "\nContent-Type: text/plain\nContent-Length: " + len + "\n\n" + data;
			strcpy(sendBuff, fullHeader.c_str());
			int len1 = strlen(sendBuff);
			sendBuff[len1] = '\0';
		}

		fileCount = 0;
		closedir(dir);

		sockets[index].send = IDLE;		
	}

	else if (sockets[index].sendSubType == HEAD)
	{
		char tmpBuffer[1500];
		char fileName[1500];
		char tmp[1500];
		strcpy(tmpBuffer, sockets[index].buffer);
		int count = 0;

		char *token = strtok(tmpBuffer, " ");
		while (token != NULL && count < 2)
		{
			count++;
			strcpy(tmp, token);
			memcpy(fileName, &token[1], sizeof(tmp) - 1);
			token = strtok(NULL, " ");
		}

		if (fileName[0] == '\0')
		{
			strcpy(fileName, "index.html");
		}

		DIR* dir;
		struct dirent* ent;
		if ((dir = opendir("./Files")) != NULL) {
			/* print all the files and directories within directory */
			while ((ent = readdir(dir)) != NULL) {
				if (strcmp(ent->d_name, fileName) == 0)
				{
					fileCount++;
					strcat(files, fileName);
					FILE *f = fopen(files, "r");
					assert(f);
					fseek(f, 0, SEEK_END);
					long length = ftell(f);
					fseek(f, 0, SEEK_SET);
					char *buffer = (char *)malloc(length + 1);
					buffer[length] = '\0';
					fread(buffer, 1, length, f);
					memset(files, 0, sizeof(files));
					strcpy(files, "./Files/");
					fclose(f);

					strcpy(header.code, "200 OK");
					strcpy(header.data, buffer);
					header.len = strlen(header.data);
					string fullHeader;
					string code = header.code;
					string data = header.data;
					string len = to_string(header.len);
					fullHeader = "HTTP/1.1 " + code + "\nContent-Type: text/plain\nContent-Length: " + len + "\n\n" + data;
					strcpy(sendBuff, fullHeader.c_str());
					int len1 = strlen(sendBuff);
					sendBuff[len1] = '\0';
				}
			}
		}

		if (fileCount == 0)
		{
			strcpy(header.code, "404 Not Found");
			header.data[0] = '\0';
			header.len = 0;
			string fullHeader;
			string code = header.code;
			string data = header.data;
			string len = to_string(header.len);
			fullHeader = "HTTP/1.1 " + code + "\nContent-Type: text/plain\nContent-Length: " + len + "\n\n" + data;
			strcpy(sendBuff, fullHeader.c_str());
			int len1 = strlen(sendBuff);
			sendBuff[len1] = '\0';
		}

		fileCount = 0;
		closedir(dir);

		sockets[index].send = IDLE;
	}
	else if (sockets[index].sendSubType == PUT)
	{
		char tmpBuffer[1500];
		char fileName[1500];
		char tmp[1500];
		strcpy(tmpBuffer, sockets[index].buffer);
		int count = 0;

		char *token = strtok(tmpBuffer, " ");
		while (token != NULL && count < 2)
		{
			count++;
			strcpy(tmp, token);
			memcpy(fileName, &token[1], sizeof(tmp) - 1);
			token = strtok(NULL, " ");
		}

		if (fileName[0] == '\0')
		{
			strcpy(fileName, "index.html");
		}

		DIR* dir;
		struct dirent* ent;
		if ((dir = opendir("./Files")) != NULL) {
			/* print all the files and directories within directory */
			while ((ent = readdir(dir)) != NULL) {
				if (strcmp(ent->d_name, fileName) == 0)
				{
					fileCount++;
					strcat(files, fileName);
					strcpy(header.code, "204 No Content");
					remove(files);
				}
			}
			if (fileCount == 0)
			{
				strcpy(header.code, "201 Created");
			}			
		}

		fstream file;
		file.open(files, ios::out);

		if (!files)
		{
			strcpy(sendBuff, "File could not be uploaded.");
		}
		else 
		{
			string data = string(getHeaderData(sockets[index].buffer));
			file << data;
			header.data[0] = '\0';
			header.len = strlen(data.c_str());
			string fullHeader;
			string code = header.code;
			string dataH = header.data;
			string len = to_string(header.len);
			fullHeader = "HTTP/1.1 " + code + "\nContent-Type: text/plain\nContent-Length: " + len + "\n\n" + data;
			strcpy(sendBuff, fullHeader.c_str());
			int len1 = strlen(sendBuff);
			sendBuff[len1] = '\0';
		}
	}

	bytesSent = send(msgSocket, sendBuff, (int)strlen(sendBuff), 0);
	if (SOCKET_ERROR == bytesSent)
	{
		cout << "Web Server: Error at send(): " << WSAGetLastError() << endl;
		return;
	}

	cout << "Web Server: Sent: " << bytesSent << "\\" << strlen(sendBuff) << " bytes of \"" << sendBuff << "\" message.\n";

	if (!sockets[index].isBrowser)
	{
		memset(sockets[index].buffer, 0, sizeof(sockets[index].buffer));
		sockets[index].len = 0;
		sockets[index].send = IDLE;
	}
	else
	{
		closesocket(msgSocket);
		removeSocket(index);
	}
}

void setRequest(char* rec, char* req) {
	int i = 0, j = 0;
	while (rec[i] != ' ')
		req[j++] = rec[i++];
	req[j] = '\0';
}

void getFileName(char* file, char* buf, int ind) {
	int i = 0;
	while (buf[ind] != ' ' && buf[ind] != '\0' && buf[ind] != '\n')
		file[i++] = buf[ind++];
	file[i] = '\0';
}

string getFilePath(char* path, char* fileName) {
	int len = strlen(path);
	while (path[len] != '\\')
		path[len--] = '\0';
	len++;
	int i = 0;
	while (fileName[i] != '\0')
		path[len++] = fileName[i++];
	path[len] = '\0';
	return string(path);
}

bool isFileExists(string fileName) {
	struct stat buffer;
	return (stat(fileName.c_str(), &buffer) == 0);
}

void readFile(char *fileName, char data[]) {
	char arr[255] = "";
	FILE *f = fopen(fileName, "rb");
	fseek(f, 0, SEEK_END);
	long fileSize = ftell(f);
	fseek(f, 0, SEEK_SET);
	fread(arr, fileSize, 1, f);
	fclose(f);
	int i, j;
	for (i = 0, j = 0; arr[i] != '\0'; i++)
	{
		data[j++] = arr[i];
	}
	data[j] = '\0';
}

void buildHeader(Header header, char* sendBuff, int type) {
	string code = header.code;
	string data = header.data;
	string len = to_string(header.len);
	string fullHeader;
	fullHeader = "HTTP/1.1 " + code + "\nContent-Type: text/plain\nContent-Length: " + len + "\n\n" + data;
	strcpy(sendBuff, fullHeader.c_str());
	int len1 = strlen(sendBuff);
	sendBuff[len1] = '\0';
}

bool isFromBrowser(char* buff) {
	bool toReturn = true;
	string f(buff);
	if (f.find("no browser") != string::npos)
		toReturn = false;
	return toReturn;
}

string getHeaderData(char* buff) {
	char data[1500] = { '\0' };
	int i = strlen(buff);
	while (buff[i] != '\n')
		i--;
	i++;
	for (int j = 0; buff[i] != '\0'; i++, j++)
		data[j] = buff[i];
	return string(data);
}
