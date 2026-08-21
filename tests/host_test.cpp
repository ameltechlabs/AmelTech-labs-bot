#include <assert.h>
#include <math.h>
#include "AmelTechBot.h"
using namespace ameltech;
int main(){
  KnowledgeBase kb;
  assert(kb.addQA("What is an apple?","An apple is a fruit.","general")==Status::OK);
  assert(kb.addQA("what is an apple","another answer","general")!=Status::OK);
  assert(kb.count()==1);
  Calculator c; double x=0; assert(c.evaluate("25 * 4",x)&&fabs(x-100)<1e-9); assert(c.evaluate("(12+8)/2",x)&&fabs(x-10)<1e-9); assert(!c.evaluate("10/0",x));
  AmelTechBot bot; bot.begin(); bot.train("how many seconds are there in one minute","There are 60 seconds in one minute.","general"); auto r=bot.ask("how many sec r there in 1 min"); assert(r.status==Status::OK || r.status==Status::LOW_CONFIDENCE);
  auto h=bot.health(); (void)h;
  assert(bot.diagnostics().refreshFast()==Status::UNAVAILABLE);
  return 0;
}
