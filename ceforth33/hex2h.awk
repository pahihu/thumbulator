BEGIN \
{
}

(3 == NF) \
{
   for (i = 0; i < length($2); i += 2)
      printf "%s,",sprintf("%d","0x" substr($2,i+1,2));
   printf "\n";
}
