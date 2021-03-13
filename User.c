#include <unistd.h> 
#include <stdlib.h> 
#include <sys/types.h> 
#include <sys/socket.h> 
#include <netinet/in.h> 
#include <arpa/inet.h> 
#include <netdb.h> 
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include "utils.h"
#define true 1
#define false 0

int existsUID = 0;
int existsTID = 0; 
int communicateWithAS = -1; // 1 communicate with AS || 0 comunicate with FS || -1 dont communicate
int rrtOkVar = 0;
int uploadFileVar = 0;
int done = 0;
int signal_count = 0;
char buffer[600];

char* ASIP = NULL;
char* ASport = NULL;
char* FSIP = NULL;
char* FSport = NULL;

// Sockets
int errcode;
ssize_t n;
socklen_t addrlen;
struct addrinfo hints_AS, hints_FS, *res_FS, *res_AS;
struct sockaddr_in addr;
ssize_t nwritten, nread, nbytes, nleft;

// "Select" variables
int fd_AS, fd_FS;
fd_set inputs;

// *******************
// READ COMMAND 
// *******************

void parseArgs (int argc, char* argv[]){
    int i;

    if (argc > 9 || argc % 2 == 0 || argc < 1) { 
        fprintf(stderr, "Invalid format, format-> ./user [-n ASIP] [-p ASport] [-m FSIP] [-q FSport]\n");
        exit(1);
    }

    for(i = 1; i < argc ; i++) {
        if (strcmp("-n", argv[i]) == 0) {
            ASIP = argv[i+1];
        }

        if (strcmp("-p", argv[i]) == 0) {
            ASport = argv[i+1];
            if (strlen(ASport) != 5) {
                printf("Invalid argument for ASport\n");
                exit(1);
            } 
        }

        if (strcmp("-m", argv[i]) == 0) {
            FSIP = argv[i+1];
        }

        if (strcmp("-q", argv[i]) == 0) {
            FSport = argv[i+1];
            if (strlen(FSport) != 5) {
                printf("Invalid argument for FSport\n");
                exit(1);
            } 
        }
    }

    if (ASIP == NULL) {
        // -n ASIP = "127.0.0.1" ou  "tejo.tecnico.ulisboa.pt"
        ASIP = "127.0.0.1";
    }

    if (ASport == NULL) {
        ASport = "58027"; // 58027 || tecnico.ulisboa -> 58011
    }

    if (FSIP == NULL) {
        // -m FSIP = "127.0.0.1" ou "tejo.tecnico.ulisboa.pt"
        FSIP = "127.0.0.1";   
    }

    if (FSport == NULL) {
        FSport = "59027"; // 59027 || tecnico.ulisboa -> 59000
    }
}

// *******************
// EXIT PROGRAM
// *******************

// Exit program correctly
void exit_error(int n){
    perror("Error, forced to exit");
    freeaddrinfo(res_AS);
    //freeaddrinfo(res_FS);
    close(fd_AS);
    //close (fd_FS);
    exit(n);
}

// Timeout program
void handle_alarm(){
    printf("Apanhou SIGNAL\n");
    if (!done){
        printf("NOT DONE\n");
        signal( SIGALRM, handle_alarm );
        alarm(3);
        if (signal_count == 2){
            exit_error(1);
        }else{
            signal_count++;
        }
    } else{
        printf("DONE\n");
    }
}

// Exit by ctrl+c
void handle_CtrlC(){
    printf("Program exit by Ctrl+C\n");
    freeaddrinfo(res_AS);
    freeaddrinfo(res_FS);
    close (fd_FS);
    close (fd_AS);
    exit(0); 

}

// *******************
// SOCKETS CONNECTIONS
// *******************

void connectFS () {
    // File Server
    fd_FS = socket(AF_INET,SOCK_STREAM,0); // tcp SOCKET
    if (fd_FS==-1) /*error*/ exit(1);

    memset(&hints_FS, 0, sizeof hints_FS);
    hints_FS.ai_family = AF_INET;                 
    hints_FS.ai_socktype = SOCK_STREAM; // TCP socket

    errcode = getaddrinfo (FSIP, FSport, &hints_FS, &res_FS);
    if (errcode!=0) /*error*/ exit(1);

    n= connect (fd_FS, res_FS->ai_addr, res_FS->ai_addrlen);
    if (n==-1) /*error*/ exit(1);
}

void connectAS() {
    // Authentication Server 
    fd_AS = socket(AF_INET,SOCK_STREAM,0); // tcp SOCKET
    if (fd_AS==-1) /*error*/ exit(1);

    memset(&hints_AS, 0, sizeof hints_AS);
    hints_AS.ai_family = AF_INET;                 
    hints_AS.ai_socktype = SOCK_STREAM; // TCP socket

    errcode = getaddrinfo (ASIP, ASport, &hints_AS, &res_AS);
    if (errcode!=0) /*error*/ exit(1);
    
    n= connect (fd_AS, res_AS->ai_addr, res_AS->ai_addrlen);
    if (n==-1) /*error*/ exit(1);
}


// Sends file contents to FS
int uploadFile(char* Fname, char* UID, char* TID) {
    char* msg = (char*) malloc(70*sizeof(char));
    char* data = (char*) malloc(512*sizeof(char));
    char* Fsize = (char*) malloc(15*sizeof(char));
    char* ptr;
    int i;

    connectFS();
    
    if( !file_exists(Fname) ) return -1;
    FILE * fp = fopen(Fname, "r");
    if (fp==NULL) exit_error(1); 

    // Get Fsize
    fseek(fp, 0L, SEEK_END); 
    sprintf(Fsize, "%ld", ftell(fp)); 
    
    // Sends first part of the message to FS
    // UPL UID TID Fname Fsize
    sprintf(msg, "UPL %s %s %s %s ", UID, TID, Fname, Fsize);

    nleft=strlen(msg);
    ptr = msg;
    while(nleft>0) {
        nwritten = write(fd_FS,ptr,nleft);
        if(nwritten==-1)/*error*/exit_error(1);
        nleft-=nwritten;
        ptr+=nwritten;
    }

    printf("Envia para o AS/FS -> %s\n", msg);

    // Receive data from file and send it to FS 
    fseek(fp, 0L, SEEK_SET);
    nleft = atoi(Fsize);

    while(nleft>0) {
        // Receive from file
        if (nleft>512){
            nread = fread(data, 1, 512, fp);
            if (nread <=0 && ferror(fp)) exit_error(1);
        }
        else if (nleft<= 512){
            nread = fread(data, 1, nleft, fp);
            if (nread <=0 && ferror(fp)) exit_error(1);
        }
        nleft -= nread;
        i = nread;
        while(i > 0) {
            // Send to FS
            nwritten = write(fd_FS, data, nread);
            if (nwritten == -1) exit_error(1);
            i -= nwritten;
            data+=nwritten;
        }
        data -= nread;
    }

    write(fd_FS, "\n", 1);   
    if (nwritten == -1) exit_error(1);
    fclose(fp);
    
    free(data);
    free(msg);
    return 0;
}

// Retrieves file from FS 
void retrieveFile(int fd_FS, char * Fname) {    
    char* Fsize = (char*) malloc(15*sizeof(char));
    char* msg = (char*) malloc(512*sizeof(char));
    
    // Read Fsize
    int i=0;
    while(1) {
        nread = read(fd_FS, Fsize, 1);
        if (nread <= 0) exit_error(1);
        if (Fsize[nread-1] == ' ') break;
        Fsize += nread;
        i += 1;
    }
    Fsize -= i;
    Fsize[i] = '\0';
    
    // Read Data
    nleft = atoi(Fsize);
    FILE * fp = fopen(Fname, "w");
    if (fp==NULL) exit_error(1);
    while(nleft > 0) {
        nread = read(fd_FS, msg, nleft);
        if (nread <= 0) exit_error(1);
        nleft -= nread; 
        nwritten = fwrite(msg, 1, nread, fp);
        if (nwritten <= 0) exit_error(1);
    }

    fclose(fp);

    printf("%s file was retrieved to the current directory.\n", Fname);
    free(Fsize);
    free(msg);
}

//************************
// RECV INPUT FROM AS/FS
//************************

void commandReceived(char * buffer, char * TID){
        char *arg1 = (char*)malloc(sizeof(char)*5);
        char *type = (char*)malloc(sizeof(char)*5);
        char *Fsize = (char*)malloc(sizeof(char)*15);
        char *Fname = (char*)malloc(sizeof(char)*25);
        char * ptr;

        sscanf(buffer, "%s %s", type, arg1);
        printf("Recebe do AS/FS -> %s\n", buffer);
        printf("Type -> %sx\nArg1 -> %sx\n", type, arg1);

        if (strcmp(type, "RLO") == 0 && word_count(buffer) == 2) { // Confirmação do command login
            // RLO status

            if (strcmp(arg1, "OK") == 0) {
                printf("You are now logged in.\n");

            } else if (strcmp(arg1, "NOK") == 0) {
                printf("Invalid password.\n");

            } else if (strcmp(arg1, "ERR") == 0) {
                printf("Invalid UID.\n");

            } else {
                printf("RLO uncommon error.\n");
            }
        
        
        } else if (strcmp(type, "RRQ") == 0 && word_count(buffer) == 2) { // Confirmação do command request
            // RRQ status

            if (strcmp(arg1, "OK") == 0) {
                printf("Request successful.\n");

            } else if (strcmp(arg1, "ELOG") == 0) {
                printf("Login was not previously done.\n");

            } else if (strcmp(arg1, "EPD") == 0) {
                printf("Message could not be sent by the AS to the PD.\n");

            } else if (strcmp(arg1, "EUSER") == 0) {
                printf("Incorrect UID.\n");

            } else if (strcmp(arg1, "EFOP") == 0) {
                printf("Invalid Fop.\n");

            } else {
                printf("Incorrectly formatted REQ message.\n");
            }

            
            

        } else if (strcmp(type,"RAU") == 0 && word_count(buffer) == 2) { // Confirmação do command val
            // RAU TID

            if (strcmp(arg1,"0") != 0 && stringNumbers(arg1, 4)) {
                printf("Authenticated! (%s)\n", arg1); //arg1 = TID
                strcpy(TID, arg1);
                existsTID = true;
            } else {
                printf("Failed to authenticate.\n");
            }
            

        
        } else if (strcmp(type, "RLS") == 0) { // Confirmação do command list
            // RLS N [Fname Fsize]*

            if (strcmp(arg1, "EOF") == 0) {
                printf("No files are available.\n");

            } else if (strcmp(arg1, "NOK") == 0) {
                printf("The UID does not exist.\n");

            } else if (strcmp(arg1, "INV") == 0) {
                printf("AS validation error of the provided TID.\n");

            } else if (strcmp(arg1, "ERR") == 0) {
                printf("LST request is not correctly formulated.\n");

            } else { // Displays list of files
                ptr = buffer + strlen(type) + strlen(arg1) + 2;

                for (int i=0; i < atoi(arg1) ; i++) {
                    sscanf(ptr, "%s %s", Fname, Fsize);
                    if (correctFilename(Fname)) {
                        printf("%d %s %s\n", i+1, Fname, Fsize);
                        ptr += strlen(Fname) + strlen(Fsize) + 2;
                    } else {
                        printf("Incorrect file name.\n");
                        break;
                    }
                }
            }
            existsTID = false;
            freeaddrinfo(res_FS);
            close(fd_FS);
        

        } else if (strcmp(type, "RRT") == 0 && word_count(buffer) == 2) { // confirmação do command retrieve
            // RRT status [Fsize data]
            
            if (strcmp(arg1, "EOF") == 0) {
                printf("File is not available.\n");

            } else if (strcmp(arg1, "NOK") == 0) {
                printf("The UID does not exist.\n");

            } else if (strcmp(arg1, "INV") == 0) {
                printf("AS validation error of the provided TID.\n");

            } else if (strcmp(arg1, "ERR") == 0) {
                printf("RTV request is not correctly formulated.\n");

            } else if (strcmp(arg1, "OK") == 0) {
                existsTID = false;
            }
            
            freeaddrinfo(res_FS);
            close(fd_FS);
        

        } else if (strcmp(type, "RUP") == 0 && word_count(buffer) == 2 ) { // confirmação do command upload
            //RUP status

            if (strcmp(arg1, "EOF") == 0) {
                printf("File is not available.\n");

            } else if (strcmp(arg1, "NOK") == 0) {
                printf("The UID does not exist.\n");

            } else if (strcmp(arg1, "DUP") == 0) {
                printf("File already exists.\n");

            } else if (strcmp(arg1, "FULL") == 0) {
                printf("Failed to upload, you have already reached your files limit (15).\n");

            } else if (strcmp(arg1, "ERR") == 0) {
                printf("UPL is not correctly formulated.\n");

            } else if (strcmp(arg1, "OK") == 0) {
                printf("UPL was successful.\n");

            } else if (strcmp(arg1, "INV") == 0){
                printf("AS validation error of the provided TID.\n");
                
            }
            existsTID = false;
            freeaddrinfo(res_FS);
            close(fd_FS);


        } else if (strcmp(type, "RDL") == 0 && word_count(buffer) == 2 ) { // confirmação do command delete
            // RDL status
            
            if (strcmp(arg1, "EOF") == 0) {
                printf("File is not available.\n");

            } else if (strcmp(arg1, "NOK") == 0) {
                printf("The UID does not exist.\n");

            } else if (strcmp(arg1, "INV") == 0) {
                printf("AS validation error of the provided TID.\n");

            } else if (strcmp(arg1, "ERR") == 0) {
                printf("DEL request is not correctly formulated.\n");

            } else if (strcmp(arg1, "OK") == 0) {
                printf("Delete was successful.\n");
            }
            existsTID = false;
            freeaddrinfo(res_FS);
            close(fd_FS);
        

        } else if (strcmp(type, "RRM") == 0 && word_count(buffer) == 2 ) { // confirmação do command remove
            // RRM status
            if (strcmp(arg1, "NOK") == 0) {
                printf("The UID does not exist.\n");

            } else if (strcmp(arg1, "INV") == 0) {
                printf("AS validation error of the provided TID.\n");

            } else if (strcmp(arg1, "ERR") == 0) {
                printf("REM request is not correctly formulated.\n");

            } else if (strcmp(arg1, "OK") == 0) {
                printf("REM request was successful.\n");
            }
            existsTID = false;
            freeaddrinfo(res_FS);
            close(fd_FS);
        
        
        } else {
            // ERR message
            printf("Unexpected error occurred.\nBuffer -> %s\n", buffer);
            exit_error(1);
        }
        free(Fname);
        free(Fsize);
        free(arg1);
        free(type);
        communicateWithAS = -1;

}

//************************
// SEND INPUT TO AS/FS
//************************

int inputCommands( char * RID, char * TID , char * UID, char * Fname){
    char *arg1 = (char*)malloc(sizeof(char)*25);
    char *arg2 = (char*)malloc(sizeof(char)*25);
    char *command = (char*)malloc(sizeof(char)*10);
    int error = 0;
            

    fgets(buffer, sizeof(buffer),stdin);
    sscanf(buffer,"%s %s %s",command,arg1,arg2);

    signal_count=0;
    done= false;
    signal( SIGALRM, handle_alarm );
    alarm(3);
    

    if (strcmp(command,"login")==0 ) {
        // login UID password

        if( !existsUID ){
            if (stringNumbers(arg1,5) && stringNumbers_Letters(arg2,8) && word_count(buffer) == 3) {
                // LOG UID password
                sprintf(buffer,"LOG %s %s\n",arg1,arg2);
                strcpy(UID,arg1);
                existsUID = true;
                communicateWithAS = 1;
            } else {
                printf("Invalid user's ID or password\n");
                error = true;
            }
        } else{
            printf("User already logged in.\n");
            error = true;
        }
    
    } else if (strcmp(command,"req") == 0 && word_count(buffer) >= 2 && word_count(buffer) <= 3) {
        // req FOP [Fname]

        int requestID = 599 + (rand()%9000);
        sprintf(RID,"%d",requestID);
        
        if (stringNumbers(UID,5)) {
            
            if ((strcmp(arg1, "R") == 0 || strcmp(arg1, "U") == 0 || strcmp(arg1, "D") == 0) && word_count(buffer) == 3) {
                strcpy(Fname,arg2);  

                sprintf(buffer,"REQ %s %s %s %s\n", UID, RID, arg1, arg2); // REQ UID RID FOP [Fname]
                communicateWithAS = 1;
            
            } else if (strcmp(arg1, "L") == 0 && word_count(buffer) == 2) {
                sprintf(buffer,"REQ %s %s %s\n", UID, RID, arg1); // REQ UID RID FOP 
                communicateWithAS = 1;
            
            } else if (strcmp(arg1, "X") == 0 && word_count(buffer) == 2) {
                sprintf(buffer,"REQ %s %s %s\n", UID, RID, arg1); // REQ UID RID FOP 
                communicateWithAS = 1;
            
            } else {
                printf("Invalid type of operation desired\n");
                error = true;
            }
        } else {
            printf("Invalid user's ID\n");
            error = true;
        }

    } else if (strcmp(command,"val") == 0 && word_count(buffer) == 2 && stringNumbers(arg1,4)) {
        // val VC

        if (stringNumbers(UID,5)) {
            sprintf(buffer,"AUT %s %s %s\n",UID,RID,arg1); // AUT UID RID VC
            communicateWithAS = 1;
        } else {
            printf("Invalid user's ID or request's ID\n");
            error = true;
        }
    
    } else if ((strcmp(command, "list") == 0 || strcmp(command, "l") == 0) && word_count(buffer) == 1) {
        // l  ou  list

        if (stringNumbers(UID,5) && existsTID) {
            sprintf(buffer, "LST %s %s\n", UID, TID); // LST UID TID 
            communicateWithAS = 0;
        } else {
            printf("Invalid user's ID or TID\n");
            error = true;
        }
    
    } else if ((strcmp(command, "retrieve") == 0 || strcmp(command, "r") == 0) && word_count(buffer) == 2) {
        // retrieve Fname    ou     r Fname
        
        if (stringNumbers(UID,5) && existsTID) {
            
            if (strcmp(arg1,Fname)!=0){ // req Fname != u Fname
                printf(" Fname different from Fname requested.\n");
                error = true; 
            } else if (!correctFilename(arg1)) {
                printf("Incorrect file name.\n");
                error = true;
            } else{

                strcpy(Fname, arg1);

                sprintf(buffer, "RTV %s %s %s\n", UID, TID, Fname); //arg1 = Fname
                communicateWithAS = 0;
            }           
        } else {
            printf("Invalid user's ID or TID\n");
            error = true;
        }
    
    } else if ((strcmp(command, "upload") == 0 || strcmp(command, "u") == 0) && word_count(buffer) == 2) {
        // u Fname     ou    upload Fname
        
        if (stringNumbers(UID,5) && existsTID) {

            if (strcmp(arg1,Fname)!=0){ // req Fname != u Fname 
                printf(" Fname different from Fname requested.\n");
                error = true;
            } else if (!correctFilename(arg1)) {
                printf("Incorrect file name.\n");
                error = true;
            } else{

                strcpy(Fname, arg1);
                int n = uploadFile(Fname, UID, TID);
                if (n == -1) {  
                    printf("File name does not exists.\n");
                    error = true;
                } else{
                    uploadFileVar = true;
                    communicateWithAS = 0;
                }
            }
            
        } else {
            printf("Doesn't have TID or incorrect UID.\n");
            error = true;
        }
    
    } else if ((strcmp(command, "delete") == 0 || strcmp(command, "d") == 0) && word_count(buffer) == 2) {
        // delete Fname    ou    d Fname
        
        if (stringNumbers(UID,5) && existsTID) {
            if (strcmp(arg1,Fname)!=0){ // req Fname != u Fname
                printf("Fname different from Fname requested.\n");
                error = true;
            }else if (!correctFilename(Fname)) {
                printf("Incorrect file name.\n");
                error = true;
            } else{

                sprintf(buffer, "DEL %s %s %s\n", UID, TID, arg1); // DEL UID TID Fname
                communicateWithAS = 0;
            }

        
        } else {
            printf("Doesn't have TID or incorrect UID.\n");
            error = true;
        }
    
    } else if ((strcmp(command, "remove") == 0 || strcmp(command, "x") == 0) && word_count(buffer) == 1) {
        // remove    ou    x
        
        if (stringNumbers(UID,5) && existsTID) {    
            existsUID = false;
            sprintf(buffer, "REM %s %s\n", UID, TID);
            communicateWithAS = 0;
        } else {
            printf("Doesn't have TID or incorrect file name.\n");
            error = true;
        }
    } else if (strcmp(command,"exit") == 0 && word_count(buffer) == 1){
        printf("Closed successfully.\n");
        freeaddrinfo(res_AS);
        close(fd_AS);
        exit(0);  
    } else {
        printf("Invalid command\n");
        error = true;
    }

    free(arg1);
    free(arg2);
    free(command);

    return error;
}

int main(int argc, char* argv[]) {
    char UID[6], RID[5] , TID[5];
    char Fname[30];
    char *ptr;
    
    parseArgs(argc, argv);    
    
    signal(SIGINT, handle_CtrlC);

    connectAS();

    while(1) {
        
        int error = inputCommands( RID, TID , UID, Fname);
        if (error) {
            done = true;
            alarm(0);
            continue;
        }
        
        // Write to AS / FS
        if (uploadFileVar == 0) {
            
            if (!communicateWithAS) connectFS();

            ptr = buffer;
            nleft=strlen(buffer);
            printf("Envia para o AS/FS -> %s\n", buffer);
            while(nleft>0) {
                nwritten = (communicateWithAS == 1) ? write(fd_AS,ptr,nleft) : write(fd_FS,ptr,nleft);
                if(nwritten<=0) /* error */ exit_error(1);                
        
                nleft-=nwritten;
                ptr+=nwritten;
            }
        }

        uploadFileVar = 0;

        nleft=300; 
        ptr=buffer;

        // Testar se recebe "RRT OK"    
        if (communicateWithAS == 0) {
            nread = read(fd_FS, ptr, 7);
            if(nread<=0)/*error*/ exit_error(1);                     

            ptr[nread] = '\0';
            if (strcmp(ptr, "RRT OK ") == 0) {
                retrieveFile(fd_FS, Fname); 
                rrtOkVar = 1;
            } else {
                ptr += nread;
            }       
        }

        if(rrtOkVar==0){
            while(1){
                if (communicateWithAS == 0){
                    if (buffer[nread-1]!= '\n'){
                        nread = read(fd_FS,ptr,nleft);
                    } else{
                        break;
                    }
                } else if (communicateWithAS == 1){
                    nread = read(fd_AS,ptr,nleft);
                }
                
                if(nread<=0)/*error*/ exit_error(1); 
                if(ptr[nread-1]=='\n') break; 
                ptr+=nread;
            }
            ptr[nread]='\0';  
        }          
        rrtOkVar = 0;

        commandReceived(buffer, TID);

        done = true;
        alarm(0);
    }

    return 0;
}