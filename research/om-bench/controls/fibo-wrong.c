#include <stdio.h>
int main(void){unsigned long long n=0,a=0,b=1,t;if(scanf("%llu",&n)!=1)return 1;for(unsigned long long i=1;i<n;i++){t=a+b;a=b;b=t;}printf("%llu\n",a);return 0;}
