/* A simple asynchronous XML-RPC client written in C, as an example of
   Xmlrpc-c asynchronous RPC facilities.  This is the same as the 
   simpler synchronous client xmlprc_sample_add_client.c, except that
   it adds 3 different pairs of numbers with the summation RPCs going on
   simultaneously.

   Use this with xmlrpc_sample_add_server.  Note that that server
   intentionally takes extra time to add 1 to anything, so you can see
   our 5+1 RPC finish after our 5+0 and 5+2 RPCs.
*/

#include <pthread.h>

#include <stdlib.h>
#include <stdio.h>

#include <xmlrpc-c/base.h>
#include <xmlrpc-c/client.h>

#include "config.h"  /* information about this build environment */

#define NAME "Xmlrpc-c Asynchronous Test Client"
#define VERSION "1.0"

#define MAJORITY 0
#define ANY 1
#define ALL 2

#define ASYNC 0
#define SYNC 1

struct thread_data {
  char** server_list;
  void (*callback) (void);
};

int value, num_responses, option, num_servers;
pthread_mutex_t result_mutex;
pthread_cond_t client_satisfied;


void* MakeRequest(void* server_name);
void* CustomClientStart(void* params);
void client_callback(void);

static void 
die_if_fault_occurred (xmlrpc_env * const envP) {
  if (envP->fault_occurred) {
    fprintf(stderr, "Something failed. %s (XML-RPC fault code %d)\n",
	    envP->fault_string, envP->fault_code);
    exit(1);
  }
}

void client_callback(void)
{
  printf("Your RPC request was answered by %d server(s) with a value of %d\n", 
	 num_responses, value);
  /* Unnecessary to unlock a mutex that is about to be destroyed.
   * This avoids any competition over the lock. */
  /* pthread_mutex_unlock(&result_mutex); */
  pthread_mutex_destroy(&result_mutex);
  pthread_cond_destroy(&client_satisfied);
}

void* CustomClientStart(void* params)  
{
  struct thread_data* input;
  char ** server_list;
  void (*callback) (void);
  
  input = (struct thread_data *)params;
  server_list = input->server_list;
  callback = input->callback;

  pthread_t threads[num_servers];
  pthread_attr_t attr;
  int rc;
  long t;
    
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

  value = 0;
  num_responses = 0;
  pthread_mutex_init(&result_mutex, NULL);
  pthread_cond_init(&client_satisfied, NULL);

  for(t = 0; t < num_servers; t++) {
    rc = pthread_create(&threads[t], &attr, 
			MakeRequest, (void *)server_list[t]);
    if(rc) {
      fprintf(stderr, "ERROR; return code from pthread_create() is %d\n", rc);
      exit(1);
    }     
  }
  pthread_attr_destroy(&attr);
  
  pthread_mutex_lock(&result_mutex);
  pthread_cond_wait(&client_satisfied, &result_mutex);
  /* run callback */
  (*callback)(); 
 
  return 0;
}

static void 
handle_sample_add_response(const char *   const server_url ATTR_UNUSED,
                           const char *   const method_name ATTR_UNUSED,
                           xmlrpc_value * const param_array,
                           void *         const user_data ATTR_UNUSED,
                           xmlrpc_env *   const faultP,
                           xmlrpc_value * const resultP) {

  xmlrpc_env env;
  xmlrpc_int addend, adder;
    
  /* Initialize our error environment variable */
  xmlrpc_env_init(&env);

  /* Our first four arguments provide helpful context.  Let's grab the
     addends from our parameter array. 
  */
  xmlrpc_decompose_value(&env, param_array, "(ii)", &addend, &adder);
  die_if_fault_occurred(&env);
            
  if (faultP->fault_occurred)
    printf("The RPC failed.  %s", faultP->fault_string);
  else {
    xmlrpc_int sum;

    xmlrpc_read_int(&env, resultP, &sum);
    die_if_fault_occurred(&env);

    pthread_mutex_lock(&result_mutex);
    num_responses++;
    value = sum;

    if(option == MAJORITY) {
      if(num_responses >= num_servers/2 + 1)
	pthread_cond_signal(&client_satisfied);
	  
    }  else if(option == ANY) { 
      //We have already received a response, so ANY is satisfied
      pthread_cond_signal(&client_satisfied);

    } else { /*All or an invalid option*/
      if(num_responses == num_servers)
	pthread_cond_signal(&client_satisfied);
    }

    pthread_mutex_unlock(&result_mutex);
  }
}

void* MakeRequest(void* server_name)
{
  char* name;
  xmlrpc_env env;
  xmlrpc_client * clientP;
  char * const methodName = "sample.add";

  name = (char*)server_name;
  
  /* Initialize our error environment variable */
  xmlrpc_env_init(&env);
   
  /* Required before any use of Xmlrpc-c client library: */
  xmlrpc_client_setup_global_const(&env);
  die_if_fault_occurred(&env);

  xmlrpc_client_create(&env, XMLRPC_CLIENT_NO_FLAGS, NAME, VERSION, NULL, 0,
		       &clientP);
  die_if_fault_occurred(&env);

  xmlrpc_client_start_rpcf(&env, clientP, name, methodName,
			   handle_sample_add_response, NULL,
			   "(ii)", (xmlrpc_int32) 5, (xmlrpc_int32) 1);
  die_if_fault_occurred(&env);

  /* Wait for all RPCs to be done.  With some transports, this is also
     what causes them to go.
  */
  xmlrpc_client_event_loop_finish(clientP);
   
  xmlrpc_client_destroy(clientP);
   
  pthread_exit(NULL);
}

int main(int argc, char** argv) 
{

  if (argc <  4) {
    fprintf(stderr, 
	    "Usage: xmlrpc_synch_client" 
	    " [One or more servers in the format http://localhost:8080/RPC2]" 
	    " [0 for MAJORITY || 1 for ANY || 2 for ALL]" 
	    " [0 for Asynchronous || 1 for Synchronous]\n");
    exit(1);
  }

  int i, sync;
  pthread_t thread_id;
  num_servers = argc - 3;
  char * server_list[num_servers];
  for(i = 1; i <= num_servers; i++) {
    server_list[i - 1] = argv[i];
  }

  option = atoi(argv[argc - 2]);
  sync = atoi(argv[argc - 1]);

  struct thread_data input;
  input.server_list = server_list;
  input.callback = &client_callback;

  if(sync == ASYNC) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
      
    pthread_create(&thread_id, &attr, CustomClientStart, 
		   (void *)&input);

    printf("The client has made a nonblocking request.\n");
    pthread_join(thread_id, NULL);
    pthread_attr_destroy(&attr);

  } else { /*SYNC is default*/
    /*blocking function*/
    CustomClientStart((void*)&input);
  } 
    
  return 0;
}
