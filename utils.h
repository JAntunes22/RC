#ifndef UTILS
#define UTILS

#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <ctype.h>
#include <dirent.h>
#define true 1
#define false 0
#define MaxClients 10

// *******************
//  CHECK STRINGS
// *******************

int stringNumbers(char * str, int size); // Check String made of numbers
int stringNumbers_Letters(char * str, int size); // Check String made of numbers and letters
int stringFOP(char * str); // Check Fop is correct
int correctFilename(char * str); // Check Fname is correct
unsigned word_count(char *str); // Count number of words 
int correctMessage(char * FOP, char * buffer); // Correct message format

// *******************
// FILE FUNCTIONS
// *******************

int newDirectory(char * dirname); // Create a new Directory
int rmDirectory(char * name);   // Remove directory
int dir_exists(char * path);    // Check if directory exists
int file_exists(char * path);   // Check if file exists
int rmFile(char* path); // Remove File
int rmFiles(char* path); // Remove Files in Directory
int fileCount(char *path); // Count number of Files

#endif