#include <unistd.h>
#include <stdio.h>
#include <signal.h>

char *hello = 0x10000;

void sig_handler(int signum){

  //Return type of the handler function should be void
  printf("\nInside handler function\n");
  printf("\nthe value of hello is %d\n", *hello);
  signal(SIGINT,SIG_DFL);
}

int main(int argc, char **argv)
{
	int i = 0;// aux;
//        signal(SIGINT,sig_handler);
	hello += 0xf;
	do {
                //printf("Hello world!!\n");
                i++;
                //fprintf(stdout,"Hello world!!\n");
		fprintf(stdout,"%c\n", *hello);
		hello += 4096;
	} while (i < 26);

	return 0;
}
