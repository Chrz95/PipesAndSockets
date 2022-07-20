#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <error.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/wait.h>

#define COMMANDLENGTH 150
#define NUMOFMESSAGES 10
#define PACKAGELENGTH 513

/* int CountFileLines(char * filename)
{
    FILE *fp;
    int count = 0;  // Line counter (result)
    char c;  // To store a character read from file

    // Open the file
    fp = fopen(filename, "r");

    // Check if file exists
    if (fp == NULL)
    {
        printf("Could not open file %s", filename);
        return 0;
    }

    // Extract characters from file and store in character c
    for (c = getc(fp); c != EOF; c = getc(fp))
        if (c == '\n') // Increment count if this character is newline
            count = count + 1;

    // Close the file
    fclose(fp);

    return count;
}*/

int CountFileLines(char * filename)
{
  FILE *fp;
  int count = 0;  // Line counter (result)
  char buf[1024];  // To store a character read from file

  // Open the file
  fp = fopen(filename, "r");

  // Check if file exists
  if (fp == NULL)
  {
      printf("Could not open file %s", filename);
      return 0;
  }

  while (fgets(buf, sizeof(buf), fp) != NULL)
  {
    buf[strlen(buf) - 1] = '\0';
    count++ ;

    if (strcmp(buf,"end") == 0)
    {
      count-- ;
    }

    if (strcmp(buf,"timeToStop") == 0)
    {
      //printf("Found time to stop\n");
      count-- ;
      return count;
    }
  }
  return count;
}

int main(int argc, char* argv[])
{
  int sock,serverPORT,receivePort,status,exit_status;
  char * inputFileWithCommands ;
  char * serverName ;
  struct sockaddr_in server ;
  struct sockaddr * serverptr = (struct sockaddr *) &server ;
  //struct sockaddr * clientptr = (struct sockaddr *) &client ;
  struct hostent * rem ;

  // Get the arguments
  if (argc != 5)
  {
    fprintf(stderr,"Wrong number of arguments\n");
    fprintf(stderr, "Parent process with pid = %d is terminated\n",getpid());
    exit(0);
  }

  serverName = argv[1] ;
  serverPORT = atoi(argv[2]) ;
  receivePort = atoi(argv[3]) ;
  inputFileWithCommands = argv[4] ;

  FILE *file = fopen (inputFileWithCommands,"r");

  if (file == NULL)
  {
    perror(inputFileWithCommands);
    exit(0);
  }

  int pid ;

  if ((pid=fork()) < 0)
    perror("Fork failed");

  if (pid != 0) // I am the parent (Send commands)
  {
    /*Create socket*/
    if(( sock = socket ( AF_INET , SOCK_STREAM , 0) ) < 0)
    {
      perror ("Socket error");
      exit (EXIT_FAILURE);
    }

    /*Find server address*/
    if ((rem = gethostbyname(serverName)) == NULL )
    {
      perror (" gethostbyname ");
      exit (1) ;
    }

    /*Initiate connection*/

    server.sin_family = AF_INET ;
    memcpy (&server.sin_addr, rem->h_addr, rem->h_length );
    server.sin_port = htons(serverPORT);

    if(connect(sock,serverptr,sizeof(server)) < 0)
    {
        perror ("Connect");
        exit (EXIT_FAILURE);
    }

    if (write(sock,argv[3],COMMANDLENGTH) < 0) // Send UDP port to server
    {
        perror ("Write failure");
        exit (EXIT_FAILURE);
    }

    printf ("Connecting to %s port %d\n",serverName,serverPORT);

    char command [COMMANDLENGTH];
    int cnt = 0;

    while (fgets(command,sizeof(command),file) != NULL) /* read file line by line */
    {
      command[strlen(command) - 1] = '\0' ; // Removing newline
      printf ("Sending command: %s\n",command);

      if (cnt == NUMOFMESSAGES) // Wait for five secs and then continue sending commands
      {
        sleep(5) ;
        cnt = 1 ;
      }
      else
      {
        cnt++;
      }

      // Send command
      if (write(sock,command,COMMANDLENGTH) < 0)
      {
          perror ("Write failure");
          fprintf(stderr, "Parent process with pid = %d is terminated\n",getpid());
          exit (EXIT_FAILURE);
      }

      if (strcmp(command,"timeToStop") == 0)
      {
          break;
      }

    }

    fprintf(stderr,"Finished sending commands\n");
    close (sock);
    fclose (file);

    // Wait for the child

    int childpid ;

    if ((childpid = wait(&status)) == -1) // If the calling process has no children
    {
      perror("Wait failed");
      fprintf(stderr, "Parent process with pid = %d is terminated\n",getpid());
      exit(0);
    }

	  fprintf(stderr,"Client (%d) parent process with pid = %d terminates with exit status %d.\n",receivePort,getpid(),exit_status);
    exit(0);

  }
  else // I am the child (Receive command results)
  {
    sleep(2);

    int sock_UDP ;
    char commandres [PACKAGELENGTH];
    struct hostent *rem_UDP ;
    struct sockaddr_in server_UDP , client_UDP ;
    struct sockaddr * serverptr_UDP = (struct sockaddr *) & server_UDP ;
    struct sockaddr * clientptr_UDP = (struct sockaddr *) & client_UDP ;
    unsigned int serverlen_UDP = sizeof(server_UDP);

    int NumOfCommands = CountFileLines(inputFileWithCommands);
    fprintf(stderr, "Number of commands %d\n",NumOfCommands);
    int PackageCounters[NumOfCommands][2];
    int CommandsCnt = 0;

    for (int i = 0 ; i<NumOfCommands ; i++) // Initialize package received counters to 0
    {
      PackageCounters[i][1] = 0;
    }

    if (( sock_UDP = socket ( AF_INET , SOCK_DGRAM , 0) ) < 0)
    {
        perror ("Socket UDP");
        fprintf(stderr, "Child process with pid = %d is terminated\n",getpid());
        exit(0);
    }

    if ((rem_UDP = gethostbyname(argv [1])) == NULL )
    {
      perror ("Gethostbyname ");
      fprintf(stderr, "Child process with pid = %d is terminated\n",getpid());
      exit(0) ;
    }

    /*Setupserver'sIPaddressandport*/
    server_UDP.sin_family = AF_INET ;
    memcpy (&server_UDP.sin_addr, rem_UDP->h_addr, rem_UDP->h_length );
    server_UDP.sin_port = htons (receivePort);

    /*Setupmyaddress*/
    client_UDP.sin_family = AF_INET ;
    client_UDP.sin_addr.s_addr = htonl (INADDR_ANY); /*Anyaddress*/
    client_UDP.sin_port = htons (0) ;

    int bd;

    // kill "Address already in use" error message
    if (setsockopt(sock_UDP,SOL_SOCKET,SO_REUSEADDR,&bd,sizeof(int)) == -1)
    {
        perror("setsockopt");
        fprintf(stderr, "Child process with pid = %d is terminated\n",getpid());
        exit(1);
    }

    if( bind (sock_UDP , clientptr_UDP ,sizeof(client_UDP)) < 0)
    {
      perror ("Bind UDP");
      fprintf(stderr, "Child process with pid = %d is terminated\n",getpid());
      exit (1) ;
    }

    fprintf(stderr, "Sending UDP message\n");

    int OrderOfCommand = 1;
    int TotalPackages = 1;
    int NumOfPackage = 1;

    while (1)
    {
      if (sendto (sock_UDP, "REQUEST", strlen("REQUEST") + 1, 0, serverptr_UDP, serverlen_UDP) < 0) // Connect to server
      {
        perror (" sendto ");
        fprintf(stderr, "Child process with pid = %d is terminated\n",getpid());
        exit (1) ;
      }

      fprintf(stderr, "Waiting for UDP message\n");

      if (recvfrom (sock_UDP, commandres, PACKAGELENGTH,0,NULL,NULL) < 0)
      {
        perror (" recvfrom ");
        fprintf(stderr, "Child process with pid = %d is terminated\n",getpid());
        exit (1) ;
      }

      fprintf(stderr, "Received UDP message\n");

      char commandres_slc [PACKAGELENGTH];
      strcpy(commandres_slc,commandres);

      char *w = strtok(commandres_slc,"@"); // Get useful information from package
      char *tokens3[10];

      int z = 0 ;

      tokens3[0] = w;

      while (w != NULL)
      {
         w = strtok (NULL,"@");
         tokens3[++z] = w;
      }

      OrderOfCommand = atoi(tokens3[0]);
      TotalPackages = atoi(tokens3[1]);
      NumOfPackage = atoi(tokens3[2]);
      strcpy(commandres,tokens3[3]);

      fprintf(stderr,"Package \n\n%s\n\nOrderOfCommand = %d\nTotalPackages = %d\nNumOfPackage = %d\n",commandres,OrderOfCommand,TotalPackages,NumOfPackage);

      PackageCounters[OrderOfCommand-1][0] = TotalPackages;
      PackageCounters[OrderOfCommand-1][1]++ ; // Received one package from the command with order = OrderOfCommand

      // Create file name and temporary file name
      char filename[30] ;
      char temp_filename[30] ;
      char temp[30] ;
      strcpy(temp_filename,"output.");
      strcat(temp_filename,argv[3]);
      strcat(temp_filename,".");
      sprintf(temp, "%d", OrderOfCommand);
      strcat(temp_filename,temp);
      strcpy(filename,temp_filename);
      strcat(temp_filename,".");
      sprintf(temp, "%d", NumOfPackage);
      strcat(temp_filename,temp);

      if (PackageCounters[OrderOfCommand-1][0] == 1) // TotalPackages
      {
        FILE *f = fopen(filename, "w");
        fprintf(f,"%s",commandres);
        fclose(f);
        CommandsCnt++ ;
      }
      else // for every package, save it in a temp file
      {
        FILE *f = fopen(temp_filename, "w"); // Create temporary file for each package
        fprintf(f,"%s",commandres);
        fclose(f);

        if (PackageCounters[OrderOfCommand-1][1] == PackageCounters[OrderOfCommand-1][0]) //  If we have received all packages of this command : Merge temp files and then erase them
        {
          FILE *final_file = fopen(filename, "a");
          CommandsCnt++;

          for (int i = 0 ; i<PackageCounters[OrderOfCommand-1][0] ; i++ )
          {
            char temp_filename1[50] ;
            char temp1[50] ;
            char temp2[50] ;
            char temp_data[128] ;
            strcpy(temp_filename1,"output.");
            strcat(temp_filename1,argv[3]);
            strcat(temp_filename1,".");
            sprintf(temp1, "%d", OrderOfCommand);
            strcat(temp_filename1,temp1);
            strcat(temp_filename1,".");
            sprintf(temp2, "%d", i+1);
            strcat(temp_filename1,temp2);

            FILE *temp_file = fopen(temp_filename1, "r"); // Open the temporary file for reading

            if (temp_file == NULL)
            {
              fprintf(stderr,"Cannot find file %s\n",temp_filename1);
            }

            while(fgets(temp_data, sizeof(temp_data), temp_file) != NULL)
            {
              fprintf(final_file,"%s",temp_data);
            }

            unlink(temp_filename1);
            fclose(temp_file);
          }
          fclose(final_file);
        }
      }

      if (CommandsCnt == NumOfCommands) // End program if we have received all commands
      {
        close(sock_UDP);
        fprintf(stderr,"Client (%d) child process with pid = %d terminates.\n",receivePort,getpid());
        exit(0);
      }

    }
  }
}
