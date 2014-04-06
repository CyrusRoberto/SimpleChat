#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdarg.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include "uthash.h"
#include "utlist.h"
#define MAX_LEN 512
#define DEF_PRT 8000
#define DEF_SNM "[INSERT NAME HERE]"

void *session(void*); //user session that will be run on multiple threads

//forward declarations
struct user;
struct room;

typedef struct user
{
	int sock;
	char *name;
	struct room *curroom;
	struct user *prev; //for the linked lists used in rooms
	struct user *next; //for the linked lists used in rooms
	UT_hash_handle hh;
} user;

typedef struct room
{
	char *name; //room name
	user *curusers; //linked list of users in room
	int count; //number of users in room
	int per; //persistence; whether room should be destroyed when empty
	UT_hash_handle hh;
} room;

room *rooms = NULL; //hash table of rooms, string as key
user *users = NULL; //hash table of users connected to network, string as key
char servname[MAX_LEN];

pthread_rwlock_t lock; //for thread safety

int sort_by_name(user *a, user *b)
{
  return strcmp(a->name,b->name);
}

void error(const char *msg)
{
	perror(msg);
	exit(1);
}

room *create_room(char *str,int per) //creates a room on the server, run within main thread
{
	room *r;
	char *name = malloc(strlen(str + 1));
	strcpy(name,str);
	
	if (pthread_rwlock_rdlock(&lock) != 0) error("can't get rdlock");
	HASH_FIND_STR(rooms, name, r); //room already exists?
	pthread_rwlock_unlock(&lock);
	
	if(r == NULL)
	{
		r = (room *) malloc(sizeof(room));
		r->name = name;
		r->count = 0;
		r->curusers = NULL;
		r->per = per;
		if (pthread_rwlock_wrlock(&lock) != 0) error("can't get wrlock");
		HASH_ADD_KEYPTR(hh, rooms, r->name, strlen(r->name),r);
		pthread_rwlock_unlock(&lock);
		
		printf("Room created: %s\n",r->name);
	}
	else
	{
		printf("Room %s already exists\n",name);
		free(name);
	}
	return r;
}

int main(int argc, char *argv[])
{
	int sockfd, newsockfd, portno, pid, option = 1;
	if(pthread_rwlock_init(&lock,NULL) != 0) error("can't create rwlock");
	
	socklen_t clilen;
	pthread_t thread_id;
	
	struct sockaddr_in serv_addr, cli_addr;
	
	if(argv[1] == NULL)
	{
		printf("ERROR, no server name specified\n");
		sprintf(servname,DEF_SNM);
	}
	else sprintf(servname,"%s",argv[1]);
	
	if(argc < 3)
	{
		printf("ERROR, no port provided, defaulting to port %d\n",DEF_PRT);
		portno = DEF_PRT;
	}
	else portno = atoi(argv[2]);
	
	create_room("lobby",1);
	create_room("64digits",1);
	create_room("hottub",1);
	
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) error("ERROR opening socket");
	bzero((char *) &serv_addr, sizeof(serv_addr));
	
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = htons(portno);
	
	if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (char*)&option, sizeof(option)) < 0) error("setsockopt failed");
	if (bind(sockfd, (struct sockaddr *) &serv_addr,sizeof(serv_addr)) < 0) error("ERROR on binding");
	
	listen(sockfd,5);
	clilen = sizeof(cli_addr);
	
	while((newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, (socklen_t*)&clilen)) > 0)
	{
		printf("Client has connected\n");
		if(pthread_create(&thread_id, NULL, session, (void*) &newsockfd) < 0)
		{
			perror("could not create thread");
			close(newsockfd);
		}
	}
	
	if (newsockfd < 0) error("ERROR on accept");
	close(sockfd);
	return 0;
}

void send_message(int sock, char * tmpstr, char * str, ...)
{
	va_list argptr;
	va_start(argptr, str);
	vsprintf(tmpstr, str, argptr);
	va_end(argptr);
	if(write(sock,tmpstr,strlen(tmpstr)) < 0) error("ERROR writing to socket");
}

void leave_room(user *u)
{
	user *curuser;
	char tmpstr[MAX_LEN];
	
	if (pthread_rwlock_rdlock(&lock) != 0) error("can't get rdlock");
	DL_FOREACH(u->curroom->curusers,curuser)
	{
		if(u == curuser) send_message(curuser->sock,tmpstr,"\r<= * user has left the chat: %s (** this is you)\n=> ",u->name);
		else send_message(curuser->sock,tmpstr,"\r<= * user has left the chat: %s\n=> ", u->name);
	}
	pthread_rwlock_unlock(&lock);
	
	if (pthread_rwlock_wrlock(&lock) != 0) error("can't get wrlock");
	DL_DELETE(u->curroom->curusers,u);
	u->curroom->count--;
	pthread_rwlock_unlock(&lock);
	
	if(u->curroom->count == 0 && u->curroom->per == 0) //room persistence
	{
		printf("Nonpersistent room %s has been deleted\n",u->curroom->name);
		if (pthread_rwlock_wrlock(&lock) != 0) error("can't get wrlock");
		HASH_DEL(rooms, u->curroom);
		pthread_rwlock_unlock(&lock);
	}
	u->curroom = NULL;
}

void list_users(user *u, room *r,char *tmpstr, int joined) //send user a list of users in room
{
	user *curuser;
	send_message(u->sock,tmpstr,"\r<= Users in room:\n",u->name);	
	
	if (pthread_rwlock_rdlock(&lock) != 0) error("can't get rdlock");
	DL_FOREACH(r->curusers,curuser)
	{
		if(curuser == u) send_message(u->sock,tmpstr,"<= * %s (** this is you)\n",curuser->name);
		else
		{
			send_message(u->sock,tmpstr,"<= * %s\n",curuser->name);
			if(joined) send_message(curuser->sock,tmpstr,"\r<= * new user joined chat: %s\n=> ",u->name);
		}
	}
	pthread_rwlock_unlock(&lock);
	send_message(u->sock,tmpstr,"\r<= end of list.\n=> ");
}

void process_string(user *u, char *str, char *tmpstr)
{
	
	strcpy(tmpstr,str);
	char *token;
	token = strtok(tmpstr," \n\r"); //need to also remove carriage return!
	
	if(token == NULL)
	{
		send_message(u->sock,tmpstr,"\r=> ");
		return;
	}
	
	if(strcmp(token,"/quit") == 0) //disconnect from network
	{
		if(u->curroom != NULL) leave_room(u);
		send_message(u->sock,tmpstr,"\r<= BYE\n");
		close(u->sock);
		return;
	}
	
	if(u->name == NULL) //assign a name to a new user
	{
		user *utmp;
		char *name = malloc(strlen(token + 1));
		strcpy(name,token);
		
		if (pthread_rwlock_rdlock(&lock) != 0) error("can't get rdlock");
		HASH_FIND_STR(users, name, utmp);
		pthread_rwlock_unlock(&lock);
		
		if(utmp == NULL)
		{
			u->name = name;
			
			//add user to user hash table
			if (pthread_rwlock_wrlock(&lock) != 0) error("can't get wrlock");
			HASH_ADD_KEYPTR(hh, users, u->name, strlen(u->name),u);
			pthread_rwlock_unlock(&lock);
			
			//greet newly created user
			send_message(u->sock,tmpstr,"\r<= Welcome, %s!\n",u->name);
			send_message(u->sock,tmpstr,"\r<= For a list of commands, type \"/help\"\n=> ");
			printf("Client has assumed the name \"%s\"\n",u->name);
		}
		else
		{
			send_message(u->sock,tmpstr,"\r<= Sorry, name taken.\n<= Login name?\n=> ");
			free(name);
		}
		return;
	}

	if(strcmp(token,"/help") == 0) //list of commands
	{
		send_message(u->sock,tmpstr,"\r<= To view a list of rooms, type \"/rooms\"\n");
		send_message(u->sock,tmpstr,"\r<= To join or create a room, type \"/join [roomname]\"\n");
		send_message(u->sock,tmpstr,"\r<= To view a list of users in a room, type \"/users\"\n");
		send_message(u->sock,tmpstr,"\r<= To send a private message to a user, type \"/msg [username]\"\n");
		send_message(u->sock,tmpstr,"\r<= To leave a room, type \"/leave\"\n");
		send_message(u->sock,tmpstr,"\r<= To disconnect from the server, type \"/quit\"\n");
		send_message(u->sock,tmpstr,"\r<= To bring up this list again, type \"/help\"\n=> ");
		return;
	}
	else if(strcmp(token,"/join") == 0) //join a room
	{
		room *r;
		user *curuser, *tmp;
		token = strtok(NULL," \n\r");
		
		if(token == NULL)
		{
			send_message(u->sock,tmpstr,"\r<= No room specified\n=> ");
			return;
		}
		
		if (pthread_rwlock_rdlock(&lock) != 0) error("can't get rdlock");
		HASH_FIND_STR(rooms, token, r); //if room already exists in hash table
		pthread_rwlock_unlock(&lock);
		
		if(r == NULL) //if room does not exist, then create the room
		{
			r = create_room(token,0);
			printf("%s has created room %s\n",u->name, r->name);
			send_message(u->sock,tmpstr,"\r<= creating and entering room: %s\n",r->name);
		}
		else //then join that room
		{
			printf("%s has entered room %s\n",u->name, r->name);
			send_message(u->sock,tmpstr,"\r<= entering room: %s\n",r->name);
		}
		
		//if user is already in a room, leave it
		if(u->curroom != NULL) leave_room(u);
		u->curroom = r;
		
		/*add current user to the room's linked list of current users, and then
		  sort that list in alphabetical order*/
		if (pthread_rwlock_wrlock(&lock) != 0) error("can't get wrlock");
		DL_APPEND(r->curusers,u);
		DL_SORT(r->curusers,sort_by_name);
		r->count++;
		pthread_rwlock_unlock(&lock);
		
		list_users(u,u->curroom,tmpstr,1);
		
		return;
	}
	else if(strcmp(token,"/rooms") == 0) //list rooms
	{
		room *curroom, *tmp;
		send_message(u->sock,tmpstr,"\r<= Active rooms are:\n");
		
		if (pthread_rwlock_rdlock(&lock) != 0) error("can't get rdlock");
		HASH_ITER(hh, rooms, curroom, tmp) send_message(u->sock,tmpstr,"<= * %s (%d)\n",curroom->name,curroom->count);
		pthread_rwlock_unlock(&lock);
		
		send_message(u->sock,tmpstr,"\r<= end of list.\n=> ");
		return;
	}
	else if(strcmp(token,"/leave") == 0) //leave room
	{
		leave_room(u);
		return;
	}
	else if(strcmp(token,"/msg") == 0) //private message a user
	{
		
		token = strtok(NULL," \n\r");
		if(token != NULL)
		{
			user *destusr;
			char *name = malloc(strlen(token) + 1);
			strcpy(name,token);
			if (pthread_rwlock_rdlock(&lock) != 0) error("can't get rdlock");
			HASH_FIND_STR(users, name, destusr);
			pthread_rwlock_unlock(&lock);
		
			if(destusr == NULL) send_message(u->sock,tmpstr,"\r<= User \"%s\" does not exist\n=> ",name);
			else
			{
				/*sendstr points to the part of str that contains the message we want to send
				  to the destination user*/
				token = strtok(NULL,"\n\r");
				if(token != NULL)
				{
					char *msg = malloc(strlen(token) + 1);
					strcpy(msg,token);
					send_message(destusr->sock,tmpstr,"\r<= PM from %s: %s\n=> ",u->name,msg); //str already has newline
					free(msg);
				}
			}
			free(name);
		}
		send_message(u->sock,tmpstr,"\r=> ");
		return;
	}
	else if(strcmp(token,"/users") == 0) //list users in room
	{
		if(u->curroom != NULL) list_users(u,u->curroom,tmpstr,0);
		else send_message(u->sock,tmpstr,"=> ");
		return;
	}
	
	if(u->curroom != NULL) //message sent to all users in room
	{
		user *curuser;
		DL_FOREACH(u->curroom->curusers,curuser) send_message(curuser->sock,tmpstr,"\r<= %s: %s=> ",u->name,str);
	}
	else send_message(u->sock,tmpstr,"=> ");
}

void *session(void *sock_desc)
{
	int n, read_size, sock = *(int*)sock_desc;
	char rbuffer[MAX_LEN];
	char tbuffer[MAX_LEN];
	
	/*when user is connected, create new user object and add to hash table when
	  user finally creates a name*/
	user *u = (user *) malloc(sizeof(user));
	u->sock = sock;
	u->name = NULL;
	u->curroom = NULL;
	
	send_message(sock,tbuffer,"\r<= Welcome to the %s Chat Server!\n",servname);
	send_message(sock,tbuffer,"\r<= Login name?\n=> ");
	
	while((read_size = recv(sock,rbuffer,MAX_LEN,0)) > 0)
	{
		process_string(u,rbuffer,tbuffer);
		//clear buffers
		bzero(tbuffer,MAX_LEN);
		bzero(rbuffer,MAX_LEN);
	}
	/*when user finally disconnects, remove user from user hash table and any rooms
	  they happen to be in when disconnected.*/
	if(u->name != NULL) //check if a user disconnects before choosing a name
	{
		printf("User %s has disconnected from the server\n",u->name);
		if(u->curroom != NULL) leave_room(u);
		HASH_DEL(users, u);
	}
	else printf("Client has disconnected\n");
	free(u);
	return 0;
}
