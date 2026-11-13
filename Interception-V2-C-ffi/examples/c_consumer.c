#include "InterceptionV2JsonSDK.h"
#include <string.h>
int main(void){uint64_t session=0;if(interception_v2_json_open(&session))return 1;char *reply=interception_v2_json_call(session,"{",1);if(!reply){interception_v2_json_close(session);return 2;}int failed=strstr(reply,"\"ok\":false")==0;interception_v2_json_free(reply);if(interception_v2_json_close(session))return 3;return failed;}
