#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/select.h>
#include <stdbool.h>
#include <time.h>
#include <sys/stat.h>
#include <ctype.h>
#include <dirent.h>
#include <signal.h>
#include "utils.h"
#define true 1
#define false 0
#define max(A,B) ((A) >= (B) ? (A) : (B))

// GLOBAL STRINGS

char* ASport = NULL;

char PDIP[32];
char PDport[8];
char buffer[1280];

// Flags
int flagSendToPD = false;
int flagRecvFromPD = false;
int flagSendToSERVER = false;
int flagRecvFromSERVER = false;
int done = false;
int signal_count=0;
int clientsFD[MaxClients], usersConnected_UID[MaxClients];


// Sockets variables
struct stat st = {0};
int fd_TCP, fd_UDP, newfd, fd_C;
int errcode;
ssize_t n;
socklen_t addrlen_TCP,addrlen_UDP, addrlen_C;
struct addrinfo hints_TCP,*res_TCP, hints_UDP, *res_UDP, hints_C, *res_C;
struct sockaddr_in addr_TCP, addr_UDP, addr_C;
fd_set inputs;
int maxfd, out_fds;
ssize_t nwritten, nread, nbytes, nleft;

// *******************
// READ COMMAND 
// *******************

int parseArgs (int argc, char* argv[], int helpPrints){
    int i;

    if (argc > 4 || argc < 1) {
        fprintf(stderr, "Invalid format, format -> ./AS [-p ASport] [-v]\n");
        exit(1);
    }

    for(i = 1; i < argc ; i++) {
        if (strcmp("-p", argv[i]) == 0) {
            ASport = argv[i+1];
        }

        if (strcmp("-v", argv[i]) == 0) {
            helpPrints = true;
        }
    }

    if ( ASport == NULL ) {
        ASport = "58027";
    }
    return helpPrints;
}

// *******************
// EXIT PROGRAM
// *******************

// Exit program correctly
void exit_error(int err){
    perror("Error, forced to exit");
    freeaddrinfo(res_UDP);
    freeaddrinfo(res_TCP);
    close (fd_UDP);
    close (fd_TCP);
    if (err==2){
        // Error Communicate with PD
        freeaddrinfo(res_C);
        close (fd_C);
    }
    exit(1);
}

// Exit by ctrl+c
void handle_CtrlC(){
    printf("Program exit by Ctrl+C\n");   
    freeaddrinfo(res_TCP);
    freeaddrinfo(res_UDP);
    close(fd_TCP);
    close(fd_UDP);
    exit(0); 

}

// Timeout 
void handle_alarm(){
    if (!done){
        signal( SIGALRM, handle_alarm );
        alarm(3);

        if (signal_count == 2){
            if (flagSendToPD || flagRecvFromPD){
                printf("Timeout SIGALARM detected communicating with PD\n");
                exit_error(2);
            } else{
                printf("Timeout SIGALARM detected\n");
                exit_error(1);
            }
        } else if (flagSendToPD){
            signal_count++;
            n = sendto(fd_C, buffer, strlen(buffer), 0, res_C->ai_addr, res_C->ai_addrlen);
            if (n == -1) exit_error(2);

        } else if (flagRecvFromPD){
            signal_count++;
            n = recvfrom (fd_C,buffer,128,0,(struct sockaddr*)&addr_C,&addrlen_C); 
            if (n == -1) exit_error(2);

        } else if (flagSendToSERVER){
            signal_count++;
            n = sendto(fd_UDP,buffer, strlen(buffer), 0, (struct sockaddr*)&addr_UDP, addrlen_UDP);
            if (n == -1) /*error*/ exit_error(1);  

        } else if (flagRecvFromSERVER){
            signal_count++;
            n = recvfrom (fd_UDP,buffer, 128, 0, (struct sockaddr*)&addr_UDP, &addrlen_UDP);
            if(n == -1) /*error*/ exit_error(1);

        }else{
            signal_count++;
        }
    } 
}

// *******************
// SOCKETS CONNECTIONS
// *******************

void connect_serverTCP(){
    // SERVER TCP
    fd_TCP = socket(AF_INET,SOCK_STREAM,0);
    if (fd_TCP == -1) /*error*/ exit(1);
    memset(&hints_TCP, 0, sizeof hints_TCP);
    hints_TCP.ai_family = AF_INET;
    hints_TCP.ai_socktype = SOCK_STREAM; // TCP SOCKET
    hints_TCP.ai_flags = AI_PASSIVE;

    errcode = getaddrinfo(NULL,ASport,&hints_TCP,&res_TCP);
    if ((errcode) != 0) /*error*/ exit(1);

    n = bind(fd_TCP,res_TCP->ai_addr,res_TCP->ai_addrlen);
    if (n == -1) /*error*/ exit(1);

    if (listen(fd_TCP,5) == -1) /*error*/ exit(1);
}

void connect_serverUDP(){
    //SERVER UDP
    fd_UDP=socket(AF_INET,SOCK_DGRAM,0);
    if(fd_UDP==-1) /*error*/exit(1);

    memset(&hints_UDP, 0, sizeof hints_UDP);
    hints_UDP.ai_family=AF_INET;                 
    hints_UDP.ai_socktype=SOCK_DGRAM; // UDP socket
    hints_UDP.ai_flags=AI_PASSIVE;

    errcode = getaddrinfo(NULL,ASport,&hints_UDP,&res_UDP); 
    if (errcode != 0) /*error*/ exit(1);

    n = bind (fd_UDP,res_UDP->ai_addr, res_UDP->ai_addrlen);
    if (n == -1) /*error*/ exit(1);
}

void connectClient_UDP(){
    fd_C = socket(AF_INET,SOCK_DGRAM, 0);
    if (fd_C == -1) /*error*/ exit(1);

    memset(&hints_C, 0, sizeof hints_C);
    hints_C.ai_family = AF_INET;                 
    hints_C.ai_socktype = SOCK_DGRAM; // UDP socket

    errcode = getaddrinfo(PDIP, PDport, &hints_C, &res_C);
    if (errcode != 0) /*error*/ exit(1);
    
}

void accept_connection_TCP(){  

    addrlen_TCP = sizeof(addr_TCP);
    if ((newfd = accept(fd_TCP, (struct sockaddr*)&addr_TCP, &addrlen_TCP)) == -1) /*error*/ exit_error(1);
    
    for (int i=0 ; i < MaxClients; i++){
        if (clientsFD[i] == 0){
            clientsFD[i] = newfd;
            break;
        }

    }
    
}

//************************
// COMMUNICATE WITH USER
//************************
void communications_User(int i, int helpPrints){

    char *arg1=(char*)malloc(sizeof(char)*32);
    char *arg2=(char*)malloc(sizeof(char)*32);
    char *arg3=(char*)malloc(sizeof(char)*32);
    char *pass=(char*)malloc(sizeof(char)*9);
    char *fname=(char*)malloc(sizeof(char)*25);
    char *fop=(char*)malloc(sizeof(char)*2);
    char *UID=(char*)malloc(sizeof(char)*6);
    char *command=(char*)malloc(sizeof(char)*8);
    char *RID=(char*)malloc(sizeof(char)*5);
    char *path=(char*)malloc(sizeof(char)*64);
    char * ptr;
    
    
    
    ptr=buffer;
    while(1){
        nread = read(clientsFD[i], ptr, 100); 
        if(nread == -1) /*error*/ exit_error(1); 
        else if(nread==0){          
            // USER LOGS OUT
            sprintf(path,"USERS/%d/%d_login.txt", usersConnected_UID[i], usersConnected_UID[i]);
            if (usersConnected_UID[i]!= 0){
                if ( !rmFile(path)) exit_error(1);
            }
            usersConnected_UID[i] = 0;
            clientsFD[i]=0;
            
            free(UID);
            free(command);
            free(RID);
            free(fop);
            free(path);
            free(fname); 
            free(pass);        
            free(arg1); 
            free(arg2);  
            free(arg3);    
            return;
            
        }
        if(ptr[nread-1]=='\n') break;
        ptr+=nread;
    }
    ptr[nread]='\0';

    // LOG UID PASS
    // REQ UID RID Fop [Fname]
    // AUT UID RID VC
    sscanf(buffer,"%s %s %s %s %s", command, UID, arg1, arg2, arg3); 

    printf("buffer recebido do User->%s-\n",buffer);

    if (strcmp(command,"LOG") == 0) {
        // LOG UID PASS
        if ( word_count(buffer) != 3 || !stringNumbers(UID,5) || !stringNumbers_Letters(arg1,8)){
            sprintf(buffer,"ERR\n");
        } else{

            sprintf(path,"USERS/%s",UID);
            if (stat(path, &st) == -1) { 
                // Users not registed
                if(helpPrints) printf("User: login failed, UID= %s\n", UID);
                sprintf(buffer,"RLO ERR\n");
            } else{
                // UID Confirmed
                sprintf(fname,"%s_pass.txt",UID);
                sprintf(path, "USERS/%s/%s", UID, fname);
                if( !file_exists(path) ) exit_error(1);
                FILE * f_pass = fopen(path, "r");
                if (f_pass==NULL) exit_error(1);
                fscanf(f_pass,"%s",pass);
                fclose(f_pass);

                if (strcmp(pass,arg1)!=0){
                    if(helpPrints) printf("User: login failed, UID= %s\n", UID);
                    sprintf(buffer,"RLO NOK\n"); // UID-> OK, pass-> NOK
                } else{
                    // PASSWORD Confirmed

                    //Login FILE
                    sprintf(fname,"%s_login.txt",UID);
                    sprintf(path, "USERS/%s/%s", UID, fname);
                    FILE * file_login = fopen(path, "w");
                    if (file_login==NULL) exit_error(1);
                    fclose(file_login);
                    
                    usersConnected_UID[i] = atoi(UID);
                    sprintf(buffer,"RLO OK\n");
                    if(helpPrints) printf("User: login ok, UID= %s\n", UID);
                                                
                }
            }
        }
        
    } else if (strcmp(command,"REQ") == 0) {
        // REQ UID RID Fop [Fname]
        int word_number = correctMessage(arg2,buffer);   
        if (word_number == -1 ){
                sprintf(buffer, "RRQ ERR\n");       
        } else if( word_number==5 && !correctFilename( arg3) ){
            sprintf(buffer, "RRQ ERR\n");
        } else if( !stringNumbers(UID,5) ) {
            sprintf(buffer, "RRQ EUSER\n");
        } else if( !stringFOP(arg2) ){
            sprintf(buffer, "RRQ EFOP\n");
        } else{

            sprintf(fname,"%s_login.txt",UID);
            sprintf(path, "USERS/%s/%s", UID, fname);
            if( access( path, F_OK ) == -1 ){  
                // NOT lOGIN YET
                sprintf(buffer,"RRQ ELOG\n"); 
            } else{
                

                int numberVC = 1000+(rand()%9000);

                //VC FILE
                sprintf(path,"USERS/%s/%s_vc.txt",UID,UID);
                FILE * file_vc = fopen(path, "w");
                if (file_vc==NULL) exit_error(1);
                fprintf(file_vc, "%d\n", numberVC);
                fclose(file_vc);

                //TID FILE - [TID] Fop Fname
                sprintf(path,"USERS/%s/%s_tid.txt",UID,UID);
                FILE * file_tid = fopen(path, "w");
                if (file_tid==NULL) exit_error(1);
                if (word_number == 5){
                    strcpy(fop,arg2);
                    strcpy(fname,arg3);
                    fprintf(file_tid, "%s %s\n",fop,fname);
                } else if(word_number == 4){
                    strcpy(fop,arg2);
                    fprintf(file_tid, "%s\n",fop);
                }
                fclose(file_tid);

                //RID FILE - RID
                sprintf(path,"USERS/%s/%s_rid.txt",UID,UID);
                FILE * file_rid = fopen(path, "w");
                if (file_rid==NULL) exit_error(1);
                strcpy(RID,arg1);
                fprintf(file_rid, "%s\n",RID);
                fclose(file_rid);


                // Get PDIP and PDPORT
                sprintf(path,"USERS/%s/%s_reg.txt",UID,UID);
                if( !file_exists(path) ){ 
                    sprintf(buffer,"RRQ EPD\n"); // cant communicate with pd  
                } else{
                
                    FILE * f_reg = fopen(path, "r");  
                    if (f_reg==NULL) exit_error(1);                   
                    fscanf(f_reg,"%s %s",PDIP,PDport);
                    fclose(f_reg); 

                    // AS Communicate with PD          
                    
                    // VLC UID VC Fop [Fname]
                    if (word_number == 5){
                        sprintf(buffer,"VLC %s %d %s %s\n", UID, numberVC, fop, fname);
                    } else if(word_number == 4){
                        sprintf(buffer,"VLC %s %d %s\n", UID, numberVC, fop);
                    }

                    connectClient_UDP();

                    flagSendToPD=true;
                    n = sendto(fd_C, buffer, strlen(buffer), 0, res_C->ai_addr, res_C->ai_addrlen);
                    if (n == -1) exit_error(2);
                    flagSendToPD=false;
                    signal_count = 0;

                    printf("enviou para o PD --->%s\n",buffer);

                    addrlen_C = sizeof(addr_C);

                    flagRecvFromPD=true;                       
                    n = recvfrom (fd_C,buffer,128,0,(struct sockaddr*)&addr_C,&addrlen_C); 
                    if (n == -1) exit_error(2);
                    flagRecvFromPD = false;
                    signal_count = 0;
                    buffer[n] = '\0';


                    freeaddrinfo(res_C);
                    close(fd_C);


                    
                    // RVC UID status
                    sscanf(buffer,"%s %s %s",command, arg1, arg2); 
                    printf("recbeu do PD --->%s",buffer);

                    if ( strcmp(command,"RVC") == 0 && strcmp(arg1,UID) == 0){
                        if (strcmp(arg2,"OK")==0){
                            if(helpPrints){
                                if (word_number == 5){
                                    printf("User: request Fop: %s, Fname:%s, UID: %s, RID=%s, VC=%d\n", fop, fname, UID, RID, numberVC);
                                } else if(word_number == 4){
                                    printf("User: request Fop: %s, UID: %s, RID=%s, VC=%d\n", fop, UID, RID, numberVC);
                                }
                            } 
                            sprintf(buffer,"RRQ OK\n");
                        } else if (strcmp(arg2,"NOK")==0){
                            sprintf(buffer,"RRQ EUSER\n");
                        } else{
                            sprintf(buffer,"ERR\n");
                        }
                    } else{
                        sprintf(buffer,"ERR\n");
                    }
                }
    
            }

        }                             

    } else if (strcmp(command,"AUT") == 0) { 
        // AUT UID RID VC      
        if ( word_count(buffer) != 4 ){
            sprintf(buffer,"ERR\n");
        } else if ( !stringNumbers(UID,5) || !stringNumbers(arg1,4) || !stringNumbers(arg2,4)){
            sprintf(buffer,"ERR\n");
        } else {
            int nVC;

            // Get VC
            sprintf(path, "USERS/%s/%s_vc.txt", UID, UID);  
            if( !file_exists(path) ) exit_error(1);         
            FILE * file = fopen(path, "r");   
            if (file==NULL) exit_error(1); 
            fscanf(file,"%d",&nVC);
            fclose(file);

            // Check VC is correct
            if( nVC != atoi(arg2)){                      
                // Authentication failed
                nVC = 0;
                if (helpPrints) printf("User: UID=%s , VC is incorrect\n", UID); 
                sprintf(buffer,"RAU %d\n",nVC); 
            } else{                           
                
                // Create TID     
                int TID = 1000+(rand()%9000);
                
                // Get RID
                sprintf(path,"USERS/%s/%s_rid.txt", UID, UID);
                if( !file_exists(path) ) exit_error(1);
                FILE * f_rid = fopen(path, "r"); 
                if (f_rid==NULL) exit_error(1);                  
                fscanf(f_rid,"%s", arg2);
                fclose(f_rid);

                // Check RID is correct
                if ( strcmp(arg1,arg2)!=0 ){ 
                    sprintf(buffer,"ERR\n");
                } else{

                    // Get Fop Fname - tid.txt
                    sprintf(path,"USERS/%s/%s_tid.txt", UID, UID);
                    if( !file_exists(path) ) exit_error(1);
                    FILE * f = fopen(path, "r");
                    if (f==NULL) exit_error(1);
                    fscanf(f,"%s", arg2); // Fop 
                    if (arg2[0]== 'U' || arg2[0] == 'R' || arg2[0] == 'D'){
                        fscanf(f," %s", arg3); // Fop Fname
                    } 
                    fclose(f);
                    
                    // Write TID Fop Fname - tid.txt
                    FILE * file = fopen(path, "w");
                    if (file==NULL) exit_error(1);
                    if (arg2[0]== 'U' || arg2[0] == 'R' || arg2[0] == 'D'){
                        fprintf(file, "%d %s %s\n", TID,arg2,arg3); // TID Fop Fname
                    } else{
                        fprintf(file, "%d %s\n", TID,arg2); // TID Fop 
                    }                         
                    fclose(file);
                    
                    sprintf(buffer,"RAU %d\n", TID);
                                        
                    if(helpPrints){
                        if (arg2[0]== 'L' || arg2[0] == 'X'){
                            printf("User: UID=%s , Fop: %s, TID=%d\n", UID, arg2, TID);                                         
                        } else{
                            printf("User: UID=%s , Fop: %s, Fname: %s, TID=%d\n", UID, arg2, arg3, TID);
                        }
                    }
                }                         
            }
            
        }

    }  else{
        // Invalid Command
        sprintf(buffer,"ERR\n");
    }     

    // Write back to the User
    // RLO status
    // RRQ status
    // RAU TID
    // RVC UID status

    nleft= strlen(buffer);
    ptr = buffer;
    while(nleft>0) {
        nwritten = write(clientsFD[i],ptr,nleft); 
        if(nwritten == -1)/*error*/exit_error(1);
        nleft-=nwritten;
        ptr+=nwritten;
    }
    
    printf("envou para user ->%s-\n",buffer);

    free(UID);
    free(command);
    free(RID);
    free(fop);
    free(path);
    free(fname);   
    free(pass);
    free(arg1);
    free(arg2);
    free(arg3);     
}

//************************
// COMMUNICATE WITH PD/FS
//************************

void communications_serverUDP(int helpPrints){

    char *arg1=(char*)malloc(sizeof(char)*32);
    char *arg2=(char*)malloc(sizeof(char)*32);
    char *arg3=(char*)malloc(sizeof(char)*32);
    char *fop=(char*)malloc(sizeof(char)*2);
    char *fname=(char*)malloc(sizeof(char)*25);
    char *command = (char*)malloc(sizeof(char)*8);
    char *UID = (char*)malloc(sizeof(char)*6);
    char *pass = (char*)malloc(sizeof(char)*9);
    char *path=(char*)malloc(sizeof(char)*64);


    addrlen_UDP = sizeof(addr_UDP);
    flagRecvFromSERVER=true;
    n = recvfrom (fd_UDP,buffer, 128, 0, (struct sockaddr*)&addr_UDP, &addrlen_UDP);
    if(n == -1) /*error*/ exit_error(1);
    flagRecvFromSERVER=false;
    signal_count = 0;
    buffer[n]='\0';

    // REG UID pass PDIP PDport
    // VLD UID TID
    // UNR UID pass
    sscanf(buffer,"%s %s %s %s %s", command, UID, arg1, arg2, arg3);

    printf("buffer recebido FS ou PD ->%s-\n",buffer);

    if (strcmp(command,"REG") == 0) { 
        // REG UID pass PDIP PDport

        if( !stringNumbers(UID,5) || !stringNumbers_Letters(arg1,8)) {
            sprintf(buffer,"REG NOK\n");
        } else{

            // Directory USERS/UID
            sprintf(path,"USERS/%s",UID);    
            if ( !newDirectory(path) ) exit_error(1);

            //Create pass.txt
            sprintf(path,"USERS/%s/%s_pass.txt", UID, UID);
            FILE * f = fopen( path, "w");
            if (f==NULL) exit_error(1);
            fprintf(f, "%s\n", arg1);
            fclose(f);

            //Create reg.txt - PDIP PDport
            sprintf(path,"USERS/%s/%s_reg.txt", UID, UID);
            FILE * file = fopen( path, "w");
            if (file==NULL) exit_error(1);
            fprintf(file, "%s %s\n", arg2,arg3);
            fclose(file);

            sprintf(buffer,"RRG OK\n");
            if (helpPrints) printf("PD: new user, UID= %s\n",UID);
            
        }
    } else if (strcmp(command,"UNR") == 0) { 
        // UNR UID pass
        
        
        if ( word_count(buffer) != 3 ){
            sprintf(buffer,"ERR\n");
        } else if( !stringNumbers(UID,5) || !stringNumbers_Letters(arg1,8)) {
            sprintf(buffer,"ERR\n");
        } else{

            sprintf(path,"USERS/%s",UID);

            if (stat(path, &st) == -1) { 
                // User was removed
                sprintf(buffer,"RUN NOK\n");
                if (helpPrints) printf("PD: unregister user failed, UID= %s\n", UID);
            } else{

                sprintf(fname,"%s_pass.txt",UID);
                sprintf(path, "USERS/%s/%s", UID, fname);
                if( !file_exists(path) ) exit_error(1);
                FILE * file = fopen( path, "r");
                if (file==NULL) exit_error(1);
                fscanf(file,"%s", pass); 
                fclose(file);
                

                
                if (strcmp(arg1,pass)!=0){ 
                    // Pass NOK                 
                    sprintf(buffer,"RUN NOK\n");
                    if (helpPrints) printf("PD: unregister user failed, UID= %s\n", UID);
                } else {   
                    
                    // Remove reg.txt file
                    sprintf(path,"USERS/%s/%s_reg.txt",UID,UID);
                    if( !file_exists(path) ) exit_error(1);
                    if ( !rmFile(path)) exit_error(1);

                    if (helpPrints) {                                  
                        printf("PD: unregister user, UID= %s\n", UID);
                    }    
                    sprintf(buffer,"RUN OK\n");    
                    
                } 
                
            }
        }
    } else if( strcmp(command,"VLD") == 0){ 
        // VLD UID TID
        
        // Get TID Fop Fname - tid.txt
        sprintf(path, "USERS/%s/%s_tid.txt", UID, UID);
        if( !file_exists(path) ) exit_error(1);
        FILE * file = fopen( path, "r");
        if (file==NULL) exit_error(1);
        fscanf(file,"%s %s %s",arg3,fop,fname);
        fclose(file);

        
        if ( word_count(buffer) != 3 ){
            sprintf(buffer,"ERR\n");
        } else if( !stringNumbers(UID,5) || !stringNumbers(arg1,4)) {
            sprintf(buffer,"ERR\n");
        } if (strcmp(arg1,arg3)==0){  
            // TID correct
            if (!correctFilename(fname)){
                sprintf(buffer,"ERR\n"); 
            } else if (fop[0] == 'X'){
                // Removes Files and directory from UID

                sprintf(path,"USERS/%s",UID);
                if ( !rmFiles(path) ) exit_error(1); 
                if ( !rmDirectory(path)) exit_error(1);
                
                for (int j=0; j < MaxClients ;j++){
                    if ( usersConnected_UID[j] == atoi(UID)){
                        usersConnected_UID[j] = 0; // user not logged in
                    }
                }
                
                // CNF UID TID Fop [Fname]
                sprintf(buffer,"CNF %s %s %s\n",UID, arg1, fop);
                if(helpPrints) printf ("FS: validated for UID=%s, TID=%s , fop=%s\n", UID, arg1, fop);
                        
            } else if (fop[0] == 'U' || fop[0]=='D' || fop[0]== 'R'){
                sprintf(buffer,"CNF %s %s %s %s\n",UID, arg1, fop, fname);
                if(helpPrints) printf ("FS: validated for UID=%s, TID=%s , fop=%s, fname:%s\n", UID, arg1, fop, fname);
            } else if (fop[0] == 'L'){
                sprintf(buffer,"CNF %s %s %s\n",UID, arg1, fop);
                if(helpPrints) printf ("FS: validated for UID=%s, TID=%s , fop=%s\n", UID, arg1, fop);
            } else{
                // Fop Wrong
                sprintf(buffer,"ERR\n");
            }
        } else{
            // TID incorrect
            sprintf(buffer,"CNF %s %s E\n",UID, arg1);
        }      
            
    } else {
        // Invalid message
        sprintf(buffer,"ERR\n");
    }
    
    // RRG status
    // CNF UID TID Fop [Fname]
    // RUN status
    flagSendToSERVER=true;
    n = sendto(fd_UDP,buffer, strlen(buffer), 0, (struct sockaddr*)&addr_UDP, addrlen_UDP);
    if (n == -1) /*error*/ exit_error(1);   
    flagSendToSERVER=false;
    signal_count = 0;

    free(pass);
    free(path);
    free(command);
    free(UID);
    free(fop);
    free(fname);
    free(arg1);
    free(arg2);
    free(arg3);
}


int main(int argc, char* argv[]) {
    int helpPrints = false;
    
    newDirectory("USERS");
    
    helpPrints = parseArgs(argc, argv, helpPrints);

    // Initialize arrays
    for(int j=0; j < MaxClients; j++){
        clientsFD[j] = 0;
        usersConnected_UID[j] = 0;
    }

    signal(SIGINT, handle_CtrlC);
        
    // connections
    connect_serverTCP();
    connect_serverUDP();

    while(1) {
        FD_ZERO(&inputs);
        FD_SET(fd_TCP,&inputs); // server TCP
        FD_SET(fd_UDP,&inputs); // server UDP
        maxfd = max(fd_TCP,fd_UDP);
        
        for (int i = 0; i < MaxClients; i++){
            if (clientsFD[i] != 0){
                FD_SET(clientsFD[i], &inputs);
                maxfd = max(maxfd,clientsFD[i]);
            }
        }
        
        out_fds = select(maxfd+1, &inputs, (fd_set*)NULL, (fd_set*)NULL, NULL);
        if (out_fds <= 0) /* error*/ exit_error(1);
        
        // START TIMER
        signal_count=0;
        done= false;
        signal( SIGALRM, handle_alarm );
        alarm(3);
        

        if (FD_ISSET(fd_TCP,&inputs)) {
            accept_connection_TCP();
        }

        for (int i = 0; i < MaxClients; i++) { 
            if (clientsFD[i] != 0){ 
                if (FD_ISSET(clientsFD[i], &inputs)) {
                    // Communicate with User
                    communications_User( i, helpPrints);
                
                }
            }
        }
    
        if (FD_ISSET(fd_UDP, &inputs)){
            // Communicate with PD or FS
            communications_serverUDP(helpPrints);                            
        }

        // END TIMER
        done = true;
        alarm(0);

        
    }
    return 0;
}