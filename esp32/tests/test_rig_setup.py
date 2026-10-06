"""Local device provisioning: token privacy, persistence, direct/proxy and failures."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
JSON = Path(os.environ.get("CJSON_SOURCE_DIR", ROOT / "managed_components/espressif__cjson/cJSON"))


class RigSetupTest(unittest.TestCase):
    def test_provisioning_and_boot_snapshot(self):
        driver = r'''
#include "rig_setup.h"
#include "config_store.h"
#include "cJSON.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char token[256],proxy[256];static bool fail;
static char *slot(const char*k){if(!strcmp(k,"rig_sdk_token"))return token;assert(!strcmp(k,"rig_proxy"));return proxy;}
bool config_get_str(const char*k,char*out,size_t cap){char*s=slot(k);if(fail||!s[0]||strlen(s)>=cap)return false;strcpy(out,s);return true;}
bool config_set_str(const char*k,const char*v){if(fail)return false;assert(strlen(v)<256);strcpy(slot(k),v);return true;}
bool config_erase_key(const char*k){if(fail)return false;slot(k)[0]=0;return true;}
config_key_lookup_t config_key_lookup(const char*k){return fail?CONFIG_KEY_LOOKUP_ERROR:slot(k)[0]?CONFIG_KEY_FOUND:CONFIG_KEY_NOT_FOUND;}
static cJSON *call(const char*c,const char*p,bool ok){cJSON*params=cJSON_Parse(p);cJSON*r=rig_setup_command(c,params);cJSON_Delete(params);assert(r);assert(cJSON_IsTrue(cJSON_GetObjectItem(r,"ok"))==ok);return r;}
static void discard(const char*c,const char*p,bool ok){cJSON_Delete(call(c,p,ok));}
int main(void){
 char host[128];int port;
 rig_setup_init();assert(!rig_setup_sdk_token());assert(rig_setup_proxy(host,sizeof(host),&port)==0);
 discard("setup.sdk_token","{\"token\":\"bad\"}",false);assert(!token[0]);
 // Canonical all-zero base64url fixture; never an issued credential.
 const char *fixture="mgst_" "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
 char params[100];snprintf(params,sizeof(params),"{\"token\":\"%s\"}",fixture);
 cJSON*r=call("setup.sdk_token",params,true);char*printed=cJSON_PrintUnformatted(r);assert(!strstr(printed,fixture));free(printed);cJSON_Delete(r);
 assert(!rig_setup_sdk_token());rig_setup_init();assert(!strcmp(rig_setup_sdk_token(),fixture));
 r=call("setup.status","{}",true);printed=cJSON_PrintUnformatted(r);assert(!strstr(printed,"mgst_"));assert(strstr(printed,"sdk_token_configured"));free(printed);cJSON_Delete(r);
 discard("setup.proxy","{\"host\":\"192.0.2.10\",\"port\":7897}",true);
 assert(rig_setup_proxy(host,sizeof(host),&port)==0);rig_setup_init();assert(rig_setup_proxy(host,sizeof(host),&port)==1 && !strcmp(host,"192.0.2.10") && port==7897);
 assert(rig_setup_proxy(host,2,&port)==-1);
 const char*bad[]={"{}","{\"host\":\"x\",\"port\":0}","{\"host\":\"x\",\"port\":65536}","{\"host\":\"x\",\"port\":1.5}","{\"host\":\"x@y\",\"port\":80}","{\"host\":\"x\\r\\nHeader\",\"port\":80}","{\"host\":\"\",\"port\":80}"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)discard("setup.proxy",bad[i],false);
 discard("setup.proxy","{\"enabled\":false}",true);rig_setup_init();assert(rig_setup_proxy(host,sizeof(host),&port)==0);
 strcpy(proxy,"corrupted record");rig_setup_init();assert(rig_setup_proxy(host,sizeof(host),&port)==-1);
 fail=true;rig_setup_init();assert(!rig_setup_sdk_token());assert(rig_setup_proxy(host,sizeof(host),&port)==-1);
 discard("setup.sdk_token",params,false);discard("setup.clear","{}",false);
 fail=false;discard("setup.clear","{}",true);rig_setup_init();assert(!rig_setup_sdk_token());assert(rig_setup_proxy(host,sizeof(host),&port)==0);
 discard("setup.unknown","{}",false);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)
            (p / "sdkconfig.h").write_text('#define CONFIG_RIG_HTTP_PROXY 1\n#define CONFIG_RIG_HTTP_PROXY_HOST ""\n#define CONFIG_RIG_HTTP_PROXY_PORT 7897\n#define CONFIG_GADGET_SDK_TOKEN ""\n')
            (p / "test.c").write_text(driver)
            cc = shlex.split(os.environ.get("CC", "cc"))
            subprocess.run([*cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined",
                            "-I", str(p), "-I", str(ROOT / "main"), "-I", str(JSON),
                            str(p / "test.c"), str(ROOT / "main/rig_setup.c"), str(JSON / "cJSON.c"),
                            "-o", str(p / "test")], check=True)
            subprocess.run([str(p / "test")], check=True)
