#include "inConnection.h"
#include "simxUtils.h"
#define HEADER_LENGTH 6 // WORD0=1 (to detect endianness), WORD1=packetSize, WORD2=packetsLeftToRead
#define SOCKET_TIMEOUT_READ 10000 // in ms
#define TCP_SEVER_CONNECT_TIMEOUT_USEC (5000*1000)

#if defined (__linux) || defined (__APPLE__)
#include <fcntl.h>      /* Defines O_ * constants */
#include <sys/stat.h>   /* Defines mode constants */
#include <sys/mman.h>
#include <stdio.h>
#endif
CInConnection::CInConnection(int theConnectionPort,int maxPacketSize,bool newVersion)
{
    _newVersion=newVersion;
    _otherSideIsBigEndian=false;
    _connected=false;
    _leaveConnectionWait=false;
    _maxPacketSize=maxPacketSize;
    _usingSharedMem=(theConnectionPort<0);
    if (_usingSharedMem)
    { // shared memory routines are courtesy of Benjamin Navarro
		theConnectionPort=-theConnectionPort;
		_socketConnectionPort=theConnectionPort;
		set_shared_memory_name(&_shared_memory_info, _socketConnectionPort);
		set_shared_memory_size(&_shared_memory_info, _maxPacketSize+20);

		if(create_shared_memory(&_shared_memory_info))
		{
			if(map_shared_memory(&_shared_memory_info))
			{
				_shared_memory_info.buffer[0] = 0; // not yet connected
				((int*)(_shared_memory_info.buffer+1))[0] = _maxPacketSize;
			}
			else
			{
				fprintf(stderr, "Failed to map the shared memory \"%s\"\n", _shared_memory_info.name);
				destroy_shared_memory(&_shared_memory_info);
			}
		}
		else
		{
			fprintf(stderr, "Failed to create the shared memory \"%s\"\n", _shared_memory_info.name);
		}
    }
    else
    {
        if (_newVersion)
        {
            _listening = false;

            memset(&_address, 0, sizeof(struct sockaddr_in));
            _address.sin_family = AF_INET;
            _address.sin_addr.s_addr = INADDR_ANY;
            _address.sin_port = htons((u_short)theConnectionPort);

            _local_socket=-1;
            _accepted_socket=-1;
        }
        else
        {
            _socketConnectionPort=theConnectionPort;
            memset(&_socketLocal,0,sizeof(struct sockaddr_in));
            #if defined (__linux) || defined (__APPLE__)
                _socketServer=-1;
                _socketClient=-1;
            #endif /* __linux || __APPLE__ */
        }
    }
}

CInConnection::~CInConnection()
{
    if (_usingSharedMem)
    { // Shared memory routines are courtesy of Benjamin Navarro
#if defined (__linux) || defined (__APPLE__)
		if(unmap_shared_memory(&_shared_memory_info) == false) {
			fprintf(stderr, "Failed to unmap the shared memory \"%s\"\n", _shared_memory_info.name);
		}
#endif
		if(destroy_shared_memory(&_shared_memory_info) == false) {
			fprintf(stderr, "Failed to destroy the shared memory \"%s\"\n", _shared_memory_info.name);
		}
    }
    else
    {
        if (_newVersion)
        {
            #ifdef _WIN32
                if (_local_socket != -1)
                    closesocket(_local_socket);
                if (_accepted_socket != -1)
                    shutdown(_accepted_socket, 2); //SD_BOTH

                WSACleanup();
            #endif /* _WIN32 */

            #if defined (__linux) || defined (__APPLE__)
                if (_local_socket !=-1)
                    close(_local_socket);
                if (_accepted_socket !=-1)
                    close(_accepted_socket);
            #endif /* __linux || __APPLE__ */
        }
        else
        {
            if (_connected)
            {
                #ifdef _WIN32
                    shutdown(_socketClient,2); //SD_BOTH
                    closesocket(_socketServer);
                    WSACleanup();
                #endif /* _WIN32 */
                #if defined (__linux) || defined (__APPLE__)
                    if (_socketServer!=-1)
                    {
                        close(_socketServer);
                    }
                    if (_socketClient!=-1)
                    {
                        close(_socketClient);
                    }
                #endif /* __linux || __APPLE__ */
            }
        }
    }
}

bool CInConnection::connectToClient()
{
    if (_leaveConnectionWait)
        return(false);

    if (_usingSharedMem)
    { // Shared memory routines are courtesy of Benjamin Navarro
		if(is_valid_shared_memory_handle(&_shared_memory_info))
		{
			while (!_leaveConnectionWait)
			{
				bool connected = _shared_memory_info.buffer[0] == 1;

				if(connected)
				{
					break;
				}
				Sleep(1);
			}
			_connected = true;
			return true;
		}
		else
		{
			return false;
		}
    }
    else
    {
        if (_newVersion)
        {
            // Connect to the port only one time in the beginning
            if (_listening == false)
            {
                #ifdef _WIN32
                    if (WSAStartup(0x101, &_socketWsaData)!= 0)
                        return(false);
                #endif

                _local_socket = socket(AF_INET, SOCK_STREAM, 0);
                if (_local_socket == INVALID_SOCKET)
                    return(false);

                if (bind(_local_socket, (struct sockaddr*)&_address, sizeof(_address))!= 0)
                    return(false);

                if (listen(_local_socket, 10)!= 0)
                    return(false);

                FD_ZERO(&_read_fd);
                FD_SET(_local_socket, &_read_fd);
                _listening = true;
            }

            // If a clients has connect request, accept it.  
            if (!_connected)
            {
                //  Timeout  specification;  must  be  reset  every  time  select()  is  called
                struct timeval  selTimeout;     //  Timeout  for  select()
                selTimeout.tv_sec = 0;          //  Set  timeout  (secs.)
                selTimeout.tv_usec = TCP_SEVER_CONNECT_TIMEOUT_USEC;

                #ifdef _WIN32
                    select(0, &_read_fd, NULL, NULL, &selTimeout);
                #else
                    select(_local_socket+1, &_read_fd, NULL, NULL, &selTimeout);
                #endif 

                if (!FD_ISSET(_local_socket, &_read_fd))
                    return (false);

                // 2. accept client:
                struct sockaddr_in from;
                int fromlen=sizeof(from);

                _accepted_socket = accept(_local_socket,(struct sockaddr*) &from, (_socklen *) &fromlen);
                _socketConnectedMachineIP = inet_ntoa(from.sin_addr);
                _socketTimeOut.tv_sec = 1; // 1 second max between successive receive for the same packet
                _socketTimeOut.tv_usec = 0;

                // Following since 13/12/2013:
                #ifdef _WIN32
                    int to = 2000;
                    setsockopt(_accepted_socket, SOL_SOCKET, SO_RCVTIMEO,(char*)&to,sizeof(int));
                    setsockopt(_accepted_socket, SOL_SOCKET, SO_SNDTIMEO,(char*)&to,sizeof(int));
                    int yes = 1;
                    setsockopt(_accepted_socket, SOL_SOCKET, SO_REUSEADDR,(char*)&yes, sizeof(int));

//                  to = 3000000;
//                  setsockopt(_accepted_socket, SOL_SOCKET, SO_RCVBUF,(char*)&to,sizeof(int));
//                  setsockopt(_accepted_socket, SOL_SOCKET, SO_SNDBUF,(char*)&to,sizeof(int));

                #else
                    struct timeval tv;
                    tv.tv_sec = 2; // from 0 to 2 on 28/6/2014. Thanks to Ulrich Schwesinger for catching this
                    tv.tv_usec = 2000;
                    setsockopt(_accepted_socket, SOL_SOCKET, SO_RCVTIMEO,(struct timeval *)&tv,sizeof(struct timeval));
                    setsockopt(_accepted_socket, SOL_SOCKET, SO_SNDTIMEO,(struct timeval *)&tv,sizeof(struct timeval));
                    int yes = 1;
                    setsockopt(_accepted_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
                #endif

                _connected = true;
                return(true);
            }
            return(false);
        }
        else
        {
            #ifdef _WIN32
                // 1. connect to port:
                if (WSAStartup(0x101,&_socketWsaData)!=0)
                    return(false);   // WSAStartup failed.
            #endif /* _WIN32 */

            _socketLocal.sin_family=AF_INET;
            _socketLocal.sin_addr.s_addr=INADDR_ANY;
            _socketLocal.sin_port=htons((u_short)_socketConnectionPort);
            _socketServer=socket(AF_INET,SOCK_STREAM,0);
            if (_socketServer==INVALID_SOCKET)
                return(false); // socket failed.

            if (bind(_socketServer,(struct sockaddr*)&_socketLocal,sizeof(_socketLocal))!=0)
                return(false); // bind failed.

            if (listen(_socketServer,10)!=0)
                return(false); // listen failed.

            // 2. accept client:
            struct sockaddr_in from;
            int fromlen=sizeof(from);
            _socketClient=accept(_socketServer,(struct sockaddr*) &from, (_socklen *) &fromlen);
            _socketConnectedMachineIP=inet_ntoa(from.sin_addr);
            _socketTimeOut.tv_sec=1; // 1 second max between successive receive for the same packet
            _socketTimeOut.tv_usec=0;
            FD_ZERO(&_socketTheSet);
            FD_SET(_socketClient,&_socketTheSet);

            // Following since 13/12/2013:
            #ifdef _WIN32
                int to=2000;
                setsockopt(_socketClient, SOL_SOCKET, SO_RCVTIMEO,(char*)&to,sizeof(int));
                setsockopt(_socketClient, SOL_SOCKET, SO_SNDTIMEO,(char*)&to,sizeof(int));
                int yes = 1;
                setsockopt(_socketClient, SOL_SOCKET, SO_REUSEADDR,(char*)&yes, sizeof(int));
            #else
                struct timeval tv;
                tv.tv_sec = 2; // from 0 to 2 on 28/6/2014. Thanks to Ulrich Schwesinger for catching this
                tv.tv_usec = 2000;
                setsockopt(_socketClient, SOL_SOCKET, SO_RCVTIMEO,(struct timeval *)&tv,sizeof(struct timeval));
                setsockopt(_socketClient, SOL_SOCKET, SO_SNDTIMEO,(struct timeval *)&tv,sizeof(struct timeval));
                int yes = 1;
                setsockopt(_socketClient, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
            #endif

            _connected=true;
            return(true);
        }
    }
}

void CInConnection::stopWaitingForConnection()
{ // Make a fake connection to this socket, to unblock it (the thread might be trapped in the "accept" function)
    _leaveConnectionWait=true;
}

char* CInConnection::receiveMessage(int& messageSize)
{ // Returns the data size if >0, 0=we had a read time out, -1=we have an error
    if (!_connected)
    {
        messageSize=-1; // error
        return(NULL);
    }
    if (_usingSharedMem)
    {
		return(_receive_sharedMem(messageSize));
    }
    else
    {
        std::vector<char> receivedData;
        while (true)
        {
            std::vector<char> inDat;
            int result=_receiveSimplePacket(inDat);
            if (result<0)
            {
                messageSize=result+1; // error or read time-out
                return(NULL);
            }
            receivedData.insert(receivedData.end(),inDat.begin(),inDat.end());
            if (result==0)
            { // success
                messageSize=int(receivedData.size());
                char* retBuff=new char[messageSize];
                for (int i=0;i<messageSize;i++)
                    retBuff[i]=receivedData[i];
                return(retBuff);
            }
        }
    }
}

bool CInConnection::replyToReceivedMessage(char* message,int messageSize)
{
    if (!_connected)
        return(false);
    if (messageSize==0)
        return(false);

    if (_usingSharedMem)
    {
		return(_send_sharedMem(message,messageSize));
    }
    else
    {
        // In Following we make sure we don't send too big packets (we might send the data in several packets)
        int packetCount=0;
        int s=messageSize;
        while (s!=0)
        {
            packetCount++;
            if (s>_maxPacketSize-HEADER_LENGTH)
                s-=_maxPacketSize-HEADER_LENGTH;
            else
                s=0;
        }

        s=messageSize;
        int ptr=0;
        while (s!=0)
        {
            packetCount--;
            int sizeToSend=s;
            if (s>_maxPacketSize-HEADER_LENGTH)
                sizeToSend=_maxPacketSize-HEADER_LENGTH;
            s-=sizeToSend;
            if (!_sendSimplePacket(message+ptr,sizeToSend,packetCount))
                return(false);
            ptr+=sizeToSend;
        }
    }
    return(true);
}

std::string CInConnection::getConnectedMachineIP()
{
    if (_usingSharedMem)
    {
        return("");
    }
    else
    {
        if (!_connected)
            return("NONE (reception line is not open)");
        return(_socketConnectedMachineIP);
    }
}

bool CInConnection::isOtherSideBigEndian()
{
    return(_otherSideIsBigEndian);
}

bool CInConnection::_sendSimplePacket(char* packet,int packetLength,WORD packetsLeft)
{
    if (packetLength==0)
        return(false);

    // Insert the header:
    WORD s=WORD(packetLength);
    char header[HEADER_LENGTH];
    ((WORD*)header)[0]=1; // Allows to detect endianness on the other side
    ((WORD*)header)[1]=littleEndianShortConversion(s,_otherSideIsBigEndian);
    ((WORD*)header)[2]=littleEndianShortConversion(packetsLeft,_otherSideIsBigEndian);

    std::vector<char> toSend;
    for (int i=0;i<HEADER_LENGTH;i++)
        toSend.push_back(header[i]);
    for (int i=0;i<packetLength;i++)
        toSend.push_back(packet[i]);

    // Send the packet:
    if (_newVersion)
        return(send(_accepted_socket,&toSend[0],packetLength+HEADER_LENGTH,0)==packetLength+HEADER_LENGTH);
    else
        return(send(_socketClient,&toSend[0],packetLength+HEADER_LENGTH,0)==packetLength+HEADER_LENGTH);
}

int CInConnection::_receiveSimplePacket(std::vector<char>& packet)
{
    if (_newVersion)
    {
        if (_connected)
        {
            //1. Read the header and packet size:
            char headerAndSize[HEADER_LENGTH];
            int totalReceived=0;
            DWORD startT=getTimeInMs();
            while(totalReceived!=HEADER_LENGTH)
            {
                int nb=recv(_accepted_socket,headerAndSize+totalReceived,HEADER_LENGTH-totalReceived,0);
                if (nb<1)
                    break;
                totalReceived+=nb;
                if (getTimeDiffInMs(startT)>SOCKET_TIMEOUT_READ)
                    break;
            }
            // 2. Check if the header is consistent:
            if (totalReceived!=HEADER_LENGTH)
                return(-2); // Error reading

            _otherSideIsBigEndian=(((WORD*)headerAndSize)[0]!=1);

            WORD dataLength=littleEndianShortConversion(((WORD*)headerAndSize)[1],_otherSideIsBigEndian);

            // 3. Read the data with correct length:
            packet.clear();
            packet.resize(dataLength,0);
            totalReceived=0;
            startT=getTimeInMs();
            while(totalReceived!=dataLength)
            {
                int nb=recv(_accepted_socket,&packet[0]+totalReceived,dataLength-totalReceived,0);
                if (nb<1)
                    break;
                totalReceived+=nb;
                if (getTimeDiffInMs(startT)>SOCKET_TIMEOUT_READ)
                    break;
            }
            if (totalReceived!=dataLength)
                return(-2); // wrong size or nothing received
            return(int(littleEndianShortConversion(((WORD*)headerAndSize)[2],_otherSideIsBigEndian)));
        }
        return(-1);
    }
    else
    {
        _socketTimeOut.tv_sec=10; // 1 second max between successive receive for the same packet
        _socketTimeOut.tv_usec=0;
        FD_ZERO(&_socketTheSet);
        FD_SET(_socketClient,&_socketTheSet);
        #ifdef _WIN32
            int selectResult=select(0,&_socketTheSet,NULL,NULL,&_socketTimeOut);
        #else
            int selectResult=select(_socketClient+1, &_socketTheSet,NULL,NULL,&_socketTimeOut);
        #endif 
        if (selectResult==1)
        {
            //1. Read the header and packet size:
            char headerAndSize[HEADER_LENGTH];
            int totalReceived=0;
            DWORD startT=getTimeInMs();
            while(totalReceived!=HEADER_LENGTH)
            {
                int nb=recv(_socketClient,headerAndSize+totalReceived,HEADER_LENGTH-totalReceived,0);
                if (nb<1)
                    break;
                totalReceived+=nb;
                if (getTimeDiffInMs(startT)>SOCKET_TIMEOUT_READ)
                    break;
            }
            // 2. Check if the header is consistent:
            if (totalReceived!=HEADER_LENGTH)
                return(-2); // Error reading

            _otherSideIsBigEndian=(((WORD*)headerAndSize)[0]!=1);

            WORD dataLength=littleEndianShortConversion(((WORD*)headerAndSize)[1],_otherSideIsBigEndian);

            // 3. Read the data with correct length:
            packet.clear();
            packet.resize(dataLength,0);
            totalReceived=0;
            startT=getTimeInMs();
            while(totalReceived!=dataLength)
            {
                int nb=recv(_socketClient,&packet[0]+totalReceived,dataLength-totalReceived,0);
                if (nb<1)
                    break;
                totalReceived+=nb;
                if (getTimeDiffInMs(startT)>SOCKET_TIMEOUT_READ)
                    break;
            }
            if (totalReceived!=dataLength)
                return(-2); // wrong size or nothing received
            return(int(littleEndianShortConversion(((WORD*)headerAndSize)[2],_otherSideIsBigEndian)));
        }
        if (selectResult==0)
            return(-1);
        return(-2);
    }
}

// Shared memory routines are courtesy of Benjamin Navarro
bool CInConnection::_send_sharedMem(const char* data,int dataLength)
{
	int initDataLength=dataLength;
	if (dataLength==0)
		return(false);

	if (_shared_memory_info.buffer[0] == 1)
	{     // ok still connected
        DWORD startT=getTimeInMs();
		int off=0;
		while (dataLength>0)
		{
            // Wait for previous data to be gone:
			while (_shared_memory_info.buffer[5]!=0)
			{
				if (getTimeDiffInMs(startT)>1000)
				{
					return(false);
				}
			}
            // ok, we can send the data:
			if (dataLength<=_maxPacketSize)
			{     // we can send the data in one shot:
				memcpy(_shared_memory_info.buffer+20,data+off,dataLength);
				((int*)(_shared_memory_info.buffer+6))[0]=dataLength;
				((int*)(_shared_memory_info.buffer+6))[1]=20;
				((int*)(_shared_memory_info.buffer+6))[2]=initDataLength;
				dataLength=0;
			}
			else
			{     // just send a smaller part first:
				memcpy(_shared_memory_info.buffer+20,data+off,_maxPacketSize);
				((int*)(_shared_memory_info.buffer+6))[0]=_maxPacketSize;
				((int*)(_shared_memory_info.buffer+6))[1]=20;
				((int*)(_shared_memory_info.buffer+6))[2]=initDataLength;
				dataLength-=(_maxPacketSize);
				off+=(_maxPacketSize);
			}
			_shared_memory_info.buffer[5]=2;     /* server has something to send! */
		}
        return(true);
	}
    return(false);
}

char* CInConnection::_receive_sharedMem(int& dataLength)
{
	int retDataOff=0;
	char* retData=0;
	int totalLength=-1;
	dataLength=0;
	if (_shared_memory_info.buffer[0]==1)
	{     // ok still connected
		DWORD startT=getTimeInMs();
		while (retDataOff!=totalLength)
		{
			// Wait for data:
			while (_shared_memory_info.buffer[5]!=1)
			{
				if (getTimeDiffInMs(startT)>1000)
				{
					return(0);
				}
			}
			// ok, data is there!
			// Read the data with correct length:
			int l=((int*)(_shared_memory_info.buffer+6))[0];
			int off=((int*)(_shared_memory_info.buffer+6))[1];
			totalLength=((int*)(_shared_memory_info.buffer+6))[2];
			if (retData==0)
				retData=new char[totalLength];
			memcpy(retData+retDataOff,_shared_memory_info.buffer+off,l);
			retDataOff=retDataOff+l;
			// Tell the other side we have read that part and additional parts could be sent (if present):
			_shared_memory_info.buffer[5]=0;
		}

	}
	dataLength=retDataOff;
	return(retData);
}
