#ifndef PROXYSERVER_HPP_INCLUDED
#define PROXYSERVER_HPP_INCLUDED
#endif // PROXYSERVER_HPP_INCLUDED

#include<vector>
#include <sys/socket.h>
#include <stdlib.h>
#include <unistd.h>
#include <iostream>
#include <arpa/inet.h>
#include <cstring>
#include <string>
#include <thread>
#include <netdb.h>
#include <FreeImage.h>
#include <fstream>
#include <time.h>
#include <sstream>

using namespace std;

class PROXYSERVER{
    private:
        string ipAddress;
        string port;
        int serverListenSocketFD;
        bool SetupSocket();
        bool PutInListeningState();
        void LoadSettings();

    public:
        PROXYSERVER(string ipAddress, string port)
        {
            this->ipAddress = ipAddress;
            this->port = port;
            serverListenSocketFD = 0;
        }
        bool StartServer();
        void StopServer();
        friend bool ProcessConnection(int socketFD);
};

struct SOCKETPACK
{
    int socketFDClient;
    int socketFDServer;

    SOCKETPACK()
    {
        socketFDClient = 0;
        socketFDServer = 0;
    }
};

struct SETTINGS
{
    string serverIpAddress;
    string serverPort;
    int compressionLevel;
    bool LoadSettings();

    SETTINGS()
    {
        if(!LoadSettings())
            exit(0);
    }
};
extern SETTINGS systemSettings;

#define RESPONSE_BUFFER_SIZE 5000
#define REQUEST_BUFFER_SIZE 1500
#define MAX_RESPONSE_BUFFER_SIZE 50000
#define MAX_REQUEST_BUFFER_SIZE 100000

bool ProcessConnection(int socketFD);
int GetHostAndPort(char packetBuffer[], string& host, string& port);
bool SendRequest(char *requestBuffer, long requestBufferSize, SOCKETPACK socketPack);
int IsConnectionPresent(string host, string port);
string GetAttributeValue(char packetBuffer[], string attribute);
bool SetAttributeValue(char*& headerBuffer, long& headerBufferSize, string attribute, string value);
bool HostnameToIp(char hostname[] , char* ip);
bool ServerToClient(SOCKETPACK socketPack);
bool CompressImageAndSend(SOCKETPACK socketPack, char *buffer, long bufferSize, int bodyStartPosition, int compressionLevel);
void FreeImageErrorHandler(FREE_IMAGE_FORMAT fif, const char *message);
FIMEMORY* saveAsJpeg(FIBITMAP *bmp, int compressionFlag);
