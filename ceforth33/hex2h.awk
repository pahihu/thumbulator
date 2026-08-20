BEGIN \
{
}

(3 == NF) \
{
   if (length($2) == 32) {
      printf "/* %s */ ",$1;
      for (i = 0; i < 32; i += 2) {
         h = "0x" substr($2,i+1,2);
         printf "%s,",h;
      }
      printf "\n";
   }
}
