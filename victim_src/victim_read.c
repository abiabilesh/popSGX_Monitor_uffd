#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>

typedef struct _encryption_header
{
    size_t file_data_size;
    unsigned char digest[20];
} encryption_header_t;

encryption_header_t *hello = 0x10000;



void sig_handler(int signum){

  //Return type of the handler function should be void
  printf("\nInside handler function\n");
  //printf("\nthe value of hello is %d\n", *hello);
  signal(SIGINT,SIG_DFL);
}

int main(int argc, char **argv)
{
	int i = 0;// aux;
//        signal(SIGINT,sig_handler);
	// hello += 0xf;
	// do {
    //             printf("Victim about to read a new page @ %p!!!!!!!!!!!!!!\n", hello);
    //             i++;
    //             //fprintf(stdout,"Hello world!!\n");
	// 	fprintf(stdout,"%c\n", *hello);
	// 	sleep(5);
	// 	sleep(5);
	// 	hello += 4096;
    //             if(i%26 == 0)
    //                 hello = 0x1000f;
	// } while (i < 10000);

    //fprintf(stdout, "file_data_size: %d\n", *(size_t*)hello);
	//fprintf(stdout, "digest: %s\n", *(unsigned char*)(hello + sizeof(size_t)));

	// while(1){
	 	sleep(5);
		sleep(5);
		sleep(5);
		fprintf(stdout,"Reading the shared memory region!!\n");
		encryption_header_t tHello = *hello;
		fprintf(stdout, "digest: %s\n", tHello.digest);
		fprintf(stdout, "file_data_size: %d\n", tHello.file_data_size);

	// }

	return 0;
}
