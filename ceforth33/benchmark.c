#include <stddef.h>
#include <stdlib.h>
#include <math.h>
#include <errno.h>
#include "nolib.h"

#define TYPE      typedef
#define RECORD    struct
#define BEGIN     {
#define END       ;}
#define PROCEDURE void
#define POINTER   *
#define RETURN    return

#define CARDINAL  unsigned
#define REAL      float
#define CHAR      char
#define CASE      switch(
#define OF        ){case
#define ESAC      ;break; case
#define REPEAT    do{
#define UNTIL(cond)     ;} while(!(cond))
#define WHILE     while(
#define DO        ){
#define FOR       for(
#define TO        <=
#define NIL       NULL
#define ASSIGN(x,y) memcpy(x,y,sizeof(y))
#define NEW(p,t)  p = (t*)malloc(sizeof(t))
#define DIV       /
#define ln        log

  /* ETH-40 The Personal Computer Lilith 1981
    a: empty REPEAT loop
    b: empty WHILE loop
    c: empty FOR loop
    d: CARDINAL arithmetic
    e: REAL arithmetic
    f: standard functions
    g: array of single dimension
    h: same as q but with index tests
    i: matrix access
    j: same as i but with index tests
    k: call of empty, parameterless procedure
    l: call of empty procedure with 4 parameters
    m: copying arrays (block moves)
    n: pointer chaining */

  TYPE RECORD __Node {
    CARDINAL x, y;
    RECORD __Node POINTER next;
  } Node;
  TYPE Node POINTER NodePtr;

  CARDINAL A[256], B[256], C[256];
  CARDINAL M[100][100];
  CARDINAL m;
  NodePtr head;

  PROCEDURE P()
  BEGIN
  END 

  PROCEDURE Q(CARDINAL x,CARDINAL y,CARDINAL z,CARDINAL w)
  BEGIN
  END


  PROCEDURE Test(CHAR ch)
  BEGIN
    CARDINAL i, j, k;
    REAL r0, r1, r2;
    NodePtr p;

    CASE ch OF
      'a': k = 20000;
           REPEAT
             k = k-1
           UNTIL (k == 0) ESAC
      'b': i = 20000;
           WHILE i > 0 DO
             i = i-1
           END ESAC
      'c': FOR i = 1; i <= 20000; i++ DO
           END ESAC
      'd': j = 0; k = 10000;
           REPEAT
             k = k-1; j = j+1; i = (k*3) DIV (j*5)
           UNTIL (k == 0) ESAC
      'e': k = 5000; r1 = 7.28; r2 = 34.8;
           REPEAT
             k = k-1; r0 = (r1*r2) / (r1+r2)
           UNTIL (k == 0) ESAC
      'f': k = 500;
           REPEAT r0 = sin(0.7); r1 = exp(2.0);
                  r0 = ln(10.0); r1 = sqrt(18.0); k = k-1
           UNTIL (k == 0) ESAC
      'g': k = 20000; i = 0; B[0] = 73;
           REPEAT
             A[i] = B[i]; B[i] = A[i]; k = k-1
           UNTIL (k == 0) ESAC
      'h': k = 20000; i = 0; B[0] = 73;
           REPEAT
             A[i] = B[i]; B[i] = A[i]; k = k-1
           UNTIL (k == 0) ESAC
      'i': FOR i = 0; i TO 99; i++ DO
             FOR j = 0; j TO 99; j++ DO
               M[i][j] = M[j][i]
             END
           END ESAC
      'j': FOR i = 0; i TO 99; i++ DO
             FOR j = 0; j TO 99; j++ DO
               M[i][j] = M[j][i]
             END
           END ESAC
      'k': k = 20000;
           REPEAT
             P(); k = k-1
           UNTIL (k == 0) ESAC
      'l': k = 20000;
           REPEAT
             Q(i,j,k,m); k = k-1
           UNTIL (k == 0) ESAC
      'm': k = 500;
           REPEAT
             k = k-1; ASSIGN(A, B); ASSIGN(B, C); ASSIGN(C, A)
           UNTIL (k == 0) ESAC
      'n': k = 500;
           REPEAT p = head;
             REPEAT p = p->next UNTIL (p == NIL);
             k = k-1
           UNTIL (k == 0)
    END
  END

int notmain()
BEGIN
  CARDINAL n;
  NodePtr q;

  errno = 0;
  puts("Lilith benchmark start...");
  head = NIL; n = 100;
  REPEAT q = head;
    NEW(head,Node); head->next = q; n = n-1
  UNTIL (n == 0);
  Test('a');
  Test('b');
  Test('c');
  Test('d');
  Test('e');
  Test('f');
  Test('g');
  Test('h');
  Test('i');
  Test('j');
  Test('k');
  Test('l');
  Test('m');
  Test('n');
  puts("done\n");
  RETURN 0
END
/* vim:set ts=2 sw=2 et: */
