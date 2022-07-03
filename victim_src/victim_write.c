#include <unistd.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>


typedef struct _encryption_header
{
    size_t file_data_size;
    unsigned char digest[20];
} encryption_header_t;

volatile encryption_header_t *hello = 0x10000;

void sig_handler(int signum){

  //Return type of the handler function should be void
  printf("\nInside handler function\n");
  //printf("\nthe value of hello is %d\n", *hello);
  signal(SIGINT,SIG_DFL);
}

int main(int argc, char **argv)
{
	int i = 0;// aux;
	while(1);
//        signal(SIGINT,sig_handler);
	//hello += 0xf;
	// do {
  //       printf("Victim about to write a new page @ %p!!!!!!!!!!!!!!\n", hello);
  //       i++;
  //       //fprintf(stdout,"Hello world!!\n");
	
	// int k = 0;

	// sleep(1);
  // #if 1
	// if(i%2 == 0)
	//     *(hello + 0xf) = 'm';
	// else
  //           *(hello + 0xf) = 'b';
  // #endif 
  
  //       fprintf(stdout,"address %p value %c\n", hello, *(hello+0xf));
  //       hello += 4096;
       
  //       if(i % 26 == 0){
  //           hello = 0x10000;
	//       }
	// } while (i < 52);

  encryption_header_t tHello;

  
  strncpy(tHello.digest, "Hello through the uffd", 25);
  tHello.file_data_size = 20;

  //sleep(3);
  //sleep(3);
  // hello->file_data_size = 20;
  // memcpy(hello->digest, "Helloworld", 20);
  fprintf(stdout, "Writing into the shared memoy region\n");
  *hello = tHello;

  //memcpy((unsigned char*)(hello + sizeof(size_t)), "helloworld", (20));

  while(1);
	return 0;
}
