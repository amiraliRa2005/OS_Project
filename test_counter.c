#include "user.h"

int main()
{
  int first = sysclcnt();
  int second = sysclcnt();
  int third = sysclcnt();
  printf("First sysclcnt: %d\n", first);
  printf("Second sysclcnt: %d\n", second);
  printf("Third sysclcnt: %d\n", third);
  
  exit(0);
}