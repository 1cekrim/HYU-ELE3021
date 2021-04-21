#ifndef __STACKTRACE_H__
#define __STACKTRACE_H__

#define TRACEDEPTH 100000

typedef unsigned int uint;
struct stelement
{
  const char* filename;
  const char* signature;
  uint line;
};

struct tracest
{
  struct stelement data[TRACEDEPTH];
  uint size;
};

void raisepanic(struct tracest* st, char* message);
struct tracest* pushtracest(char* filename, const char* signature, uint line);
void poptracest(struct tracest** st);

#define STACKTRACE()                                                           \
  __attribute__((__cleanup__(poptracest))) struct tracest* _stacktraceraii =   \
      pushtracest(__FILE__, __func__, __LINE__)

#define RAISEPANIC(msg) raisepanic(_stacktraceraii, msg)

#endif