#include "csapp.h"

int main()
{
	int *p = calloc(2, sizeof(int));
	p[0] = 2;

	for(int i = 0; i < 2; i++)
	{
		printf("%d\n", p[i]);
	}
	int *newp = calloc(4, sizeof(int));
	newp[0] = p[0];
	newp[1] = p[1];

	int *tmp = p;
	p = newp;

	//int *fp = p;
	free(tmp);
	for(int i = 0; i < 4; i++)
	{
		printf("%d\n", p[i]);
	}
	printf("%d\n", (int)sizeof(pthread_t));
	return 0;
}