
: recurse last @ name> , ; immediate
: fib ( n1 -- n2 ) dup 2 < if drop 1 exit then 1-   dup recurse swap 1-  recurse + 1+ ;
decimal
cr 25 fib .
bye
