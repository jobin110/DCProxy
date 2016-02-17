#include <PROXYSERVER.hpp>

long double dataUsageCounter = 0;
long double dataSavedCounter = 0;
long double UnCompressibleDataCounter = 0;
SETTINGS systemSettings;

bool PROXYSERVER::StartServer()
{
    if(!this->SetupSocket())
        return false;
    if(!this->PutInListeningState())
        return false;
    return true;
}

bool PROXYSERVER::SetupSocket()
{
  struct sockaddr_in serverAddr;
  this->serverListenSocketFD = socket(AF_INET, SOCK_STREAM, 0);
  if(this->serverListenSocketFD == -1)
  {
    cerr<<"\nFailed to create server socket";
    return false;
  }
  memset(&serverAddr, '0', sizeof(serverAddr));
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_addr.s_addr = inet_addr(this->ipAddress.c_str());
  serverAddr.sin_port = htons(atoi(this->port.c_str()));

  if(bind(this->serverListenSocketFD, (struct sockaddr*)&serverAddr,sizeof(serverAddr)) == -1)
  {
    cerr<<"\nFailed to bind server socket to specified ip address";
    return false;
  }
  else
    return true;
}

bool PROXYSERVER::PutInListeningState()
{
   if(listen(this->serverListenSocketFD, 20) == -1)
   {
    cerr<<"\nFailed to listen on server socket";
    return false;
   }
    while(1)
    {
        int socketFD = accept(serverListenSocketFD, (struct sockaddr*)NULL ,0);
        thread *td = new thread(ProcessConnection,socketFD);
    }
    close(this->serverListenSocketFD);
    return true;
}
bool ProcessConnection(int socketFD)
{
    SOCKETPACK socketPack;
    socketPack.socketFDClient = socketFD;
    string buffer("");
    buffer.resize(REQUEST_BUFFER_SIZE, '\0');
    int bytesReceived = 0;
    char *carryForwardBuffer = NULL;
    int carryForwardBufferSize = 0;

    struct timeval tv;
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    setsockopt(socketPack.socketFDClient, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));
    while((bytesReceived = recv(socketPack.socketFDClient, (void*)&(buffer.data()[carryForwardBufferSize]), (buffer.capacity() - carryForwardBufferSize), NULL)) > 0)
    {
        if(carryForwardBuffer != NULL)
            memcpy((void*)buffer.data(), carryForwardBuffer, carryForwardBufferSize);
        int bodyStartPosition = buffer.find("\r\n\r\n");
        if(bodyStartPosition != string::npos)
        {
            bodyStartPosition += 4;
            int contentLength = 0;
            string strContentLength = GetAttributeValue((char*)buffer.data(), "Content-Length");
            if(strContentLength == "")
                contentLength = 0;
            else
                contentLength = atoi(strContentLength.c_str());
            if((bodyStartPosition + contentLength) == (bytesReceived+carryForwardBufferSize)) //last byte of request in this packet
            {
                if(carryForwardBuffer != NULL)
                {
                    free(carryForwardBuffer);
                    carryForwardBuffer = NULL;
                    carryForwardBufferSize = 0;
                }
                if(!SendRequest((char*)buffer.data(), bodyStartPosition + contentLength, socketPack))
                    break;
            }
            else if((bodyStartPosition + contentLength) < (bytesReceived+carryForwardBufferSize))
            {
                SendRequest((char*)buffer.data(), bodyStartPosition + contentLength, socketPack);
                if(carryForwardBuffer != NULL)
                    free(carryForwardBuffer);
                if(!SendRequest((char*)buffer.data(), bodyStartPosition + contentLength, socketPack))
                    break;
                carryForwardBufferSize = (bytesReceived+carryForwardBufferSize) - (bodyStartPosition + contentLength);
                carryForwardBuffer = new char[carryForwardBufferSize];
                memcpy(carryForwardBuffer, &buffer[bodyStartPosition + contentLength], carryForwardBufferSize);
            }

            else
            {
                if(carryForwardBuffer != NULL)
                    free(carryForwardBuffer);
                carryForwardBufferSize = bytesReceived+carryForwardBufferSize;
                if(carryForwardBufferSize >= buffer.capacity())
                {
                    if(carryForwardBufferSize < MAX_REQUEST_BUFFER_SIZE)
                        buffer.resize(buffer.capacity() + 500);
                    else
                    {
                        close(socketPack.socketFDClient);
                        return false;
                    }
                }
                carryForwardBuffer = new char[carryForwardBufferSize];
                memcpy(carryForwardBuffer, (void*)buffer.data(), carryForwardBufferSize);
            }
        }
        else
        {
            if(carryForwardBuffer != NULL)
                free(carryForwardBuffer);
            carryForwardBufferSize = bytesReceived + carryForwardBufferSize;
            if(carryForwardBufferSize >= buffer.capacity())
            {
                if(carryForwardBufferSize < MAX_REQUEST_BUFFER_SIZE)
                    buffer.resize(buffer.capacity() + 500);
                else
                {
                    close(socketPack.socketFDClient);
                    return false;
                }
            }
            carryForwardBuffer = new char[carryForwardBufferSize];
            memcpy(carryForwardBuffer, (void*)buffer.data(), carryForwardBufferSize);
        }
        memset((void*)buffer.data(), '\0', buffer.capacity());
    }
    close(socketPack.socketFDClient);
}

bool SendRequest(char *requestBuffer, long requestBufferSize, SOCKETPACK socketPack)
{
    string host, port;
    int socketFDServer = 0;
    int foundLocation = -1;
    if(GetHostAndPort(requestBuffer, host, port) != -1)
    {
        char hostIP[20];
        HostnameToIp((char*)host.c_str(), (char*)hostIP);
        struct sockaddr_in serv_addr;
        if((socketFDServer = socket(AF_INET, SOCK_STREAM, 0)) < 0)
            return false;
        memset(&serv_addr, '0', sizeof(serv_addr));
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(atoi(port.c_str()));
        if(inet_pton(AF_INET, hostIP, &serv_addr.sin_addr)<=0)
            return false;
        if(connect(socketFDServer , (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
            return false;
        socketPack.socketFDServer = socketFDServer;
        new thread(ServerToClient, socketPack);

        if(send(socketPack.socketFDServer, requestBuffer, requestBufferSize, NULL) > 0)
            return true;
        else
            return false;
    }
    else
        return false;
}

bool ServerToClient(SOCKETPACK socketPack)
{
    string socketBuffer("");
    socketBuffer.resize(RESPONSE_BUFFER_SIZE, '\0');
    int bytesReceived = 0;
    char *carryForwardBuffer = NULL;
    long carryForwardBufferSize = 0;
    string contentType = "";
    long contentLength = -1;
    bool isCompressible = false;
    int bodyStartPosition = 0;

    //-------------------------------------------Parse Header--------------------------------------------------------------------------------------------------------------
    struct timeval tv;
    tv.tv_sec = 30;
    tv.tv_usec = 0;
    setsockopt(socketPack.socketFDServer, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));
    while((bytesReceived = recv(socketPack.socketFDServer, (void*)&(socketBuffer.data()[carryForwardBufferSize]), socketBuffer.capacity()-carryForwardBufferSize, NULL)) > 0)
    {
        if(carryForwardBuffer != NULL)
            memcpy((void*)socketBuffer.data(), carryForwardBuffer, carryForwardBufferSize);
        bodyStartPosition = socketBuffer.find("\r\n\r\n");
        if(bodyStartPosition == string::npos)
        {
            if(carryForwardBuffer != NULL)
                free(carryForwardBuffer);
            carryForwardBufferSize = bytesReceived + carryForwardBufferSize;
            if(carryForwardBufferSize >= socketBuffer.capacity())
            {
                if(carryForwardBufferSize < MAX_RESPONSE_BUFFER_SIZE)
                    socketBuffer.resize(socketBuffer.capacity() + 2000);
                else
                {
                    close(socketPack.socketFDServer);
                    return false;
                }
            }
            carryForwardBuffer = new char[carryForwardBufferSize];
            memcpy(carryForwardBuffer, (void*)socketBuffer.data(), carryForwardBufferSize);
        }
        else
        {
            bodyStartPosition += 4;
            contentType = GetAttributeValue((char*)socketBuffer.data(), "Content-Type");
            string strContentLength = GetAttributeValue((char*)socketBuffer.data(), "Content-Length");
            if(strContentLength == "")
                contentLength = -1;
            else
                contentLength = atoi(strContentLength.c_str());
            if((contentType.find("image") != string::npos) && (contentLength != -1))
            {
                isCompressible = true;
                if(carryForwardBuffer != NULL)
                    free(carryForwardBuffer);
                carryForwardBufferSize = bytesReceived + carryForwardBufferSize;
                carryForwardBuffer = new char[carryForwardBufferSize];
                memcpy(carryForwardBuffer, (void*)socketBuffer.data(), carryForwardBufferSize);
            }
            else
            {
                isCompressible = false;
                int bytesToSend = bytesReceived + carryForwardBufferSize;
                if(carryForwardBuffer != NULL)
                {
                    free(carryForwardBuffer);
                    carryForwardBuffer = NULL;
                }
                carryForwardBufferSize = 0;
                /*if(GetAttributeValue((char*)socketBuffer.data(), "Content-Type").find("application/ocsp-response") != string::npos)
                {
                    static int ocspcounter = 0;
                    ocspcounter += atoi((GetAttributeValue((char*)socketBuffer.data(), "Content-Length")).data());
                    cerr<<"\nocsp:"<<ocspcounter;
                    close(socketPack.socketFDServer);
                    return false;
                }*/
                if(send(socketPack.socketFDClient, (void*)socketBuffer.data(), bytesToSend, NULL) > 0)
                {
                    dataUsageCounter += (double)bytesToSend/1024;
                    UnCompressibleDataCounter += (double)bytesToSend/1024;
                    cerr<<"\nData Saved : "<<dataSavedCounter<<" Data Usage:"<<dataUsageCounter;
                    memset((void*)socketBuffer.data(), '\0', socketBuffer.capacity());
                }
            }
            break;
        }
    }
    //---------------------------------------------------------------------------------------------------------------------------------------------------------------------

    if(isCompressible)
    {
        char *fileBuffer = new char[bodyStartPosition + contentLength];
        if(carryForwardBuffer != NULL)
            memcpy((void*)fileBuffer, carryForwardBuffer, carryForwardBufferSize);
        long totalBytesReceived = carryForwardBufferSize;
        free(carryForwardBuffer);
        carryForwardBufferSize = 0;
        if(bodyStartPosition + contentLength > totalBytesReceived)
            struct timeval tv;
            tv.tv_sec = 30;
            tv.tv_usec = 0;
            setsockopt(socketPack.socketFDServer, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));
            while((bytesReceived = recv(socketPack.socketFDServer, (void*)&fileBuffer[totalBytesReceived], bodyStartPosition + contentLength-totalBytesReceived, NULL)) > 0)
            {
                totalBytesReceived += bytesReceived;
                if(totalBytesReceived >= (bodyStartPosition + contentLength))
                break;
            }
        if(!CompressImageAndSend(socketPack, fileBuffer, bodyStartPosition + contentLength, bodyStartPosition, 70))
        {
            if(send(socketPack.socketFDClient, (void*)fileBuffer, bodyStartPosition + contentLength, NULL) > 0)
            {
                dataUsageCounter += (double)bytesReceived/1024;
                cerr<<"\nData Saved : "<<dataSavedCounter<<" Data Usage:"<<dataUsageCounter;
            }
            else
            {
                close(socketPack.socketFDServer);
            }
        }
        free(fileBuffer);
    }
    else
    {
        int socketBufferSize = socketBuffer.capacity();
        struct timeval tv;
        tv.tv_sec = 30;
        tv.tv_usec = 0;
        setsockopt(socketPack.socketFDServer, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval));
        while((bytesReceived = recv(socketPack.socketFDServer, (void*)socketBuffer.data(), socketBufferSize, NULL)) > 0)
        {
            if(send(socketPack.socketFDClient, (void*)socketBuffer.data(), bytesReceived, NULL) > 0)
            {
                dataUsageCounter += (double)bytesReceived/1024;
                UnCompressibleDataCounter += (double)bytesReceived/1024;
                //cerr<<"\nUncompressible data : "<<UnCompressibleDataCounter;
                cerr<<"\nData Saved : "<<dataSavedCounter<<" Data Usage:"<<dataUsageCounter;
                memset((void*)socketBuffer.data(), '\0', socketBufferSize);
            }
            else
                break;
        }
    }
    close(socketPack.socketFDServer);
}

bool CompressImageAndSend(SOCKETPACK socketPack, char *buffer, long bufferSize, int bodyStartPosition, int compressionLevel)
{
    long int contentLength = bufferSize - bodyStartPosition;
    long headerBufferSize = bodyStartPosition;
    char *headerBuffer = new char[headerBufferSize];
    memcpy(headerBuffer, buffer, bodyStartPosition);
    FIMEMORY *hMem = FreeImage_OpenMemory((BYTE*)&buffer[bodyStartPosition], contentLength);
    FREE_IMAGE_FORMAT fif = FreeImage_GetFileTypeFromMemory(hMem, 0);
    if(fif == FIF_GIF)
    {
        FreeImage_CloseMemory(hMem);
        free(headerBuffer);
        return false;
    }
    FIBITMAP *pic = FreeImage_LoadFromMemory(fif, hMem, 0);
    int minCompression_jpg = 10;
    int compressionFlag = 100 - ((100 - minCompression_jpg) * ((double)compressionLevel/100));
    FIMEMORY *hTargetMem = saveAsJpeg(pic, compressionFlag);
    if(!hTargetMem && fif==FIF_PNG)
    {
        hTargetMem = FreeImage_OpenMemory();
        FreeImage_SaveToMemory(FIF_PNG, pic, hTargetMem, PNG_Z_BEST_COMPRESSION);
    }
    else if(!hTargetMem)
    {
        FreeImage_Unload(pic);
        FreeImage_CloseMemory(hMem);
        FreeImage_CloseMemory(hTargetMem);
        free(headerBuffer);
        return false;
    }

    BYTE *destBuffer = NULL;
    DWORD destBufferSize = 0;
    if(!FreeImage_AcquireMemory(hTargetMem, &destBuffer, &destBufferSize) | (destBufferSize >= contentLength))
    {
        FreeImage_Unload(pic);
        FreeImage_CloseMemory(hMem);
        FreeImage_CloseMemory(hTargetMem);
        free(headerBuffer);
        return false;
    }
    ostringstream destBufferSizeStream;
    destBufferSizeStream << destBufferSize;
    string newContentLength = destBufferSizeStream.str();
    ostringstream dataSavedStream;
    dataSavedStream << contentLength-destBufferSize;
    string strDataSaved = dataSavedStream.str();
    if(!SetAttributeValue(headerBuffer, headerBufferSize, "Content-Length", newContentLength))
    {
        FreeImage_Unload(pic);
        FreeImage_CloseMemory(hMem);
        FreeImage_CloseMemory(hTargetMem);
        free(headerBuffer);
        return false;
    }
    SetAttributeValue(headerBuffer, headerBufferSize, "Content-Type", "image/jpeg");
    SetAttributeValue(headerBuffer, headerBufferSize, "Data-Saved", strDataSaved);
    if(headerBufferSize+destBufferSize > bufferSize)
    {
        bufferSize = headerBufferSize+destBufferSize;
        buffer = new char[bufferSize];
    }
    memcpy((char*)buffer, (void*)headerBuffer, headerBufferSize);
    memcpy(&buffer[headerBufferSize], destBuffer, destBufferSize);
    if(send(socketPack.socketFDClient, buffer, headerBufferSize+destBufferSize, NULL) > 0)
    {
        dataSavedCounter += (double)(contentLength - destBufferSize)/1024;
        dataUsageCounter += (double)(headerBufferSize + destBufferSize)/1024;
        cerr<<"\nData Saved : "<<dataSavedCounter<<" Data Usage:"<<dataUsageCounter;
    }
    else
    {
        FreeImage_Unload(pic);
        FreeImage_CloseMemory(hMem);
        FreeImage_CloseMemory(hTargetMem);
        free(headerBuffer);
        return false;
    }
    FreeImage_Unload(pic);
    FreeImage_CloseMemory(hMem);
    FreeImage_CloseMemory(hTargetMem);
    free(headerBuffer);
    return true;
}

FIMEMORY* saveAsJpeg(FIBITMAP *bmp, int compressionFlag)
{
    if(!bmp)
        return NULL;
    int bpp = FreeImage_GetBPP(bmp);
    FIMEMORY *hMem = FreeImage_OpenMemory();
    bool succeed = false;
    if(bpp == 24)
        succeed = FreeImage_SaveToMemory(FIF_JPEG, bmp, hMem, compressionFlag);
    else
    {
        succeed = FreeImage_SaveToMemory(FIF_TIFF, bmp, hMem, TIFF_JPEG);
    }

    if(!succeed)
        return NULL;
    else
        return hMem;
}

int GetHostAndPort(char packetBuffer[], string& host, string& port)
{
    string buffer(packetBuffer);
    int startPosition = buffer.find("Host:");
    if(startPosition == string::npos)
        return -1;
    startPosition += 5;
    int endPosition = buffer.find("\r\n", startPosition);
    if(endPosition == string::npos)
        return -1;
    while(buffer.at(startPosition) == ' ')
        startPosition++;
    while(buffer.at(endPosition) == ' ')
        endPosition--;

    host = buffer.substr(startPosition, endPosition-startPosition+1);
    int separatorPosition = host.find(":");
    if(separatorPosition == string::npos)
    {
        port = "80";
        host.at(host.length()-1) = '\0';
    }
    else
    {
        port = host.substr(separatorPosition+1, host.length()-1);
        host = host.substr(0, separatorPosition);
    }
}

string GetAttributeValue(char packetBuffer[], string attribute)
{
    string buffer(packetBuffer);
    int startPosition = buffer.find(attribute);
    if(startPosition == string::npos)
        return "";
    startPosition += attribute.length()+1;
    int endPosition = buffer.find("\r\n", startPosition);
    if(endPosition == string::npos)
        return "";
    while(buffer.at(startPosition) == ' ')
        startPosition++;
    while(buffer.at(endPosition) == ' ')
        endPosition--;

    return buffer.substr(startPosition, endPosition-startPosition+1);
}

bool SetAttributeValue(char*& headerBuffer, long& headerBufferSize, string attribute, string value)
{
    if(!headerBuffer)
        return false;
    string buffer(headerBuffer);
    int startPosition = buffer.find(attribute);
    if(startPosition == string::npos)
    {
        char *tempBuffer = new char[headerBufferSize + attribute.length() + value.length() + 4];
        memcpy(tempBuffer, headerBuffer, headerBufferSize);
        free(headerBuffer);
        headerBuffer = tempBuffer;
        strcpy(&headerBuffer[headerBufferSize-2], attribute.c_str());
        headerBuffer[headerBufferSize - 2 + attribute.length()] = ':';
        strcpy(&headerBuffer[headerBufferSize - 2 + attribute.length() + 1], value.c_str());
        headerBufferSize = headerBufferSize + attribute.length() + value.length() + 3;
        headerBuffer[headerBufferSize] = '\0';
        strcpy(&headerBuffer[headerBufferSize-4], "\r\n\r\n");
        return true;
    }
    startPosition += attribute.length()+1;
    int endPosition = buffer.find("\r\n", startPosition);
    if(endPosition == string::npos)
        return false;
    endPosition--;
    if((endPosition-startPosition+1) < value.length())
        return false;
    memset(&headerBuffer[startPosition], ' ', endPosition-startPosition+1);
    for(int i=0; i<(endPosition-startPosition+1)-value.length(); i++)
    {
        headerBuffer[startPosition] = ' ';
        startPosition++;
    }
    memcpy(&headerBuffer[startPosition], (void*)value.data(), value.length());
    return true;
}

bool HostnameToIp(char hostname[] , char* ip)
{
    int sockfd;
    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_in *h;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if ( (rv = getaddrinfo( hostname , "http" , &hints , &servinfo)) != 0)
    {
        return false;
    }

    for(p = servinfo; p != NULL; p = p->ai_next)
    {
        h = (struct sockaddr_in *) p->ai_addr;
        strcpy((char*)ip , (char*)inet_ntoa( h->sin_addr ) );
    }

    freeaddrinfo(servinfo);
    return true;
}

void FreeImageErrorHandler(FREE_IMAGE_FORMAT fif, const char *message)
{
    cerr<<"\n*** ";
    if(fif != FIF_UNKNOWN)
    {
        cerr<<FreeImage_GetFormatFromFIF(fif)<<"Format\n";
    }
    cerr<<message;
    cerr<<" ***\n";
}

bool SETTINGS::LoadSettings()
{
    ifstream file("config.cfg");
    string keyValuePair = "";

    if(file.is_open())
    {cerr<<"yes";
        while(getline(file, keyValuePair))
        {
            int pos = keyValuePair.find("=");
            if(pos != string::npos)
            {
                string key = keyValuePair.substr(0, pos);
                string value = keyValuePair.substr(pos+1, keyValuePair.length()-pos);
                if(key == "ip")
                    this->serverIpAddress = value;
                else if(key == "port")
                    this->serverPort = value;
                else if(key == "compression_level")
                    this->compressionLevel = atoi(value.c_str());
            }
        }
        return true;
    }
    else
    {   cerr<<"no";
        return false;
    }
}

/*static char label = 'a';
        string filename = "/home/jobin/Desktop/img/";
        filename += label;
         if(contentType == "image/jpeg")
        filename += ".jpg";
         if(contentType == "image/png")
         filename += ".png";
        ofstream f(filename, ios::out|ios::binary);
        f.write(&(fileBuffer.data()[bodyStartPosition]), contentLength);
        f.close();
        label++;*/
