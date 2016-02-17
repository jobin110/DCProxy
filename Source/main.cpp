#include <PROXYSERVER.hpp>

int main()
{
    //FreeImage_SetOutputMessage(FreeImageErrorHandler);
    PROXYSERVER server(systemSettings.serverIpAddress, systemSettings.serverPort);
    if(!server.StartServer())
    {
        cerr<<"Failed to start server";
        return 0;
    }

    return 1;
}
