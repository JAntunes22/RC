#include "utils.h"

// *******************
//  CHECK STRINGS
// *******************

int stringNumbers(char * str, int size){ 
    
    if( strlen(str) != size){
        return false;
    }

    for (int i=0; i<size; i++){ 
        if (isdigit(str[i]) == 0) {
            return false ;
        }
    } 
    
    return true;
} 

int stringNumbers_Letters(char * str, int size){ 
    
    if( strlen(str) != size){
        return false;
    }

    for (int i=0; i<size; i++){ 
        if (isalpha(str[i]) == 0 && isdigit(str[i]) == 0) {
            return false ;
        }
    } 
    
    return true;
} 

int stringFOP(char * str){
    if( strlen(str) != 1){
        return false;
    }

    if ( str[0] == 'L' || str[0] == 'R' || str[0] == 'U' || str[0] == 'D' || str[0] == 'X' ){
        return true;
    }
    return false;
}

int correctFilename(char * str) { 

    if( strlen(str) > 24) return false;

    for (int i=0; i<strlen(str); i++){ 
        if (isalpha(str[i]) == 0 && isdigit(str[i]) == 0 && str[i] != '-'  && str[i] != '_'  && str[i] != '.') {
            return false ;
        }
    } 
    
    return true;
}

unsigned word_count(char *str) {
    int OUT = 0;
    int IN = 1;
    int state = OUT;
    unsigned wc = 0;  // word count
 
    while (*str) {
        // If next character is a separator, set the state as OUT
        if (*str == ' ' || *str == '\n' || *str == '\t')
            state = OUT;
 
        // If next character is not a word separator and 
        // state is OUT, then set the state as IN and 
        // increment word count
        else if (state == OUT)
        {
            state = IN;
            ++wc;
        }
        ++str;
    }
    return wc;
}

int correctMessage(char * FOP, char * buffer){
    if ( FOP[0]== 'R' || FOP[0]== 'U' || FOP[0]== 'D'){
        if ( word_count(buffer) != 5 ){
            return -1;
        }
        return 5;

    } else if ( FOP[0]== 'X' || FOP[0]== 'L'){
        if ( word_count(buffer) != 4 ){
            return -1;
        }

        return 4;

    } else {
        return -1;
    }
}

// *******************
// FILE FUNCTIONS
// *******************

int newDirectory(char * dirname) {
    struct stat st = {0};
    if (stat(dirname, &st) == -1) {
        if (mkdir(dirname,0700) != 0) {
            return false;
        }
    }
    return true;
}

int rmDirectory(char * name){
    if(rmdir(name)!=0){
        return false;
    }
    return true;
}

int dir_exists(char * path) {
    DIR* dir = opendir(path);
    if (dir) {
        closedir(dir);
        return true;
    }
    else return false;
}

int file_exists(char * path){
    if( access( path, F_OK ) == -1 ){  
        // File doesn't exists
        return false;
    }
    return true;
}

int rmFile(char* path){
    if (remove(path) != 0) {
      return false;
    } 
    return true;
}

int rmFiles(char* path){
    DIR *theFolder = opendir(path);
    struct dirent *next_file;
    char filepath[300];

    while ( (next_file = readdir(theFolder)) != NULL ) {
        // build the path for each file in the folder
        if (next_file->d_name[0] == '.') continue;
        
        sprintf(filepath, "%s/%s", path, next_file->d_name);
      
        if (!rmFile(filepath)) return false;    
    }
    closedir(theFolder);
    return true;
}

int fileCount(char *path) {
    int file_count = 0;
    DIR * dirp;
    struct dirent * entry;

    dirp = opendir(path);
    if (dirp == NULL) return -1;
    while ((entry = readdir(dirp)) != NULL) {
        if (entry->d_type == DT_REG) {
            file_count++;
        }
    }
    closedir(dirp);
    return file_count;
}
























