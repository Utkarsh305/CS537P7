#include <stdio.h>
#include <stdlib.h>
#include <string.h>


char *file_name = "mnt/test.txt";

void write(int number, FILE *fp, int offset) {
    char *buffer = (char *)malloc(number);
    for(int i = 0; i < number; i++) {
        buffer[i] = 'a' + i % 26;
    }
    fseek(fp, offset, SEEK_SET);
    fwrite(buffer, number, 1, fp);
    // fflush(fp);
}

void read(FILE *fp) {
    char *buffer = (char *)malloc(1024);
    fseek(fp, 0, SEEK_SET);

    memset(buffer, 0, 1024);
    while(fread(buffer, 1024, 1, fp)!=0) {
        printf("%s", buffer);
        memset(buffer, 0, 1024);
    }
    printf("\n");
}

int main() {
    int bytesToWrite = 512*3;

    FILE *fp = fopen(file_name, "w");
    if (fp == NULL) {
        printf("Error opening file!\n");
        exit(1);
    }

    write(bytesToWrite, fp, 0);
    sleep(1);
    write(512, fp, bytesToWrite - 512/2);

    sleep(1);
    read(fp);

    printf("\n%u", (unsigned int)-1);

}
