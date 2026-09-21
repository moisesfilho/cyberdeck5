#include "gai_strerror.h"
#include "lwip/netdb.h"

const char *gai_strerror(int errcode)
{
    switch (errcode) {
    case EAI_BADFLAGS: return "EAI_BADFLAGS";
    case EAI_FAIL: return "EAI_FAIL";
    case EAI_FAMILY: return "EAI_FAMILY";
    case EAI_MEMORY: return "EAI_MEMORY";
    case EAI_NONAME: return "EAI_NONAME";
    case EAI_SERVICE: return "EAI_SERVICE";
#ifdef EAI_AGAIN
    case EAI_AGAIN: return "EAI_AGAIN";
#endif
    default: return "Unknown error";
    }
}
