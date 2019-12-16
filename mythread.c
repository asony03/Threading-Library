#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <queue>
#include <list>
#include <iterator> 
#include <string>

#include "mythread.h"

#define stack_size 8*1024

ucontext_t main_cntxt;
int num_threads = 0;
int num_semaphores = 0;

typedef struct my_thread {
	int tid;
	std::list<struct my_thread *> * children;
	ucontext_t * cntxt;
	struct my_thread *parent;
	int join;
	int join_all;	
} my_thread;

typedef struct my_semaphore {
	int sid;
	int val;
	std::queue<my_thread *> * queue;
} my_semaphore;

std::queue<my_thread *> ready_queue;
std::list<my_thread *> blocked_list;
my_thread * running_thread;

void print_error(std::string message) {
	perror(message.c_str());
	exit(1);
}

void MyThreadInit(void (*start_funct)(void *), void *args) {

	my_thread *main_thread = (my_thread *) malloc(sizeof(my_thread));

	ucontext_t *cntxt = (ucontext_t *) malloc(sizeof(ucontext_t));

	if (getcontext(cntxt) == -1) {
		print_error("Error getting context");
	}

	cntxt->uc_link = 0;
	cntxt->uc_stack.ss_size = stack_size;
	cntxt->uc_stack.ss_sp = malloc(stack_size);	
	cntxt->uc_stack.ss_flags = 0;

	makecontext(cntxt, (void(*)(void)) start_funct, 1, args);

	main_thread->tid = num_threads++;
	main_thread->join = 0;
	main_thread->join_all = 0;
	main_thread->parent = NULL;
	main_thread->cntxt = cntxt;
	main_thread->children = new std::list<my_thread *>;

	running_thread = main_thread;

	if (swapcontext(&main_cntxt, main_thread->cntxt) == -1)
		print_error("Error switching contexts");
}

MyThread MyThreadCreate(void (*start_funct)(void *), void *args) {

	my_thread *new_thread = (my_thread *) malloc(sizeof(my_thread));
	ucontext_t *cntxt = (ucontext_t *) malloc(sizeof(ucontext_t));

	if(getcontext(cntxt) == -1)
		print_error("Error getting context");

	cntxt->uc_link = 0;
	cntxt->uc_stack.ss_size = stack_size;
	cntxt->uc_stack.ss_sp = malloc(stack_size);	
	cntxt->uc_stack.ss_flags = 0;

	makecontext(cntxt, (void(*)(void)) start_funct, 1, args);

	new_thread->tid = num_threads++;
	new_thread->join = 0;
	new_thread->join_all = 0;
	new_thread->parent = running_thread;
	new_thread->cntxt = cntxt;
	new_thread->children = new std::list<my_thread *>;

	// Push the new thread into the ready_queue and the parent's children queue.
	running_thread->children->push_back(new_thread);
	ready_queue.push(new_thread);

	return (MyThread) new_thread;
}

void MyThreadYield() {

	my_thread *next_thread;
	my_thread *previous_thread;
	previous_thread = running_thread;

	if(!ready_queue.empty()) {

		// Invoking thread goes from running to ready queue
		ready_queue.push(running_thread);

		next_thread = ready_queue.front();
		ready_queue.pop();

		running_thread = next_thread;

		if(swapcontext(previous_thread->cntxt, next_thread->cntxt) == -1)
			print_error("Error swapping contexts");
	}
}

int MyThreadJoin(MyThread thread) {

	my_thread *child_thread;
	my_thread *next_thread;
	my_thread *previous_thread;

	child_thread = (my_thread *) thread;
	previous_thread = running_thread;

	if (child_thread->parent == running_thread) {
		// Update join flag of child thread
		child_thread->join = 1;
		// Block the parent thread
		blocked_list.push_back(running_thread);

		// Front of ready queue becomes running
		next_thread = ready_queue.front();
		ready_queue.pop();
		running_thread = next_thread;

		if(swapcontext(previous_thread->cntxt, next_thread->cntxt) == -1)
			print_error("Error swapping contexts");
	} else {
		return -1;
	}
	return 0;
}

void MyThreadJoinAll() {

	my_thread *next_thread;
	my_thread *previous_thread;
	previous_thread = running_thread;

	if (!running_thread->children->empty()) {
		// Update joined_all flag of parent
		running_thread->join_all = 1;

		// Block the running thread
		blocked_list.push_back(running_thread);

		// Front of ready queue becomes running
		next_thread = ready_queue.front();
		ready_queue.pop();
		running_thread = next_thread;

		if(swapcontext(previous_thread->cntxt, next_thread->cntxt) == -1)
			print_error("Error swapping contexts");
	}
}

void MyThreadExit() {

	my_thread * current_thread;
	my_thread * next_thread;

	current_thread = running_thread;

	if (current_thread->parent != NULL && current_thread->join == 1) {
		// Checking join condition
		blocked_list.remove(current_thread->parent);
		ready_queue.push(current_thread->parent);
	} else if (current_thread->parent != NULL && current_thread->parent->join_all == 1) {
		// Checking joinAll
		if ((current_thread->parent->children->front() == current_thread)
				&& (current_thread->parent->children->back() == current_thread)) {
			// Thread is the last child of the parent
			blocked_list.remove(current_thread->parent);
			ready_queue.push(current_thread->parent);
		}
	}

	// Delete the thread and free memory

	if (current_thread->parent != NULL) {
		// Check if parent exists
		current_thread->parent->children->remove(current_thread);
		current_thread->parent = NULL;
	}

	std::list <my_thread *> :: iterator itr; 
    for(itr = current_thread->children->begin(); itr != current_thread->children->end(); ++itr) {
    	// Make parent pointers of the children null
    	(*itr)->parent = NULL;
    }

    delete(current_thread->children);
	free(current_thread->cntxt);
	free(current_thread);

	//Get the next thread from the ready queue.
	if (!ready_queue.empty()) {
		next_thread = ready_queue.front();
		ready_queue.pop();
		running_thread = next_thread;
		if(setcontext(next_thread->cntxt) == -1) {
			print_error("Error setting context");
		}
	} else {
		if(setcontext(&main_cntxt) == -1) {
			print_error("Error setting context");
		}
	}
}

MySemaphore MySemaphoreInit(int initialValue) {

	my_semaphore * sem = NULL;

	if (initialValue >= 0) {
		sem = (my_semaphore *) malloc(sizeof(my_semaphore));
		sem->sid = num_semaphores++;
		sem->val = initialValue;		
		sem->queue = new std::queue<my_thread *>;
	} 
	return (MySemaphore) sem;
}

void MySemaphoreSignal(MySemaphore sem) {

	my_semaphore * semaphore = (my_semaphore *) sem;
	semaphore->val++;

	if(!semaphore->queue->empty()) {
		ready_queue.push(semaphore->queue->front());
		semaphore->queue->pop();
	}
}

void MySemaphoreWait(MySemaphore sem) {

	my_semaphore * semaphore = (my_semaphore *) sem;	
	semaphore->val--;

	if(semaphore->val < 0) {
		my_thread * current_thread = running_thread;
		semaphore->queue->push(current_thread);
		running_thread = ready_queue.front();
		ready_queue.pop();
		if(swapcontext(current_thread->cntxt, running_thread->cntxt) == -1)
			print_error("Error swapping contexts");
	}
}

int MySemaphoreDestroy(MySemaphore sem) {

	my_semaphore * semaphore = (my_semaphore *) sem;

	if(!semaphore->queue->empty())
		return -1;

	delete(semaphore->queue);
	free(semaphore);
	return 0;
}