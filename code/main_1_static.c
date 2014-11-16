#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

#define FAILURE -1 	/* Macro for Failure */
#define SUCCESS  0	/* Macro for Success */

/*Device message structure */
struct dev_msg {
	int messageId;			/* Message Id */
	int sourceId;			/* Source Id*/
	int destinationId;		/* Destination Id */
	char msgString[80];		/* Message string of max size 80 */
	int msgLength;			/* Length of the Message */
	unsigned long long enqtime;		/* Enqueue Time stamp */
	unsigned long long timeStamp;		/* Time Stamp */
};

/* End notifications for threads*/
int g_sender1Check = 0;
int g_sender2Check = 0;
int g_sender3Check = 0;
int g_daemonCheck = 0;

/* Global message Id */
int g_msgIdCount = 1;

/* Mutex for scheduling */
pthread_mutex_t senderLock;
pthread_mutex_t receiver1Lock;
pthread_mutex_t receiver2Lock;
pthread_mutex_t receiver3Lock;

/* Function Protypes */
void* pthread_write_sender(void *threadId_p);
void* pthread_read_receiver(void *device_p);
void* pthread_transfer_daemon(void*);

/*
 * Main function - This creates all the threads. 
 */
int main(int argc, char **argv){
	pthread_t sender1, sender2, sender3, receiver1, receiver2, receiver3, daemon;
	int sender1Ret, sender2Ret, sender3Ret;
	int receiver1Ret, receiver2Ret, receiver3Ret;
	int daemonRet;	
	int i = 0;
	int threadid1 = 1;
	int threadid2 = 2;
	int threadid3 = 3;

	/* Initialize mutex locks */
	if((pthread_mutex_init(&senderLock, NULL) != 0) &&
		(pthread_mutex_init(&receiver1Lock, NULL) != 0) &&
		(pthread_mutex_init(&receiver2Lock, NULL) != 0) &&
		(pthread_mutex_init(&receiver3Lock, NULL) != 0)){
		////printf("Mutex locks initialization failed");
	}

	/* Create sender thread1 */
	sender1Ret = pthread_create(&sender1, NULL, pthread_write_sender, (void*)&threadid1); 
	if(sender1Ret){
		//fprintf(stderr, "pthread_create failed: %d\n", sender1Ret);
		return 0;
	}

	/* Create sender thread2 */
	sender2Ret = pthread_create(&sender2, NULL, pthread_write_sender, (void*)&threadid2);
	if(sender2Ret){
		//fprintf(stderr, "pthread_create failed: %d\n", sender2Ret);
		return 0;
	}

	/* Create sender thread3 */
	sender3Ret = pthread_create(&sender3, NULL, pthread_write_sender, (void*)&threadid3);
	if(sender3Ret){
		//fprintf(stderr, "pthread_create failed: %d\n", sender3Ret);
		return 0;
	}

	/* Create daemon thread */
	daemonRet = pthread_create(&daemon, NULL, pthread_transfer_daemon, NULL); 
	if(daemonRet){
		//fprintf(stderr, "pthread_create failed: %d\n", daemonRet);
		return 0;
	}

	/* Create receiver thread1 */
	receiver1Ret = pthread_create(&receiver1, NULL, pthread_read_receiver, (void*)"bus_out_q1"); 
	if(receiver1Ret){
		//fprintf(stderr, "pthread_create failed: %d\n", receiver1Ret);
		return 0;
	}

	/* Create receiver thread2 */
	receiver2Ret = pthread_create(&receiver2, NULL, pthread_read_receiver, (void*)"bus_out_q2"); 
	if(receiver2Ret){
		//fprintf(stderr, "pthread_create failed: %d\n", receiver2Ret);
		return 0;
	}

	/* Create receiver thread3 */
	receiver3Ret = pthread_create(&receiver3, NULL, pthread_read_receiver, (void*)"bus_out_q3"); 
	if(receiver3Ret){
		//fprintf(stderr, "pthread_create failed: %d\n", receiver3Ret);
		return 0;
	}

	/* Join all the threads */
	pthread_join(sender1, NULL);
	pthread_join(sender2, NULL);
	pthread_join(sender3, NULL);
	pthread_join(daemon,NULL); 
	pthread_join(receiver1, NULL);
	pthread_join(receiver2, NULL);
	pthread_join(receiver3, NULL);
	
	return 0;
}

/*
 * Function for the daemon thread operations
 */
void* pthread_transfer_daemon(void* arg_p){
	struct dev_msg message;
	int fd,res;
	time_t base = time(0);
	pthread_mutex_t receiverLock;

	while(1){
	/* Mutex Lock */
	pthread_mutex_lock(&senderLock);

	/* Read for bus_in_q */
	fd = open("/dev/bus_in_q", O_RDWR);
	if (fd < 0){
		//printf("Can not open device file bus_in_q daemon.\n");		
		//fprintf(stderr, "open() failed: %s\n", strerror(errno));

		/* Mutex Unlock */
		pthread_mutex_unlock(&senderLock);

		return;
	}
	else{
		res = read(fd, (char*)&message, sizeof(message));

		/* Check if reading failied */
		if(res == -1){
			/* Mutex Unlock */
			pthread_mutex_unlock(&senderLock);

			/* Close device */	
			close(fd);
			
			/* Update errno */			
			errno = 0;
	
			/* Check if senders have stopped */
			if((g_sender1Check == 1) && (g_sender2Check == 1) && (g_sender3Check == 1)){
				g_daemonCheck = 1;
				break;
			}
			
			/* Sleep for a random period between 1 to 10ms */
			usleep(((rand() % 10) + 1) * 1000);;
			
			continue;
		}
	}

	/* Close device */	
	close(fd);
		
	/* Mutex Unlock */
	pthread_mutex_unlock(&senderLock);

	/* Select which destination device to open */
	if(message.destinationId == 0){
		receiverLock = receiver1Lock;	
		pthread_mutex_lock(&receiverLock);
		fd = open("/dev/bus_out_q1", O_RDWR);
	}
	else if(message.destinationId == 1){
		receiverLock = receiver2Lock;
		pthread_mutex_lock(&receiverLock);
		fd = open("/dev/bus_out_q2", O_RDWR);	
	}
	else{
		receiverLock = receiver3Lock;
		pthread_mutex_lock(&receiverLock);
		fd = open("/dev/bus_out_q3", O_RDWR);
	}
	
	if (fd < 0){
		//printf("Can not open device file in daemon.\n");		
		//fprintf(stderr, "open() failed: %s\n", strerror(errno));

		/* Mutex Unlock */
		pthread_mutex_unlock(&receiverLock);

		continue;
	}
	else{
		/* Write to the device file */
		res = write(fd, (char*)&message, sizeof(message));

		if(res == -1){
			/* Mutex Unlock */
			pthread_mutex_unlock(&receiverLock);

			/* Close device */	
			close(fd);

			/* Update errno */			
			errno = 0;

			/* Sleep for a random period between 1 to 10ms */
			usleep(((rand() % 10) + 1) * 1000);;

			continue;
		}		
	}
	
	/* Close device */	
	close(fd);

	/* Mutex Unlock */
	pthread_mutex_unlock(&receiverLock);
	}
}

/*
 * Function of Input thread
 */
void* pthread_write_sender(void* threadId_p){
	int fd,res;
	int msgCount = 1;
	int *threadId = (int*)threadId_p;
	time_t base = time(0);
	struct dev_msg newMessage;

	while((time(0) - base) <= 10){
		/* Mutex Lock */
		pthread_mutex_lock(&senderLock);

		/* open devices */
		fd = open("/dev/bus_in_q", O_RDWR);
		if (fd < 0){
			//printf("Can not open device file in writer sender.\n");		
			//fprintf(stderr, "open() failed: %s\n", strerror(errno));

			/* Mutex Unlock */
			pthread_mutex_unlock(&senderLock);

			return;
		}
	
		/* Populate the data structure */
		sprintf(newMessage.msgString, "%s%d", "This is a message from Thread - ", *threadId);
		newMessage.msgLength = strlen(newMessage.msgString);
		newMessage.enqtime = 0;
		newMessage.timeStamp = 0;
		newMessage.sourceId = *threadId;
		newMessage.messageId = g_msgIdCount++;
		newMessage.destinationId = (rand() % 3);

		/* Write to the device file */
		res = write(fd, (char*)&newMessage, sizeof(newMessage));

		/* Print the result */
		if(res < 0){
			//printf("Writing Failed - Bus full\n");		
			errno = 0;
		}
		else{
			//printf("Wrote - MId - %d, DId - %d, Message - '%s'\n", 
			//	newMessage.messageId, newMessage.destinationId, newMessage.msgString);					
		}

		/* Close the device */		
		close(fd);
		
		/* Mutex Unlock */
		pthread_mutex_unlock(&senderLock);

		/* Sleep for a while */
		usleep(((rand() % 10) + 1) * 1000);;
	}
	
	/* Set to global variable to say that the senders theards have completed */
	if(*threadId == 1){
		g_sender1Check = 1;	
	}
	else if(*threadId == 2){
		g_sender2Check = 1;
	}
	else{
		g_sender3Check = 1;
	}
}

/*
 * Function of Output thread
 */
void* pthread_read_receiver(void *device_p){
	int fd,res;
	struct dev_msg outputMsg;
	time_t base = time(0);
	char path[100] = "/dev/";
	pthread_mutex_t receiverLock;

	/* Make the complete path */
	strcat(path,(char*)device_p);
	
	/* Take a copy of the Lock */
	if(strcmp(path,"/dev/bus_out_q1") == 0){
		receiverLock = receiver1Lock;
	}
	else if(strcmp(path,"/dev/bus_out_q2") == 0){
		receiverLock = receiver3Lock;
	}
	else{
		receiverLock = receiver3Lock;
	}

	while(1){
		/* Mutex Lock */
		pthread_mutex_lock(&receiverLock);

		/* open devices */
		fd = open(path, O_RDWR);
		if (fd < 0){
			//printf("Reading Can not open device file.\n");		
			//fprintf(stderr, "open() failed: %s\n", strerror(errno));

			/* Mutex Unlock */
			pthread_mutex_unlock(&receiverLock);

			return;
		}
		
		/* Read from the device to the buffer */
		res = read(fd, (char*)&outputMsg, sizeof(outputMsg));

		if(res != -1){
			/* Print the Message content */
			//printf("Read - MId - %d, DId - %d, Message -'%s', TimeStamp in micro sec - %llu\n", 
			//		outputMsg.messageId, outputMsg.destinationId, 
			//		outputMsg.msgString, (outputMsg.timeStamp/400));
		}
		else{
			/* Update the errno when device is empty */
			errno = 0;

			/* Check if the sender and daemon threads have ended */
			if((g_sender1Check == 1) && (g_sender2Check == 1) && (g_sender3Check == 1) 
				&& (g_daemonCheck == 1)){
				break;
			}

		}
		
		/* Close the device */		
		close(fd);
		
		/* Mutex Unlock */
		pthread_mutex_unlock(&receiverLock);

		/* Sleep for a random while */
		usleep(((rand() % 10) + 1) * 1000);;
		
	}
	
}

