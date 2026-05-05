#include <stdio.h>
int main() {
	char name[50] = "Nobody";
	FILE* file = fopen("name", "r");
	printf("What is your name?\n");
	fprintf(stdout, "Reading from file...\n");
	fscanf(file, "%s", name);
	printf("My name is %s\n", name);
	fprintf(stderr, "I am alive!!!\n");
	fclose(file);
	return 0;
}
