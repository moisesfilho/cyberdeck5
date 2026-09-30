#include "platform/input/cyberdeck_keyboard_dispatch.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
struct capture { int calls{}; std::string text; std::uint8_t modifier{}; std::uint32_t key{}; };
static void receive(const char *text,std::size_t n,std::uint8_t m,std::uint32_t k,void *p){auto&c=*static_cast<capture*>(p);++c.calls;c.text.assign(text,n);c.modifier=m;c.key=k;}
int main(){capture c; cyberdeck_keyboard_dispatch::dispatcher d; assert(!d.start(nullptr,&c)); assert(d.start(receive,&c)); d.submit(nullptr,0,0,17); assert(c.calls==1&&c.key==17); d.submit("abc",3,4,0); assert(c.text=="abc"&&c.modifier==4); d.submit(nullptr,0,0,0); d.stop(); d.submit("ignored",7,0,1); assert(c.calls==2); std::cout<<"keyboard dispatch tests passed\n";}
