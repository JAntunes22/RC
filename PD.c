#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <ctype.h>
#include <signal.h>
#include "utils.h"
#define true 1
#define false 0

// GLOBAL STRINGS
char * ASIP = NULL;
char * ASport = NULL;
char * PDIP = NULL;
char * PDport = NULL;  

// Flags
int done = 0;
int signal_count = 0;
char buffer[128];
int flagSendToAS = false;
int flagRecvFromAS = false;
int flagSendToAS_client = false;
int flagRecvFromAS_client = false;

// Socket variables
int fd_client, fd_server;
int maxfd, out_fds;
fd_set inputs;
int errcode;
ssize_t n;
socklen_t addrlen_C,addrlen_S;
struct addrinfo hints_C,*res_C, hints_S, *res_S;
struct sockaddr_in addr_C, addr_S;

// *******************
// READ COMMAND 
// *******************

void parseArgs(int argc, char* argv[]) {
    

    if (argc > 8 || argc % 2 != 0 || argc < 2) {
        fprintf(stderr, "Invalid format, ./pd PDIP [-d PDport] [-n ASIP] [-p ASport]\n");
        exit(1); //error
    }
    for(int i = 1; i < argc ; i++) {
        if (i == 1) 
            PDIP = argv[i] ;
        else {
            if (strcmp("-d", argv[i]) == 0) 
                PDport = argv[i+1];

            if (strcmp("-n", argv[i]) == 0) 
                ASIP = argv[i+1];              
            
            if (strcmp("-p", argv[i]) == 0) 
                ASport = argv[i+1];
        }
    }
    
    if ( PDport == NULL) 
        PDport = "57027"; 

    if ( ASIP == NULL )  
        ASIP = "127.0.0.1";  

    if ( ASport == NULL ) 
        ASport = "58027"; 
}

// *******************
// EXIT PROGRAM
// *******************

// Exit program correctly
void exit_error(int n){
    perror("Error, forced to exit");
    freeaddrinfo(res_C);
    freeaddrinfo(res_S);
    close(fd_client);
    close(fd_server);
    exit(n);
}

// Exit by ctrl+c
void handle_CtrlC(){
    printf("Program exit by Ctrl+C\n");
    freeaddrinfo(res_C);
    freeaddrinfo(res_S);
    close (fd_client);
    close (fd_server);
    exit(0); 

}

// Timeout 
void handle_alarm(){
    if (!done){
        signal( SIGALRM, handle_alarm );
        alarm(3);

        if (signal_count == 2){
            if (flagSendToAS || flagRecvFromAS || flagRecvFromAS_client || flagSendToAS_client){
                printf("Timeout SIGALARM detected in communications with AS\n");
                exit_error(2);
            } else{
                printf("Timeout SIGALARM detected\n");
                exit_error(1);
            }
        } else if (flagSendToAS){
            signal_count++;
            n = sendto(fd_client, buffer, strlen(buffer), 0, res_C->ai_addr, res_C->ai_addrlen); 
            if (n == -1) /*error*/ exit_error(1);

        } else if (flagRecvFromAS){
            signal_count++;
            n = recvfrom (fd_client,buffer,128,0,(struct sockaddr*)&addr_C, &addrlen_C);
            if(n == -1) /*error*/ exit_error(1);

        } else if (flagRecvFromAS_client){
            signal_count++;
            n = recvfrom(fd_server, buffer, 128, 0, (struct sockaddr*)&addr_S, &addrlen_S);
            if (n == -1) /*error*/ exit_error(1); 

        } else if (flagSendToAS_client){
            signal_count++;
            n = sendto (fd_server, buffer, strlen(buffer), 0, (struct sockaddr*)&addr_S, addrlen_S);
            if (n == -1) /*error*/ exit_error(1);

        }else{
            signal_count++;
        }
    } 
}

// *******************
// SOCKETS CONNECTIONS
// *******************

void connectClient_UDP(){
    fd_client = socket(AF_INET,SOCK_DGRAM, 0);
    if (fd_client == -1) /*error*/ exit(1);

    memset(&hints_C, 0, sizeof hints_C);
    hints_C.ai_family = AF_INET;                 
    hints_C.ai_socktype = SOCK_DGRAM; // UDP socket
    errcode = getaddrinfo(ASIP, ASport, &hints_C, &res_C); // ligação ao AS
    if (errcode != 0) /*error*/ exit(1);
}

void connectServer_UDP(){
    fd_server = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_server == -1) /*error*/ exit(1);

    memset(&hints_S, 0, sizeof hints_S);
    hints_S.ai_family = AF_INET;                 
    hints_S.ai_socktype = SOCK_DGRAM; // UDP socket
    hints_S.ai_flags = AI_PASSIVE;

    errcode=getaddrinfo(NULL, PDport, &hints_S, &res_S); // ligação ao AS
    if (errcode != 0) /*error*/ exit(1);

    n = bind(fd_server,res_S->ai_addr, res_S->ai_addrlen);
    if (n == -1) /*error*/ exit(1);
}

//************************
// SERVER PD
//************************
void serverPD( char* UID){
    char *AScommand=(char*)malloc(sizeof(char)*10);
    char *AS_UID=(char*)malloc(sizeof(char)*6);
    char *VC=(char*)malloc(sizeof(char)*5);
    char * FOP=(char*)malloc(sizeof(char)*2);
    char * Fname=(char*)malloc(sizeof(char)*25);

    addrlen_S = sizeof(addr_S);

    flagRecvFromAS_client = true;
    n = recvfrom(fd_server, buffer, 128, 0, (struct sockaddr*)&addr_S, &addrlen_S);
    if (n == -1) /*error*/ exit_error(1);
    flagRecvFromAS_client = false;
    signal_count = 0;
    buffer[n]='\0';
    
    sscanf(buffer,"%s %s %s %s %s", AScommand, AS_UID, VC, FOP, Fname); 
    
    // VLC UID VC FOP [Fname]
    if ( correctMessage(FOP,buffer) == -1){
        sprintf(buffer, "ERR\n"); 
    } else if( !stringNumbers(AS_UID,5) || !stringNumbers_Letters(VC,4) || !stringFOP(FOP) ) {
            sprintf(buffer, "ERR\n");
    } else if (strcmp(AScommand, "VLC") == 0 && strcmp(UID, AS_UID) == 0) {
        printf("val: %s\n",VC);
        sprintf(buffer, "RVC %s OK\n",AS_UID);

    } else if (strcmp(AScommand, "VLC") == 0 && strcmp(UID, AS_UID) != 0) {
        sprintf(buffer, "RVC %s NOK\n",AS_UID);

    } else {
        sprintf(buffer, "ERR\n");
    }

    // RVC UID status
    flagSendToAS_client = true;
    n = sendto (fd_server, buffer, strlen(buffer), 0, (struct sockaddr*)&addr_S, addrlen_S);
    if (n == -1) /*error*/ exit_error(1);
    flagSendToAS_client = false;
    signal_count = 0;

    free(AScommand);
    free(AS_UID);
    free(FOP);
    free(Fname);
    free(VC);
}

//************************
// PD COMMUNICATE WITH AS
//************************

void clientPD(char * UID, char * pass, int * user_reg){
    char *command=(char*)malloc(sizeof(char)*10);
    char *status=(char*)malloc(sizeof(char)*5);

    // Read input
    fgets(buffer, sizeof(buffer), stdin);
    sscanf(buffer, "%s %s %s", command, UID, pass);
    
    if (strcmp(command, "reg") == 0){ 
        // reg UID pass

        if(*user_reg == 1){
            printf("User already registered\n");
            free(command);
            free(status);
            return;
        } else if ( word_count(buffer) != 3 ){
            printf("Wrong format input, format: reg UID pass\n");
        } else if( !stringNumbers(UID,5) || !stringNumbers_Letters(pass,8) ) {
            printf("Wrong UID or pass format\n");
        } else {
            sprintf(buffer, "REG %s %s %s %s\n", UID, pass, PDIP, PDport);

            // communicate with AS
            // REG UID pass PDIP PDport
            flagSendToAS = true;
            n = sendto(fd_client, buffer, strlen(buffer), 0, res_C->ai_addr, res_C->ai_addrlen); 
            if (n == -1) /*error*/ exit_error(1);
            flagSendToAS = false;
            signal_count = 0;
            
            addrlen_C = sizeof(addr_C);

            flagRecvFromAS = true;
            n = recvfrom (fd_client,buffer,128,0,(struct sockaddr*)&addr_C, &addrlen_C);
            if(n == -1) /*error*/ exit_error(1);
            flagRecvFromAS = false;
            signal_count = 0;
            buffer[n]='\0';

            // RRG status
            sscanf(buffer,"%s %s", command, status);

            if (strcmp(command,"RRG") == 0) { 
                if (strcmp(status, "OK") == 0 ) {
                    printf("Registration successful.\n");
                    *user_reg = 1;
                
                } else if (strcmp(status, "NOK") == 0) {
                    printf("Registration not accepted, invalid UID.\n");
                }
            } else{
                // ERR message
                exit_error(1); 
            }
        }

    } 
    
    else if (strcmp(command,"exit") == 0) {
        // exit

        if (word_count(buffer) != 1){
            printf("Wrong format input, format: exit\n");
            free(command);
            free(status);
            return;
        }

        else if (user_reg) { 
            // User is registered
            
            // UNR UID pass
            sprintf(buffer, "UNR %s %s\n", UID, pass);

            flagSendToAS = true;
            n = sendto(fd_client, buffer, strlen(buffer), 0, res_C->ai_addr, res_C->ai_addrlen); // command to res
            if(n == -1) /*error*/ exit_error(1);
            flagSendToAS = false;
            signal_count = 0;
            
            addrlen_C = sizeof(addr_C);

            flagRecvFromAS = true;
            n = recvfrom (fd_client, buffer, strlen(buffer), 0, (struct sockaddr*)&addr_C, &addrlen_C); 
            if(n == -1) /*error*/ exit_error(1);
            flagRecvFromAS = false;
            signal_count = 0;
            buffer[n]='\0';

            printf("Received from AS -%s-\n",buffer);

            // RUN status
            sscanf(buffer, "%s %s", command, status);
            if (strcmp(command, "RUN") == 0) {
                if (strcmp(status, "OK") == 0) {
                    printf("Request successful.\n");
                } else if (strcmp(status, "NOK") == 0) {
                    printf("Request unsuccessful.\n");
                }
            } else{
                printf("User was not registered\n");
            }
        }

        freeaddrinfo(res_C);
        freeaddrinfo(res_S);
        close (fd_client);
        close (fd_server);
        exit(0); 

    }  else {
        printf("Wrong command format\n");
    }
}

int main(int argc, char* argv[]) {    
    char UID[6]="", pass[9]="";
    int user_reg = 0;

    parseArgs(argc, argv);
    
    signal(SIGINT, handle_CtrlC);

    // Connections
    connectClient_UDP();
    connectServer_UDP();

    while(true) {
        FD_ZERO(&inputs);
        FD_SET(fd_server, &inputs); // server udp
        FD_SET(0, &inputs); // stdin
        maxfd = fd_server;
        
        out_fds = select(maxfd+1, &inputs, (fd_set*)NULL, (fd_set*)NULL, NULL); 
        if (out_fds <= 0) /*error*/ exit_error(1);

        // START TIMER
        signal_count=0;
        done= false;
        signal( SIGALRM, handle_alarm );
        alarm(3);

        if (FD_ISSET(fd_server, &inputs)) { 
            serverPD( UID);
        }

        if (FD_ISSET(0, &inputs)) {     
            clientPD( UID, pass, &user_reg);
        }

        // END TIMER
        done = true;
        alarm(0);
        
    }

    return 0;
}