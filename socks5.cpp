#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <strings.h>
#include <iostream>
#include <arpa/inet.h>
#include <stdlib.h>
#include <netdb.h>
#include <string.h>
#include <sys/wait.h>
#include <poll.h>
#include <set>
#include <map>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#define MAXSIZE 1024

#define VERSION      '\x05'
#define AUTH_WAY     '\x00'
#define IP_VERSION   '\x01'
#define CONNECT_TYPE '\x01'
#define SUCCESS      '\x00'
#define ERR          '\x01'
#define SRV          '\x00'
#define WEB_NAME     '\x03'
#define UDP          '\x03'

void sigchild(int signo){
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

struct Client_Hello{
    uint8_t version;
    uint8_t method_num;
    uint8_t methods[255];
};

struct Hello_Reply{
    uint8_t version;
    uint8_t method;
};

struct Request_One{
    uint8_t version;
    uint8_t cmd;
    uint8_t rsv;
    uint8_t ip_type;
    uint8_t dst_addr[4];
    uint8_t dst_port[2];

};

struct Request_One_Reply{
    uint8_t version;
    uint8_t req;
    uint8_t rsv;
    uint8_t ip_type;
    uint8_t bind_addr[4];
    uint8_t bind_port[2];
};


void struct_msg(char* data, sockaddr_in* addr, char* contents, int len){
    memcpy(data, "\00\00\00\01", 4);
    memcpy(data + 4, &(addr->sin_addr), sizeof(addr->sin_addr));
    memcpy(data + 8, &(addr->sin_port), sizeof(addr->sin_port));
    memcpy(data + 10, contents, len);
    
}

int delete_header(char* data, int len, char* content){
    memcpy(content, data + 10, len - 10);
    return len - 10;
}

void getRemoteConnectionFromData(char* data, int len, struct sockaddr_in* addr){
    addr->sin_family = AF_INET;
    char name[256];
    if(data[3] == WEB_NAME){
        uint8_t num = data[4];
        std::cout<< num << std::endl;
        int index = 0;
        for(int i = 0; i < num; i++){
            name[index++] = data[5 + i];
        }
        name[index] = 0;
    
        
        memcpy(&addr->sin_port, data + 5 + num, 2);
        struct hostent* host= gethostbyname(name);

        for(int i=0; host->h_addr_list[i]; i++){
        printf("IP addr %d: %s\n", i+1, inet_ntoa( *(struct in_addr*)host->h_addr_list[i] ) );
        }

        std::cout<<name << std::endl;
        memcpy(&addr->sin_addr, host->h_addr_list[0], sizeof(host->h_addr_list[0]));
    }else{
        memcpy(&addr->sin_addr,  data + 4, 4);
        memcpy(&addr->sin_port, data + 8, 2);
    }
}

void transferdataUDP(int tcp_socket, int udp_socket){
    sockaddr_in local_addr, targetaddr, clientaddr, src_;
    socklen_t sock_len;
    char data[MAXSIZE], contents[MAXSIZE];
    int len;
    fd_set rs, all_set;
    FD_ZERO(&all_set);
    FD_SET(udp_socket, &all_set);
    FD_SET(tcp_socket, &all_set);


    sock_len = sizeof(local_addr);
    if(getpeername(tcp_socket, (struct sockaddr*)&local_addr, &sock_len) < 0){
        perror("getsockname_1");
        exit(0);
    }

    rs = all_set;
    while(select(std::max(tcp_socket, udp_socket) + 1, &rs, NULL, NULL, NULL) > 0){
        
        if(FD_ISSET(udp_socket, &rs)){

            sock_len = sizeof(clientaddr);
            len = recvfrom(udp_socket, data, MAXSIZE, 0, (struct sockaddr*)&clientaddr, &sock_len);
            if(len <= 0)break;

            std::cout<< inet_ntoa(clientaddr.sin_addr) << std::endl;

            if(memcmp(&clientaddr.sin_addr, &local_addr.sin_addr, sizeof(local_addr.sin_addr)) == 0){


                getRemoteConnectionFromData(data, len, &targetaddr);
                std::cout<< inet_ntoa(targetaddr.sin_addr) << " "<< ntohs(targetaddr.sin_port) << std::endl;
                len = delete_header(data, len, contents);

                
                sendto(udp_socket, contents, len, 0, (struct sockaddr*)&targetaddr, sizeof(targetaddr));
            }else{
                std::cout<<"xxx"<< std::endl;
                struct_msg(contents, &clientaddr, data, len);
                sendto(udp_socket, contents, 10 + len, 0, (struct sockaddr*)&clientaddr, sizeof(clientaddr));
            }
            
           

            
        }

        if(FD_ISSET(tcp_socket, &rs)){
            len = read(tcp_socket, data, MAXSIZE);
            std::cout <<"关闭" << std::endl;
            if(len <= 0)break;
        }

    }

    exit(0);

}

void transferdataTCP(int target_id, int client_id){
    char line[MAXSIZE];
    int len;
    fd_set rs, all_set;

    FD_ZERO(&all_set);
    FD_SET(target_id, &all_set);
    FD_SET(client_id, &all_set);
    struct timeval time_out = {5, 0};


    rs = all_set;
    while(select(std::max(target_id, client_id) + 1, &rs, NULL, NULL, NULL) > 0){
     
  
        if(FD_ISSET(client_id, &rs)){
            len = read(client_id, line, MAXSIZE);
            if(len <= 0){
                shutdown(target_id, SHUT_WR);
                shutdown(client_id, SHUT_RD);
                FD_CLR(client_id, &all_set);
            }
            write(target_id, line, len);
        }

        if(FD_ISSET(target_id, &rs)){
            len = read(target_id, line, MAXSIZE);
           
            if(len <= 0){
                shutdown(client_id, SHUT_WR);
                shutdown(target_id, SHUT_RD);
                break;
            }
            write(client_id, line, len);
        }

        rs = all_set;
    }
    exit(0);
}



void getRequestInformation(char*data, struct Request_One* rq){
    char name[1024];

    rq->version = data[0];
    rq->cmd = data[1];
    rq->rsv = data[2];
    rq->ip_type = data[3];

    if(data[3] == WEB_NAME){
        
        uint8_t num = data[4];
        int index = 0;
        for(int i = 0; i < num; i++){
            name[index++] = data[5 + i];
        }
        name[index] = 0;
    
        
        memcpy(rq->dst_port, data + 5 + num, 2);
        struct hostent* host= gethostbyname(name);
        //std::cout<< "x::" << inet_ntoa(*(struct in_addr*)(host->h_addr_list[0])) << std::endl;

        memcpy(rq->dst_addr, (uint8_t*)(host->h_addr_list[0]), 4);
        //std::cout<< "y::" << inet_ntoa(*(((struct in_addr*)rq->dst_addr)) << std::endl;
        
    }else{
        memcpy(rq->dst_addr, data + 4, 4); 
        memcpy(rq->dst_port, data + 8, 2);
    }
}

int udp_negotiation(int tcp_socket, struct Request_One* request_one, uint32_t udp_port){
    
    //创建与目标服务器的套接字
    sockaddr_in clientaddr, tcpaddr;
    socklen_t addr_len;
    int udp_socket;
    int opt = 1;
    struct Request_One_Reply request_one_reply = {VERSION, SUCCESS, SRV, IP_VERSION};

    bzero(&clientaddr, sizeof(clientaddr));
    clientaddr.sin_family = AF_INET;

    memcpy(&clientaddr.sin_addr, request_one->dst_addr , sizeof(request_one->dst_addr));
    memcpy(&clientaddr.sin_port, request_one->dst_port , sizeof(request_one->dst_port));


    /*获得该IP*/
    addr_len = sizeof(tcpaddr);
    if(getsockname(tcp_socket, (struct sockaddr*)&tcpaddr, &addr_len) < 0){
        perror("getsockname");
        exit(0);
    }

    tcpaddr.sin_port = htons(udp_port);

    //std::cout<< inet_ntoa(*((struct in_addr*)&tcpaddr.sin_addr)) <<" " << ntohs(tcpaddr.sin_port) << std::endl;

    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    

    if(setsockopt(udp_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
        perror("setsockopt");
        exit(0);
    }

    if(bind(udp_socket, (struct sockaddr*)&tcpaddr, sizeof(tcpaddr)) < 0){
        perror("bind_udp");
        exit(0);
    }

    memcpy(request_one_reply.bind_addr, (char*)&tcpaddr.sin_addr, 4);
    memcpy(request_one_reply.bind_port, (char*)&tcpaddr.sin_port, 2);

    write(tcp_socket, (char*)&request_one_reply, sizeof(request_one_reply));

    transferdataUDP(tcp_socket, udp_socket);    

    return -1;
}

int tcp_negotiation(int conn_fd, struct Request_One* request_one){
    //std::cout << inet_ntoa(*(struct in_addr*)(request_one->dst_addr)) << std::endl;
    
    struct Request_One_Reply request_one_reply;
    bzero(&request_one_reply, sizeof(request_one_reply));
    request_one_reply.version = VERSION;
    request_one_reply.req = SUCCESS;
    request_one_reply.rsv = SRV;
    request_one_reply.ip_type = IP_VERSION;
    
    //创建与目标服务器的套接字
    sockaddr_in targetaddr;
    bzero(&targetaddr, sizeof(targetaddr));
    targetaddr.sin_family = AF_INET;
   
    memcpy(&targetaddr.sin_addr, request_one->dst_addr , sizeof(request_one->dst_addr));
    memcpy(&targetaddr.sin_port, request_one->dst_port , sizeof(request_one->dst_port));

    int new_fd = socket(AF_INET, SOCK_STREAM, 0);
        
    if(new_fd < 0)return -1;

    //std::cout<< inet_ntoa(targetaddr.sin_addr) << " " << ntohs(targetaddr.sin_port) << std::endl;
    

    if(connect(new_fd, (struct sockaddr*)&targetaddr, sizeof(targetaddr)) < 0){
        request_one_reply.req = ERR;
        write(conn_fd, (char*)&request_one_reply, sizeof(request_one_reply));
        return -1;
    }else{
        write(conn_fd, (char*)&request_one_reply, sizeof(request_one_reply));
    }
    return new_fd;
}

int negotiation(int conn_fd, uint32_t udp_port){
    
    char line[MAXSIZE];
    int len;
    struct sockaddr_in addr;
    socklen_t addrlen;
    struct Client_Hello client_hello;
    struct Hello_Reply hello_reply = {VERSION, AUTH_WAY}; 
    struct Request_One request_one;
    

    len = read(conn_fd, (char*)&client_hello, sizeof(client_hello));
    
    if(len <= 0)return -1;

    if(client_hello.version == VERSION && client_hello.methods[0] == AUTH_WAY){
        
        //hello 回应
        write(conn_fd, (char*)&hello_reply, sizeof(hello_reply));
        
        //读取客户端对于需要连接服务器的请求
        len = read(conn_fd, line, MAXSIZE);
        if(len <= 0)return -1;

        getRequestInformation(line, &request_one);
       
        
        //判断是否是请求连接，以及是否是IPV4协议
        if(request_one.ip_type != IP_VERSION && request_one.ip_type != WEB_NAME)return -1;

       

        if(request_one.cmd == CONNECT_TYPE){
            return tcp_negotiation(conn_fd, &request_one);
        }else if(request_one.cmd == UDP){
            std::cout<<"UDP 来了" << std::endl;
            return udp_negotiation(conn_fd, &request_one, udp_port);
        }else{
            return -1;
        }

    }else{
        return -1;
    }
}

uint32_t getPort(){
    int start = 8000, socket_id;
    struct sockaddr_in addr;
    int opt = 1;

    for(int i = start; i < 65534; ++i){
        addr.sin_family = AF_INET;
        addr.sin_port = htons(i);
        addr.sin_addr.s_addr = INADDR_ANY;


        socket_id = socket(AF_INET, SOCK_DGRAM, 0);
         
        if(setsockopt(socket_id, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
            perror("setsockopt");
            exit(0);
        }

        if(bind(socket_id, (struct sockaddr*)&addr, sizeof(addr)) < 0){
            continue;
        }

        close(socket_id);
        return i;
    }
    

}

int main(){

    char ip[] = "192.168.137.3";
	int socket_fd, conn_fd, opt = 1, len, udp_socket;
    uint32_t udp_port;
    socklen_t addrlen;
	sockaddr_in addr, udpaddr, clientaddr;
    char line[MAXSIZE];

    //tcp 套接字
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	
    bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port   = htons(8888);
	addr.sin_addr.s_addr = INADDR_ANY;

    signal(SIGCHLD, sigchild);
    
    if(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
        perror("setsockopt");
        exit(0);
    }


    if(bind(socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        perror("bind");
        exit(0);
    }

    listen(socket_fd, 1024);
    
    while(true){
        addrlen = sizeof(clientaddr);

        conn_fd = accept(socket_fd, (struct sockaddr*)&clientaddr, &addrlen);
        
        if(setsockopt(conn_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0){
            perror("setsockopt");
            exit(0);
        }


        udp_port = getPort();

        if(fork() == 0){
            close(socket_fd);
            int new_fd;
            if((new_fd = negotiation(conn_fd, udp_port)) > 0){
                transferdataTCP(new_fd, conn_fd);
            }
            exit(0);
        }
        close(conn_fd);

    }
    return 0;
}
