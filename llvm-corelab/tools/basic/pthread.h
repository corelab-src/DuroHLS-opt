#include <stdint.h>
#include <stdbool.h>

typedef unsigned fpga_pthread_t;
typedef uint8_t fpga_pthread_mutex_t;

__attribute__((optnone))
int fpga_pthread_create(fpga_pthread_t *thread, void *fcn, void *arg)
{
	//Create pthread

	//a store instruction will be inserted before this call
	//the instruction will store fcnptr to thread

	//should be used in main function

	//Success -> return 0
	//Fail -> return 1
}

__attribute__((optnone))
int fpga_pthread_join(fpga_pthread_t thread)
{
	//Wait for the thread

	//Success -> return 0
	//Fail -> return 1
}

__attribute__((optnone))
int fpga_pthread_mutex_init(fpga_pthread_mutex_t *mutex)
{
	//initialize mutex value

	//should be used in main function

	//Success -> return 0
	//Fail -> return 1
}

__attribute__((optnone))
int fpga_pthread_mutex_lock(fpga_pthread_mutex_t mutex)
{
	//Try to Lock the mutex
	//Wait until get the mutex

	//Success -> return 0
	//Fail -> return 1
}

__attribute__((optnone))
int fpga_pthread_mutex_unlock(fpga_pthread_mutex_t mutex)
{
	//unlock the mutex

	//Success -> return 0
	//Fail -> return 1
}

