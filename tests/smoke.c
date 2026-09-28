#include <stdio.h>

int
main(void)
{
   if (puts("cwiki verification harness") == EOF) {
      return 1;
   }

   return 0;
}
