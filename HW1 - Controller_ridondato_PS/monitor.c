//------------------- MONITOR.C ---------------------- 

#include <time.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <mqueue.h>
#include <fcntl.h>
#include <string.h>
#include "rt-lib.h"
#include "parameters.h"

//emulates the controller

static int keep_on_running = 1;
mqd_t ps_queue;
mqd_t backup_queue;

void * monitor_loop(void * par) {

	// Messaggio da ricevere dal controller e inviare al plant
	char message [MAX_MSG_SIZE];

	/* Code */
	struct mq_attr attr;

	attr.mq_flags = 0;				
	attr.mq_maxmsg = MAX_MESSAGES;	
	attr.mq_msgsize = MAX_MSG_SIZE; 
	attr.mq_curmsgs = 0;

	// Apriamo una coda in sola lettura (O_RDONLY), se non esiste la creiamo (O_CREAT)
	// La coda conterrà il segnale di controllo da girare all'actuator
	mqd_t monitor_qd;
	if ((monitor_qd = mq_open (MONITOR_QUEUE_NAME, O_RDONLY | O_CREAT, QUEUE_PERMISSIONS, &attr)) == -1) {
		perror ("monitor loop: mq_open (actuator)");
		exit (1);
	}
	
	// Apriamo la coda actuator del plant in scrittura 
	mqd_t actuator_qd;
	if ((actuator_qd = mq_open (ACTUATOR_QUEUE_NAME, O_WRONLY|O_CREAT, QUEUE_PERMISSIONS,&attr)) == -1) {
		perror ("monitor loop: mq_open (actuator)");
		exit (1);
	}	

	// Apriamo la coda del ps in scrittura 
	if ((ps_queue  = mq_open (PS_QUEUE_NAME, O_WRONLY|O_CREAT | O_NONBLOCK, QUEUE_PERMISSIONS,&attr)) == -1) {
		perror ("monitor loop: mq_open (actuator)");
		exit (1);
	}

	
	while (keep_on_running)
	{
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		timespec_add_us(&ts, TICK_TIME*BUF_SIZE+TICK_TIME*BUF_SIZE/4);
		if (mq_timedreceive(monitor_qd, message,MAX_MSG_SIZE,NULL,&ts) == -1){
			perror ("monitor loop: mq_receive (actuator)");	
			if(mq_send(ps_queue, message, strlen(message)+1,0)==-1){
				perror ("monitor loop: Not able to send message to polling server");
			}
			break;						//DEBUG
		} else {
			//invio del controllo al driver del plant
			printf("Forwarding control %s\n",message); //DEBUG
			if (mq_send (actuator_qd, message, strlen (message) + 1, 0) == -1) {
		    	perror ("monitor loop: Not able to send message to controller");
		    	continue;
			}
		}	
	}

	//qua inseire di nuovo il while di sopra per il backup controller

	/* Clear */
    if (mq_close (actuator_qd) == -1) {
        perror ("monitor loop: mq_close actuator_qd");
        exit (1);
    }
	if (mq_close (monitor_qd) == -1) {
        perror ("monitor loop: mq_close monitor_qd");
        exit (1);
    }
	return 0;
}

//ora creiamo il thread polling server

void* ps(void* parameter){
	periodic_thread *th = (periodic_thread *) parameter;
	start_periodic_timer(th,TICK_TIME);
//mesaggio che vogliamo inviare a backup
	char message [] = "attiva il backup!";

	//Messaggio da ricevere dal monitor
	char in_buffer [MAX_MSG_SIZE];

	struct mq_attr attr;

	attr.mq_flags = 0;				
	attr.mq_maxmsg = MAX_MESSAGES;	
	attr.mq_msgsize = MAX_MSG_SIZE; 
	attr.mq_curmsgs = 0;

	//ci apriamo la coda ps_queue in lettura
	if ((ps_queue  = mq_open (PS_QUEUE_NAME, O_RDONLY|O_CREAT | O_NONBLOCK, QUEUE_PERMISSIONS,&attr)) == -1) {
		perror ("monitor loop: mq_open (ps)");
		exit (1);
	}

	//ci apriamo la coda baclup_queue in scrittura
	if ((backup_queue  = mq_open (BACKUP_QUEUE_NAME, O_WRONLY|O_CREAT , QUEUE_PERMISSIONS,&attr)) == -1) {
		perror ("monitor loop: mq_open (backup)");
		exit (1);
	}

	//ora implemento il POLLING :
	while (1)
	{
		wait_next_activation(th);
		printf("sono qui");
	    if (mq_receive(ps_queue,in_buffer,MAX_MSG_SIZE,NULL) == -1){ //se non dovessi ricevere la richiesta, cioe se non ci sono richieste nella coda ma non si blocca
		    printf("eccomi \t");
			printf ("No message ...\n");							//DEBUG
	    }
	    else{
		    printf ("Polling Server: message received: %s.\n",in_buffer);			//DEBUG
			if(mq_send(backup_queue,message,strlen (message)+1,0)==-1){
				perror ("polling server: Not able to send message to polling server");
				continue;
			}

	    }
	}

}

int main(void)
{
	printf("The monitor is STARTED! [press 'q' to stop]\n");
 	
    pthread_t monitor_thread;

	pthread_attr_t myattr;
	struct sched_param myparam;

	// MONITOR THREAD
	pthread_attr_init(&myattr);
	pthread_attr_setschedpolicy(&myattr, SCHED_FIFO);
	pthread_attr_setinheritsched(&myattr, PTHREAD_EXPLICIT_SCHED); 

	myparam.sched_priority = 51;
	pthread_attr_setschedparam(&myattr, &myparam); 
	pthread_create(&monitor_thread,&myattr,monitor_loop,NULL);

	pthread_attr_destroy(&myattr);

	periodic_thread th_ps;
	pthread_t thread_ps;

	th_ps.period = TICK_TIME*BUF_SIZE;
	th_ps.priority = 52;
	myparam.sched_priority = th_ps.priority;

	pthread_attr_setschedparam(&myattr, &myparam); 	
	pthread_create(&thread_ps, &myattr, ps, (void*)&th_ps);

	pthread_attr_destroy(&myattr);
	
	
	/* Wait user exit commands*/
	while (1) {
   		if (getchar() == 'q') break;
  	}
	keep_on_running = 0;

	if (mq_unlink (MONITOR_QUEUE_NAME) == -1) {
        perror ("Main: mq_unlink monitor queue");
        exit (1);
    }

 	printf("The monitor is STOPPED\n");
	return 0;
}




