#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
		
	int rc = fork();
	
	if (rc < 0) {
		printf(1, "fork failed\n");
		exit();
	} else if (rc == 0) {
		while (1) {
			printf(1, "Child\n");
			yield();
		}
		exit();
	} else {
		while (1) {
			printf(1, "Parent\n");
			yield();
		}
		wait();
	}

	exit();	
}
