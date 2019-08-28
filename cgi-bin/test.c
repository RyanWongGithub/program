#include <stdio.h>
#include <string.h>

int main()
{
	char a1[20] = "haha\0heihei";
	char a2[30] = "xxx";
	strcat(a2, a1);
	printf("%s", a2);
	return 0;
}
