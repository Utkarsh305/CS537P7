#include <stdio.h>
#include <stdlib.h>
#include <string.h>


char *file_name = "mnt/test.txt";
int main() {

    FILE *fp = fopen(file_name, "w");
    if (fp == NULL) {
        printf("Error opening file!\n");
        exit(1);
    }

    int bytesToWrite = 512*7 + 1;
    char *buffer = (char *)malloc(bytesToWrite);
    for(int i = 0; i < bytesToWrite; i++) {
        buffer[i] = 'a' + i % 26;
    }

    fwrite(buffer, 1, bytesToWrite, fp);
    fclose(fp);
    free(buffer);
}
