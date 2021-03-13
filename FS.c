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

char* ASIP = NULL;
char* FSIP = NULL;
char* ASport = NULL;
char* FSport = NULL;
char* PDIP = NULL;
char* PDport = NULL;
char buffer[2048];

int helpPrints = false;

// Sockets variables
int errcode;
ssize_t n;
socklen_t addrlen_TCP, addrlen_UDP;
struct addrinfo hints_TCP, *res_TCP, hints_UDP, *res_UDP;
struct sockaddr_in addr_TCP, addr_UDP;
struct stat st = {0};

// Flags
int clientsFD[MaxClients];
int done;
int signal_count=0;
int flagSendToAS= false;
int flagRecvFromAS = false;


// "select" variables
int clientsFD[MaxClients], numClients = 0;
int fd_TCP, fd_UDP, newfd;
fd_set inputs;
int maxfd, out_fds;

// *******************
// READ COMMAND 
// *******************

void parseArgs (int argc, char* argv[]){
    int i;

    if (argc > 8 || argc < 1) {
        fprintf(stderr, "Invalid format, format -> ./FS [-q FSport] [-n ASIP] [-p ASport] [-v]\n");
        exit(1);
    }

    for(i = 1; i < argc ; i++) {
        if (strcmp("-q", argv[i]) == 0) {
            FSport = argv[i+1];
        }

        if (strcmp("-n", argv[i]) == 0) {
            ASIP = argv[i+1];
        }

        if (strcmp("-p", argv[i]) == 0) {
            ASport = argv[i+1];
        }

        if (strcmp("-v", argv[i]) == 0) {
            helpPrints = true;
        }
    }

    if (FSport == NULL) {
         FSport = "59027";
    }

    if (ASIP == NULL) {
        ASIP = "127.0.0.1"; 
    }
    
    if (ASport == NULL) {
        ASport = "58027"; 
    }
}

// *******************
// EXIT PROGRAM
// *******************

// Exit program correctly
void exit_error(int n){
    perror("Error, forced to exit");
    freeaddrinfo(res_UDP);
    freeaddrinfo(res_TCP);
    close (fd_UDP);
    close (fd_TCP);
    exit(n);
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

// Timeout program
void handle_alarm(){
    if (!done){
        signal( SIGALRM, handle_alarm );
        alarm(3);

        if (signal_count == 2){
            if (flagSendToAS || flagRecvFromAS){
                printf("Timeout SIGALARM detected communicating with AS\n");
                exit_error(2);
            } else{
                printf("Timeout SIGALARM detected\n");
                exit_error(1);
            }
        } else if (flagSendToAS){
            signal_count++;
            n = sendto(fd_UDP, buffer, strlen(buffer), 0, res_UDP->ai_addr, res_UDP->ai_addrlen);
            if (n == -1) /*error*/ exit_error(1);
        } else if (flagRecvFromAS){
            signal_count++;
            n = recvfrom(fd_UDP, buffer, 128, 0,(struct sockaddr*)&addr_UDP, &addrlen_UDP);
            if (n == -1) /*error*/ exit_error(1);
        } else{
            signal_count++;
        }
    } 
}

// *******************************
// FILES AND DIRECTORIES FUNCTIONS
// *******************************

int listFiles(char *path, char *fileList) {
    
    DIR *folder;
    struct dirent *next_file;
    char* filepath = (char*) malloc(300*sizeof(char));
    int fileCount = 0;
    fileList[0] = '\0';

    if ((folder = opendir(path)) != NULL) {

        while ((next_file = readdir(folder)) != NULL) {
            // build the path for each file in the folder
            if (next_file->d_name[0] == '.') {
                continue;
            }

            sprintf(filepath, "%s/%s", path, next_file->d_name);
            FILE * fp = fopen(filepath, "r");
            if (fp == NULL) exit_error(1);

            char *fsize=(char*)malloc(sizeof(char)*10);

            strcat(fileList, " ");
            strcat(fileList, next_file->d_name);
            strcat(fileList, " ");

            fseek(fp, 0L, SEEK_END); 
            sprintf(fsize, "%ld", ftell(fp));
            strcat(fileList, fsize);
            fileCount++;
            free(fsize);
            
            fclose(fp);
        }
        strcat(fileList, "\n");
        closedir(folder);

    } else {
        /* could not open directory */
        printf("Could not open directory\n");
        free(filepath);
        return -1;
    }

    free(filepath);
    return fileCount;
}

int uploadFile(int fd_client, char * fname, char * path) {    
    char* fsize = (char*) malloc(10*sizeof(char));
    char* filepath = (char*) malloc(64*sizeof(char));
    char* msg = (char*) malloc(512*sizeof(char));
    FILE* fp;
    ssize_t nread, nbytes, nleft;
    
    //read fname
    while(1){
        nread = read(fd_client, msg, strlen(fname)+1);
        if (msg[nread-1] == ' ') {
            break;
        }
        msg += nread;
    }
    msg[nread] = '\0';
    
    //read fsize
    int i = 0;
    while(1) {
        nread = read(fd_client, fsize, 1);
        if (fsize[nread-1] == ' ') {
            break;
        }
        fsize += nread;
        i += 1;
    }
    fsize -= i;
    fsize[i] = '\0';
    nbytes = atoi(fsize);
    
    //read data
    nleft = nbytes;
    sprintf(filepath, "%s/%s", path, fname);
    fp = fopen(filepath, "w");
    if (fp == NULL) exit_error(1);

    while(nleft > 0) {
        nread = read(fd_client, msg, nleft);
        nleft -= nread;
        fwrite(msg, 1, nread, fp);
    }

    fclose(fp);
    free(fsize);
    free(msg);
    return true;
}

int retrieveFile(int fd_client, char *fname, char* path) {
    char* msg = (char*)malloc(70*sizeof(char));
    char* data = (char*)malloc(512*sizeof(char));
    char* fsize = (char*)malloc(10*sizeof(char));
    char* filepath = (char*)malloc(32*sizeof(char));
    char* ptr;
    int i, status = false;
    ssize_t nleft, nread, nwritten; 
    
    sprintf(filepath, "%s/%s", path, fname);

    FILE * fp = fopen(filepath, "r"); 
    if (fp == NULL) exit_error(1);

    if(!file_exists(filepath)) {
        sprintf(msg, "RRT EOF\n");
    } else if (dir_exists(path) && fileCount(path) == 0) {
        sprintf(msg, "RRT NOK\n");
    } else {
        // Get Fsize
        fseek(fp, 0L, SEEK_END); 
        sprintf(fsize, "%ld", ftell(fp)); 
        // Sends first part of the message to User
        sprintf(msg, "RRT OK %s", fsize);
        status = true;
    }
    
    nleft = strlen(msg);
    ptr = msg;
    while (nleft > 0) {
        nwritten = write(fd_client,ptr,nleft);
        if (nwritten==-1) /*error*/ exit_error(1);
        nleft -= nwritten;
        ptr += nwritten;
    }
    write(fd_client, " ", 1);

    if (status) {
        // Receive data from file and send it to User
        fseek(fp, 0L, SEEK_SET);
        nleft = atoi(fsize);

        while (nleft > 0) {
            // Receive from file
            if (nleft > 512) {
                nread = fread(data, 1, 512, fp);
            } 
            else if (nleft <= 512) {
                nread = fread(data, 1, nleft, fp);
            }
            nleft -= nread;
            i = nread;
            while(i > 0) {
                // Send to User
                nwritten = write(fd_client, data, nread);
                i -= nwritten;
                data += nwritten;
            }
            data -= nread;
        }

        write(fd_client, "\n", 1);
        fclose(fp);
        free(data);
        free(msg);
        free(fsize);
        free(filepath);
        return true;

    } else {
        fclose(fp);    
        free(data);
        free(msg);
        free(fsize);
        free(filepath);
        return -1;
    }
}

// *******************
// SOCKETS CONNECTIONS
// *******************

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

void connect_serverTCP(){
    // SERVER TCP
    fd_TCP = socket(AF_INET,SOCK_STREAM,0);
    if (fd_TCP == -1) /*error*/ exit(1);
    memset(&hints_TCP, 0, sizeof hints_TCP);
    hints_TCP.ai_family = AF_INET;
    hints_TCP.ai_socktype = SOCK_STREAM; // TCP SOCKET
    hints_TCP.ai_flags = AI_PASSIVE;

    errcode = getaddrinfo(NULL,FSport,&hints_TCP,&res_TCP);
    if ((errcode) != 0) /*error*/ exit(1);

    n = bind(fd_TCP, res_TCP->ai_addr, res_TCP->ai_addrlen);
    if (n == -1) /*error*/ exit(1);

    if (listen(fd_TCP,5) == -1) /*error*/ exit(1);

}

void connect_clientUDP(){
    //CLIENT UDP
    fd_UDP = socket(AF_INET,SOCK_DGRAM, 0);
    if (fd_UDP == -1) /*error*/ exit(1);

    memset(&hints_UDP, 0, sizeof hints_UDP);
    hints_UDP.ai_family = AF_INET;                 
    hints_UDP.ai_socktype = SOCK_DGRAM; // UDP socket
    errcode = getaddrinfo(ASIP, ASport, &hints_UDP, &res_UDP); // ligacao ao AS
    if (errcode != 0) /*error*/ exit(1);

}

void handleClient(int i, char * users_dir) {

    ssize_t nleft, nwritten; 
    int error = false;
    char *ptr = buffer;

    int j = 15;
    while (j > 0) {
        n = read(clientsFD[i], ptr, j);  
        if (n == -1) /*error*/ exit_error(1); 
        else if (n == 0) { // closed by peer         
            // USER LOGS OUT
            clientsFD[i] = 0;
            continue;     
        }
        j -= n;
        ptr += n;
    }
    ptr[0]='\0';
    
    printf("leu do user %sx", buffer); //!!!

    char *command=(char*)malloc(sizeof(char)*10);
    char *UID=(char*)malloc(sizeof(char)*10);
    char *TID=(char*)malloc(sizeof(char)*10);
    char *fname=(char*)malloc(sizeof(char)*25);
    char *fop=(char*)malloc(sizeof(char)*10);
    char *CNF=(char*)malloc(sizeof(char)*10);
    char *response=(char*)malloc(sizeof(char)*512);
    char *fileList=(char*)malloc(sizeof(char)*512);
    char *path=(char*)malloc(sizeof(char)*32);
    char *filepath=(char*)malloc(sizeof(char)*64);
    char *status=(char*)malloc(sizeof(char)*8);

    sscanf(buffer,"%s %s %s", command, UID, TID);

    if (!stringNumbers(UID, 5)) {
        printf("Wrong UID format received (%ld)\n", strlen(UID));
        error = true;
    }
    if (!stringNumbers(TID, 4)) {
        printf("Wrong TID format received\n");
        error = true;
    }
    
    if (!error) {

        sprintf(buffer,"VLD %s %s\n", UID, TID);
        printf("enviar para o as-%sx\n", buffer); //!!!

        flagSendToAS = true;
        n = sendto(fd_UDP, buffer, strlen(buffer), 0, res_UDP->ai_addr, res_UDP->ai_addrlen);
        if (n == -1) /*error*/ exit_error(1);
        flagSendToAS = false;
        signal_count = 0;


        addrlen_UDP = sizeof(addr_UDP);
        flagRecvFromAS = true;
        n = recvfrom(fd_UDP, buffer, 128, 0,(struct sockaddr*)&addr_UDP, &addrlen_UDP);
        if (n == -1) /*error*/ exit_error(1);
        flagRecvFromAS = false;
        signal_count = 0;
        buffer[n]='\0';
        printf("recebeu do as-%sx\n", buffer); //!!!


        sscanf(buffer,"%s %s %s %s %s", CNF, UID, TID, fop, fname);

        int word_number = correctMessage(fop,buffer);
        if (word_number == -1){
            printf("Wrong input format\n");
            error = true;
        }

        if (strcmp(CNF, "CNF") != 0) {
            printf("Wrong CNF format received\n");
            error = true;
        }
        if (!stringNumbers(UID, 5)) {
            printf("Wrong UID format received\n");
            error = true;
        }
        if (!stringNumbers(TID, 4)) {
            printf("Wrong TID format received\n");
            error = true;
        }
        if (!stringFOP(fop)) {
            printf("Wrong Fop format received\n");
            error = true;
        }
        if ( word_number == 5 && !correctFilename(fname)) {
            printf("Wrong file name format received\n");
            error = true;
        }
        

        if (!error) {
            
            if (strcmp(command, "LST") == 0) {
                sprintf(path,"%s/%s", users_dir, UID);
                if (strcmp(fop, "E") == 0)
                    sprintf(response, "RLS INV");

                else if (strcmp(fop, "L") == 0){
                    if (!dir_exists(path)) sprintf(response, "RLS EOF");
                    else {
                        int fc = listFiles(path, fileList);
                        if (fc == 0) sprintf(response, "RLS EOF");
                        else if (fc < 0){
                            sprintf(response, "RLS ERR");
                        }
                        else {
                            sprintf(response, "RLS %d%s", fc, fileList);
                            if(helpPrints) printf("User: %s, fop: %s, files list sent\n", UID, fop);
                        }
                    }
                } else{
                    sprintf(response, "RLS ERR");
                }
            } else if (strcmp(command, "RTV") == 0) {

                sprintf(path,"%s/%s", users_dir, UID);
                if (strcmp(fop, "E") == 0) {
                    sprintf(status, "INV");
                    sprintf(buffer, "RRT %s\n", status);

                    nleft = strlen(buffer);
                    ptr = buffer;
                    while (nleft > 0) {
                        nwritten = write(clientsFD[i], ptr, nleft);
                        if (nwritten == -1) /*error*/ exit_error(1);
                        nleft -= nwritten;
                        ptr += nwritten;
                    }

                    if (n == -1) /*error*/ exit_error(1);
                } else if (strcmp(fop, "R") == 0) {
                    if (!retrieveFile(clientsFD[i], fname, path)) {
                        sprintf(status, "ERR");
                        sprintf(buffer, "RRT %s\n", status);

                        nleft = strlen(buffer);
                        ptr = buffer;
                        while (nleft > 0) {
                            nwritten = write(clientsFD[i], ptr, nleft);
                            if (nwritten == -1) /*error*/ exit_error(1);
                            nleft -= nwritten;
                            ptr += nwritten;
                        }

                    } else if (helpPrints) printf("User: %s, fop: %s, file sent: %s\n", UID, fop, fname);
                } else sprintf(status, "ERR");

            } else if (strcmp(command, "UPL") == 0) {

                // create USERS_FS/UID folder
                sprintf(path,"%s/%s", users_dir, UID);
                if (stat(path, &st) == -1) {
                    if(!newDirectory(path)) exit_error(1);
                }
                sprintf(filepath, "%s/%s", path, fname);
                if (strcmp(fop, "E") == 0)
                    sprintf(status, "INV");
                else if (access(filepath, F_OK) != -1 )
                    sprintf(status, "DUP");
                else if (fileCount(path) >= 15)
                    sprintf(status, "FULL");
                else if (!stringNumbers(UID, 5))
                    sprintf(status, "NOK");
                else if (strcmp(fop, "U") == 0) {
                    //upload file to USERS_FS/UID/fname
                    if(uploadFile(clientsFD[i], fname, path)) {
                    sprintf(status, "OK");
                    if (helpPrints) printf("User: %s, fop: %s, uploaded file %s to directory %s\n", UID, fop, fname, path);
                    } else sprintf(status, "ERR");
                } else sprintf(status, "ERR");
                sprintf(response, "RUP");

            } else if (strcmp(command, "DEL") == 0) {
                printf("entrou no delete com fop:%sx\n", fop); //!!!
                sprintf(path,"%s/%s", users_dir, UID);
                sprintf(filepath, "%s/%s", path, fname);
                if (strcmp(fop, "E") == 0)
                    sprintf(status, "INV");
                else if (strcmp(fop, "D") == 0 && rmFile(filepath)) {
                    sprintf(status, "OK");
                    printf("deu delete bem\n"); //!!!
                    if (helpPrints) printf("User: %s, fop: %s, deleted file %s from directory %s\n", UID, fop, fname, path);
                }
                else if (!stringNumbers(UID, 5))
                    sprintf(status, "NOK");
                else
                    sprintf(status, "ERR");
                sprintf(response, "RDL");
            
            } else if (strcmp(command, "REM") == 0) {
                sprintf(path,"%s/%s", users_dir, UID);
                if (strcmp(fop, "E") == 0) {
                    sprintf(status, "INV");
                    if (helpPrints) printf("User: %s, fop: %s, couldn't remove files\n", UID, fop);
                } else if (!dir_exists(path)) {
                    sprintf(status, "OK");
                    if (helpPrints) printf("User: %s, fop: %s, files were already removed for this user\n", UID, fop);
                } else if(strcmp(fop, "X") == 0) {
                    if (!rmFiles(path)) exit_error(1);
                    if (!rmDirectory(path)) exit_error(1);
                    sprintf(status, "OK");
                    if (helpPrints) printf("User: %s, fop: %s, files removed successfully\n", UID, fop);
                } else if (!stringNumbers(UID, 5))
                    sprintf(status, "NOK");
                else {
                    sprintf(status, "ERR");
                    if (helpPrints) printf("User: %s, fop: %s, couldn't remove files\n", UID, fop);
                }
                sprintf(response, "RRM");
            }

            // Copy responses to buffer
            if (strcmp(status, "ERR") == 0) {

                sprintf(buffer, "%s\n", status);
                printf("vai enviar para o user %sx", buffer); //!!!
                nleft = strlen(buffer);
                ptr = buffer;
                while (nleft > 0) {
                    nwritten = write(clientsFD[i], ptr, nleft);
                    if (nwritten == -1) /*error*/ exit_error(1);
                    nleft -= nwritten;
                    ptr += nwritten;
                }

            } else if (strcmp(command, "RTV") != 0) {

                if (strcmp(command, "LST") == 0)
                    sprintf(buffer, "%s\n", response);

                else
                    sprintf(buffer, "%s %s\n", response, status);
                
                printf("vai enviar para o user %sx", buffer); //!!!
                nleft = strlen(buffer);
                ptr = buffer;
                while (nleft > 0) {
                    nwritten = write(clientsFD[i], ptr, nleft);
                    if (nwritten == -1) /*error*/ exit_error(1);
                    nleft -= nwritten;
                    ptr += nwritten;
                }

                
            }
        }
    }               
    clientsFD[i] = 0;
    free(command);
    free(UID);
    free(TID);
    free(fname);
    free(fop);
    free(CNF);
    free(response);
    free(fileList);
    free(path);
    free(filepath);
    free(status); 
}

int main(int argc, char* argv[]) {
    char users_dir[16];

    parseArgs(argc, argv);

    for(int j=0; j < MaxClients; j++){
        clientsFD[j]=0;
    }

    sprintf(users_dir,"USERS_FS");
    if (stat(users_dir, &st) == -1) {
        if(!newDirectory(users_dir)) exit_error(1);
    }

    signal(SIGINT, handle_CtrlC);

    connect_serverTCP();
    connect_clientUDP();

    while(1) {
        FD_ZERO(&inputs);
        FD_SET(fd_TCP,&inputs); // server TCP
        maxfd = fd_TCP;
        
        for (int i = 0; i < MaxClients; i++) {
            if (clientsFD[i] != 0) {
                FD_SET(clientsFD[i], &inputs);
                maxfd = max(maxfd, clientsFD[i]);
            }
        }
        
        out_fds = select(maxfd+1, &inputs, (fd_set*)NULL, (fd_set*)NULL, NULL);
        if (out_fds <= 0) /*error*/ exit_error(1);

        signal_count = 0;
        done = false;
        signal(SIGALRM, handle_alarm);
        alarm(3);

        if (FD_ISSET(fd_TCP, &inputs)) {
            // SERVER TCP
            accept_connection_TCP();
        }
        
        for (int i = 0; i < MaxClients; i++) {
            if (clientsFD[i] != 0) {
                if (FD_ISSET(clientsFD[i], &inputs)) {
                    handleClient(i, users_dir);
                }
            }
        }
        done = true;
        alarm(0);
    }

    freeaddrinfo(res_TCP);
    freeaddrinfo(res_UDP);
    close (fd_TCP);
    close(fd_UDP);

    return 0;
}