#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <error.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <sys/wait.h>
#include <errno.h>
#include <math.h>
#include <signal.h>

#define READ 0
#define WRITE 1
#define COMMANDLENGTH 150
#define PACKAGELENGTH 512
#define COMMANDRESLENGTH 5120

void CatchSigUsr1 (int signo) // Parent catches SIGUSR1 (end command)
{
    int pid,status,exit_status;
    if((pid=wait(&status))==-1)
    {
        perror("Wait failed");
        exit(2);
    }

    if ((exit_status=WIFEXITED(status)))
    {
        fprintf(stderr,"Process %d exited with exit status %d because of SIGUSR1\n",pid,exit_status);
    }

    return;
}

void CatchSigUsr2 (int signo) // Parent catches SIGUSR2 (timeToStop command)
{
    int pid,status;

    kill(-getpid(),SIGTERM);
    while ((pid = wait(&status)) > 0); // Wait for all children to terminate

    fprintf(stderr,"Parent process %d exited\n",getpid());
    exit(0);
}

void CatchTerm (int signo)
{
    fprintf(stderr,"Child process %d exited\n",getpid());
    exit(0);

    return;
}

char *rtrim(char *str, const char *seps)
{
    int i;
    if (seps == NULL) {
        seps = "\t\n\v\f\r ";
    }
    i = strlen(str) - 1;
    while (i >= 0 && strchr(seps, str[i]) != NULL) {
        str[i] = '\0';
        i--;
    }
    return str;
}

char *ltrim(char *str, const char *seps)
{
    size_t totrim;
    if (seps == NULL) {
        seps = "\t\n\v\f\r ";
    }
    totrim = strspn(str, seps);
    if (totrim > 0) {
        size_t len = strlen(str);
        if (totrim == len) {
            str[0] = '\0';
        }
        else {
            memmove(str, str + totrim, len + 1 - totrim);
        }
    }
    return str;
}

char *trim(char *str, const char *seps)
{
    return ltrim(rtrim(str, seps), seps);
}

void pipeline(char *ar[], int pos, int in_fd, int write_dsc)
{
      static struct sigaction act;
      act.sa_handler=SIG_IGN;
      sigfillset(&(act.sa_mask));
      int status;

      if(ar[pos+1] == NULL)
      { /*last command */

        int pid = fork();

        sigaction(SIGPIPE,&act,NULL); // All processes will ignore SIGPIPE signal

        if (pid == 0)
        {
          sigaction(SIGPIPE,&act,NULL);
          dup2(write_dsc,STDOUT_FILENO);

            if(in_fd != STDIN_FILENO){
                if(dup2(in_fd, STDIN_FILENO) != -1)
                    close(in_fd); /*successfully redirected input*/
                else perror("dup2");
            }

            int z = 0;
            char *f = strtok(trim(ar[pos],NULL)," ");
            char *tokens1[100];
            char *arguments[10];

            tokens1[0] = f;

            while (f != NULL) // Find the arguments
            {
                f = strtok (NULL," ");
                tokens1[++z] = f;
                //fprintf(stderr,"%s,%d\n",f,i);
            }

            int j = 0;

            for (j=0;j<z;j++)
            {
                arguments[j] = tokens1[j];
                //fprintf(stderr,"Arguments %s\n",arguments[j]);
            }

            arguments[j] = NULL;

            execvp(trim(tokens1[0],NULL),arguments);
            perror("Failed to run execvp");
            fprintf(stderr, "%s\n",trim(tokens1[0],NULL));
        }
        else
        {
          sigaction(SIGPIPE,&act,NULL); // All processes will ignore SIGPIPE signal
          if((pid=wait(&status))==-1)
          {
              perror("Wait failed");
              exit(2);
          }

          fprintf(stderr,"Pipeline child (last command) with pid = %d is terminated\n",pid);
          return ;
        }
    }
    else
    {
        sigaction(SIGPIPE,&act,NULL);
        int fd[2];
        pid_t childpid;

        if ((pipe(fd) == -1) || ((childpid = fork()) == -1)) {
            perror("Failed to setup pipeline");
        }

        sigaction(SIGPIPE,&act,NULL); // All processes will ignore SIGPIPE signal

        if (childpid == 0)
        {
          /* child executes current command */
            close(fd[0]);
            if (dup2(in_fd, STDIN_FILENO) == -1) /*read from in_fd */
                perror("Failed to redirect stdin");
            if (dup2(fd[1], STDOUT_FILENO) == -1)   /*write to fd[1]*/
                perror("Failed to redirect stdout");
            else
            {
              int z = 0;
              char *f = strtok(trim(ar[pos],NULL)," ");
              char *tokens1[100];
              char *arguments[10];

              tokens1[0] = f;

              while (f != NULL) // Find the arguments
              {
                  f = strtok (NULL," ");
                  tokens1[++z] = f;
                  //fprintf(stderr,"%s,%d\n",f,i);
              }

              int j = 0;

              for (j=0;j<z;j++)
              {
                  arguments[j] = tokens1[j];
                  //fprintf(stderr,"Arguments %s\n",arguments[j]);
              }

              arguments[j] = NULL;

              sigaction(SIGPIPE,&act,NULL); // All processes will ignore SIGPIPE signal

              execvp(trim(tokens1[0],NULL),arguments);
              perror("Failed to run execvp");
              fprintf(stderr,"%s\n",trim(ar[pos],NULL));
            }
        }
        else
        {
          if((childpid=wait(&status))==-1)
          {
              perror("Wait failed");
              exit(2);
          }

          fprintf(stderr,"Pipeline child (last command) with pid = %d is terminated\n",childpid);
          close(fd[1]);   /* parent executes the rest of commands */
          close(in_fd);
          pipeline(ar, pos+1,fd[0],write_dsc);
        }
    }
}

int RunCommand(char * commands[],int write_dsc)
{
  pipeline(commands, 0, STDIN_FILENO,write_dsc);
  return 0;
}

int main (int argc, char* argv[])
{
    int sock,newsock;
    socklen_t clientlen ;
    struct sockaddr_in server, client ;
    struct sockaddr * serverptr = (struct sockaddr *) &server ;
    struct sockaddr * clientptr = (struct sockaddr *) &client ;
    static struct sigaction act,act1,act2;
    fd_set active_fd_set, read_fd_set;
    int bd;

    int n;
    int sock_UDP=0;
    unsigned int serverlen_UDP , clientlen_UDP ;
    char buf [PACKAGELENGTH];
    struct sockaddr_in server_UDP , client_UDP ;
    struct sockaddr * serverptr_UDP = (struct sockaddr *) & server_UDP ;
    struct sockaddr * clientptr_UDP = (struct sockaddr *) & client_UDP ;

    act.sa_handler=SIG_IGN;
    sigfillset(&(act.sa_mask));

    act1.sa_handler=CatchSigUsr1;
    sigfillset(&(act1.sa_mask));
    sigaction(SIGUSR1,&act1,NULL); // Catch SIGUSR1 signal

    act2.sa_handler=CatchSigUsr2;
    sigfillset(&(act2.sa_mask));
    sigaction(SIGUSR2,&act2,NULL); // Catch SIGUSR2 signal

    // Get the arguments
    if (argc != 3)
    {
      fprintf(stderr,"Wrong number of arguments\n");
      fprintf(stderr, "Parent process with pid = %d is terminated \n",getpid());
      exit(0);
    }

    int portNumber = atoi(argv[1]) ;
    int numChildren = atoi(argv[2]) ;

    if (numChildren <1)
    {
      fprintf(stderr,"Children must be one or more!\n");
      fprintf(stderr, "Parent process with pid = %d is terminated \n",getpid());
      exit(0);
    }

    int * ChildPids = (int*) malloc(sizeof(int)*numChildren);

    if ((sock = socket (AF_INET,SOCK_STREAM,0)) < 0)
    {
      perror ("Socket");
      fprintf(stderr, "Parent process with pid = %d is terminated \n",getpid());
      exit ( EXIT_FAILURE );
    }

    server.sin_family = AF_INET ;
    server.sin_addr.s_addr = htonl(INADDR_ANY);
    server.sin_port = htons (portNumber) ;

    int tr=1;

    // kill "Address already in use" error message
    if (setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&tr,sizeof(int)) == -1)
    {
        perror("setsockopt");
        fprintf(stderr, "Parent process with pid = %d is terminated \n",getpid());
        exit(1);
    }

    if (bind(sock,serverptr,sizeof(server)) < 0)
    {
      perror ("Bind");
      fprintf(stderr, "Parent process with pid = %d is terminated \n",getpid());
      exit (EXIT_FAILURE);
    }

    int pid = 1;

    if (listen(sock,5) < 0)
    {
        perror ("Listen");
        fprintf(stderr, "Parent process with pid = %d is terminated \n",getpid());
        exit (EXIT_FAILURE );
    }

    /* Initialize the set of active sockets. */
    FD_ZERO (&active_fd_set);
    FD_SET (sock, &active_fd_set);

    int p[2];

    while(1)
    {
      if (pid != 0) // Parent (Read command and send it to child through pipe (write pipe))
      {
        //fprintf(stderr,"(%d): Waiting at select function for another connection\n",getpid());
        read_fd_set = active_fd_set;
        if (select (FD_SETSIZE, &read_fd_set, NULL, NULL, NULL) < 0)
        {
          perror ("select");
          fprintf(stderr, "Parent process with pid = %d is terminated \n",getpid());
          exit (EXIT_FAILURE);
        }

        for (int i = 0; i < FD_SETSIZE; ++i)
          if (FD_ISSET (i, &read_fd_set))
          {
              if (i == sock)
              {
                  fprintf(stderr,"==================================\n");
                  fprintf(stderr,"(%d): Waiting for accept\n",getpid());
                  if ((newsock = accept(sock,clientptr,&clientlen)) < 0)
                  {
                    // perror ("Accept");
                    // if first accept fails due to a system interrupt or another reason, try again
                    newsock = accept(sock,clientptr,&clientlen);
                  }

                  FD_SET (newsock, &active_fd_set);

                  if (pipe(p)==-1) // Create pipe of connection between parent and children processes
                  {
                      perror("Pipe creation failed");
                      fprintf(stderr, "Parent process with pid = %d is terminated \n",getpid());
                      exit(1);
                  }

                  fprintf(stderr,"(%d): Accepted connection for accept\n",getpid());

                  char port[COMMANDLENGTH]; // Get UTP port from client

                  if (read(newsock,port,COMMANDLENGTH) < 0)
                  {
                    perror("Could not read port");
                  }

                  if (( sock_UDP = socket ( AF_INET, SOCK_DGRAM , 0) ) < 0)
                  {
                      perror ("Socket UDP");
                      fprintf(stderr, "Parent process with pid = %d is terminated \n",getpid());
                      exit(10);
                  }

                  server_UDP.sin_family = AF_INET ;
                  server_UDP.sin_addr.s_addr = htonl (INADDR_ANY);
                  server_UDP.sin_port = htons (atoi(port)) ;
                  serverlen_UDP = sizeof(server_UDP);

                  // kill "Address already in use" error message
                  if (setsockopt(sock_UDP,SOL_SOCKET,SO_REUSEADDR,&bd,sizeof(int)) == -1)
                  {
                      perror("setsockopt");
                      fprintf(stderr, "Parent process with pid = %d is terminated \n",getpid());
                      exit(1);
                  }

                  if( bind (sock_UDP , serverptr_UDP , serverlen_UDP ) < 0)
                  {
                      perror ("Bind UDP");
                      fprintf(stderr, "Parent process with pid = %d is terminated \n",getpid());
                      exit(10);
                  }

                  /*Discover selected port*/
                  if( getsockname (sock_UDP , serverptr_UDP , &serverlen_UDP ) < 0)
                  {
                      perror ("Get sock name");
                      fprintf(stderr, "Parent process with pid = %d is terminated \n",getpid());
                      exit(10);
                  }

                  printf ("Socket port : %d\n",ntohs(server_UDP.sin_port ));
                  clientlen_UDP = sizeof(client_UDP);

                  for (int i=0;i<numChildren;i++) // Generate numChildren children
                  {
                    if (pid > 0)
                    {
                      pid = fork();
                      ChildPids[i] = pid;
                    }
                    else if (pid < 0)
                    {
                      perror("Failed to fork");
                      fprintf(stderr, "Parent process with pid = %d is terminated \n",getpid());
                      exit(1);
                    }
                    else //  (pid==0)
                      break;
                  }

                  sigaction(SIGPIPE,&act,NULL); // All processes will ignore SIGPIPE signal

                  if (pid == 0)
                    break;
              }
              else
              {
                int commandCounter = 0 ;

                //close(p[READ]);
                char command[COMMANDLENGTH];
                char command_slc[COMMANDLENGTH];

                //fprintf(stderr,"%d\n",getpid());

                while(read(newsock,command,COMMANDLENGTH) > 0) // Read command from socket and send command to child through pipe
                {

                  commandCounter++ ;

                   // Send command to server (TCP)
                  int isCommandValid = 1 ;
                  char temp_commmand1[COMMANDLENGTH];

                  int i = 0 ;
                  int y = 0 ;

                  while((command[i]!='\0') && (command[i]!=' ') && (command[i]!='\n')) // Check if first command is valid
                  {
                      temp_commmand1[y++] = command[i++];
                  }

                  temp_commmand1[y] = '\0';

                  if ((strcmp(temp_commmand1,"ls") == 0) || (strcmp(temp_commmand1,"grep")==0) || (strcmp(temp_commmand1,"cat") == 0) || (strcmp(temp_commmand1,"cut")==0) || (strcmp(temp_commmand1,"tr") ==0) || (strcmp(temp_commmand1,"end")==0) || (strcmp(temp_commmand1,"timeToStop")==0))
                  {
                    //fprintf(stderr,"Command is valid (2): %s\n",temp_commmand1);
                  }
                  else
                  {
                    //fprintf(stderr,"Command is valid (3): %s\n",temp_commmand1);
                    strcpy(command,"NOT_VALID");
                    isCommandValid= 0 ;
                  }

                  // Send command to server (TCP)

                  i = 0 ;

                  if ((strlen(command) > 100) || (isCommandValid == 0))
                    strcpy(command,"NOT_VALID");
                  else
                  {
                    while(command[i]!='\0') // Get rid of first semicolon and all following calls
                    {
                        if ((command[i]==';') || (command[i]=='\n'))
                        {
                            command[i]='\0';
                        }
                        i++;
                    }

                    i = 0;

                    while (command[i]!='\0') // Get rid of wrong commands in pipes
                    {
                        char temp_command[100];

                        if ((command[i]=='|'))
                        {
                            int z = 0 ;
                            int j = i + 1 ;

                            while (command[j] == ' ') // Ignore spaces at the beginning of the command
                            {
                              j++;
                            }

                            while((command[j] != ' ') && (command[j]!='\0') && (command[j]!='\n'))  // Read command after pipe
                            {
                              temp_command[z++] = command[j];
                              //printf("PROBLEM (%c)\n",command[j]);
                              j++;
                            }

                            temp_command[z] = '\0';

                            //fprintf(stderr,"Temp command is %s\n",temp_command);

                            if ((strcmp(temp_command,"ls") == 0) || (strcmp(temp_command,"grep")==0) || (strcmp(temp_command,"cat") == 0) || (strcmp(temp_command,"cut")==0) || (strcmp(temp_command,"tr") ==0) || (strcmp(temp_command,"end")==0) || (strcmp(temp_command,"timeToStop")==0))
                            {
                              //fprintf(stderr,"Command is valid (2): %s\n",command);
                            }
                            else
                            {
                              command[i] = '\0';
                              break;
                            }
                        }
                        i++;
                    }
                  }

                   fprintf(stderr, "Received command <<%s>> with order %d\n",command,commandCounter);
                   //trim(command,NULL); // Remove useless characters from command

                   // Tokenize the command using space as delimiter
                   i = 0 ;
                   strcpy(command_slc,command);

                   char *f = strtok(command_slc," ");
                   char *tokens[100];

                   tokens[0] = f;

                   while (f != NULL)
                   {
                      f = strtok (NULL," ");
                      tokens[++i] = f;
                   }

                   if (strcmp(tokens[0],"timeToStop")==0) // If the command is timeToStop, wait a little before running it, so that all previous commands are complete
                   {
                     sleep(5);
                   }

                  char new_command[COMMANDLENGTH];
                  sprintf(new_command, "%d", commandCounter);
                  strcat(new_command,"@");
                  strcat(new_command,command);

                  //fprintf(stderr, "New command is %s\n",new_command);

                  if (write(p[WRITE],new_command,COMMANDLENGTH) < 0)
                  {
                     perror("Failed to write");
                     fprintf(stderr, "Parent process with pid = %d is terminated \n",getpid());
                     exit(1);
                  }

                  if (strcmp(tokens[0],"end")==0) // If the command is end, wait a little before running it, so that all previous commands are complete
                  {
                    sleep(5);
                  }
                }
              }
            }
      }
      else // Child (Run command (read pipe) and send its result back through UDP)
      {
        sigaction(SIGPIPE,&act,NULL); // All processes will ignore SIGPIPE signal

        static struct sigaction act3;
        act3.sa_handler=CatchTerm;
        sigfillset(&(act3.sa_mask));
        sigaction(SIGTERM,&act3,NULL); // Catch SIGTERM signal

        //close(p[WRITE]);
        char * command_rec = (char *) malloc(COMMANDLENGTH);
        char * command_slc1 = (char *) malloc(COMMANDLENGTH);

        while(read(p[READ],command_rec,COMMANDLENGTH) > 0) // Send command to child through pipe
        {
            // Get the order of the command and the command itself
            char * command_slc2 = (char *) malloc(COMMANDLENGTH);
            strcpy(command_slc2,command_rec);

            char *w = strtok(command_slc2,"@");
            char *tokens3[10];

            int z = 0 ;

            tokens3[0] = w;

            while (w != NULL)
            {
               w = strtok (NULL,"@");
               tokens3[++z] = w;
            }

            strcpy(command_rec,tokens3[1]);
            int OrderOfCommand = atoi(tokens3[0]);

            fprintf(stderr,"The child with pid = %d has received the command <<%s>> of order %d \n",getpid(),command_rec,OrderOfCommand);
            char * command_results = (char *) malloc(COMMANDRESLENGTH);

            int res_pipe[2];

            if (pipe(res_pipe)==-1)
            {
                perror("Pipe creation failed");
                fprintf(stderr, "Child process with pid = %d is terminated \n",getpid());
                exit(1);
            }

            int isCommandValid = 1 ;

            if (strcmp(command_rec,"NOT_VALID") == 0)
            {
              isCommandValid = 0 ;
            }
            else if (strcmp(command_rec,"end") == 0) // If command is end, release resourses, alert parent with SIGUSR1 and terminate
            {
                int ParentPid = getppid();
                close(p[READ]);
                close(p[WRITE]);
                close(sock);
                close(sock_UDP);
                free(command_rec);
                free(command_slc1);
                free(command_slc2);
                free(command_results);
                close(res_pipe[READ]);
                close(res_pipe[WRITE]);
                kill (ParentPid,SIGUSR1);
                fprintf(stderr, "Child process with pid = %d is terminated \n",getpid());
                exit(0);
            }
            else if (strcmp(command_rec,"timeToStop") == 0) // If command is end, release resourses, alert parent with SIGUSR2
            {
                int ParentPid = getppid();
                sleep(1); // Wait a second before releasing resources
                free(command_rec);
                free(command_slc1);
                free(command_slc2);
                free(command_results);
                close(p[READ]);
                close(p[WRITE]);
                close(res_pipe[READ]);
                close(res_pipe[WRITE]);
                close(sock);
                close(sock_UDP);
                kill (ParentPid,SIGUSR2);
            }

            if (isCommandValid == 1) // We have a valid command
            {
              // Split the command if it has pipes
              strcpy(command_slc1,command_rec);
              char *f = strtok(command_slc1,"|");
              char *tokens2[10];

              int i = 0 ;

              tokens2[0] = f;

              while (f != NULL)
              {
                 f = strtok (NULL,"|");
                 tokens2[++i] = f;
              }

              tokens2[i] = NULL ;

              RunCommand(tokens2,res_pipe[WRITE]);
            }
            else // Not valid
            {
              if (write(res_pipe[WRITE]," ",2) < 0)
              {
                 fprintf(stderr, "Child process with pid = %d is terminated \n",getpid());
                 perror("Failed to write (2)");
                 exit(0);
              }
            }

            //fprintf(stderr, "Child %d returned\n",getpid());

            int numofCharacters = 0 ;

            if ((numofCharacters = read(res_pipe[READ],command_results,COMMANDRESLENGTH)) > 0)
            {
              command_results[numofCharacters-1] = '\0';
              //fprintf(stderr, "I read %d characters\n",numofCharacters);
              //fprintf(stderr,"====================\n");
              int LengthCommand = strlen(command_results);
              fprintf(stderr,"Command: <<%s>>, Order: %d, Length: %d\n",command_rec,OrderOfCommand,LengthCommand);

              //fprintf(stderr, "\n%s\n", command_results);

              // Send results to client (CommandOrder@NumOfPackages@PackageNum@Results)

              int PackageNum ;

              fprintf(stderr, "Waitng for UDP connection message\n");

                /*Receive message*/
                if((n = recvfrom (sock_UDP , buf ,8 , 0, clientptr_UDP , &clientlen_UDP)) <0)
                      perror ("Recv from");

                int NumOfPackages = ceil((float) LengthCommand/PACKAGELENGTH);

                if (NumOfPackages == 0)
                {
                    NumOfPackages = 1 ;
                }

                fprintf(stderr, "NumOfPackages: %d\n",NumOfPackages );

                int WritePoint = 0;

                for (int i = 0; i<NumOfPackages ; i++)
                {
                  char * Package = (char *) malloc(PACKAGELENGTH);
                  char * TempPackage = (char *) malloc(PACKAGELENGTH);
                  char * temp = (char *) malloc(20);
                  char * PackageInf = (char *) malloc(20);

                  // Fix package info

                  sprintf(PackageInf, "%d", OrderOfCommand);
                  strcat(PackageInf,"@");

                  sprintf(temp, "%d", NumOfPackages);
                  strcat(PackageInf,temp);
                  strcat(PackageInf,"@");

                  PackageNum = i+1;
                  sprintf(temp, "%d", PackageNum);
                  strcat(PackageInf,temp);
                  strcat(PackageInf,"@");

                  strcpy(Package,PackageInf);

                  /////////////////////////////

                  int infosize = strlen(PackageInf);

                  //WritePoint = WritePoint + NumOfBytes;

                  strncpy(TempPackage, command_results + PACKAGELENGTH*i - WritePoint, PACKAGELENGTH - infosize);
                  strcat(Package,TempPackage);

                  WritePoint += infosize;

                  //Package[strlen(Package)] = '\0';

                  fprintf(stderr, "Sending package: \n\n%s\n\n",Package);

                  if(sendto(sock_UDP,Package, PACKAGELENGTH,0,clientptr_UDP , clientlen_UDP ) <0)
                  {
                      perror ("Send to");
                      fprintf(stderr, "Child process with pid = %d is terminated \n",getpid());
                      exit(10);
                  }
                  free(Package);
                  free(TempPackage);
                  free(PackageInf);
                  free(temp);
                }
              }
              close(res_pipe[READ]);
            }
        }
      }
}
