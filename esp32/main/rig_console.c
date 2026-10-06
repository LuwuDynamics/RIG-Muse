// USB UART bench access to the same bounded public commands. No pairing,
// networking or arbitrary motor-write bypass. Idle input never starts a show.
#include "rig_robot.h"
#include "rig_setup.h"
#include "esp_system.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void console_worker(void *unused) {
    (void)unused;
    char line[512];size_t used=0;bool overflow=false;
    for(;;) {
        int c=getchar();
        if(c==EOF){clearerr(stdin);vTaskDelay(pdMS_TO_TICKS(20));continue;}
        if(c=='\r')continue;
        if(c=='\n') {
            line[used]=0;
            if(!overflow && !strcmp(line,">setup.restart")) {
                printf("\n@setup {\"ok\":true,\"restarting\":true}\n"); fflush(stdout);
                rig_robot_stop();
                vTaskDelay(pdMS_TO_TICKS(100)); esp_restart();
            } else if(!overflow && (!strncmp(line,">rig.",5) || !strncmp(line,">setup.",7))) {
                bool setup=!strncmp(line,">setup.",7);
                char *argument=strchr(line,' ');if(argument)*argument++=0;
                cJSON *params=argument && argument[0]=='{'?cJSON_Parse(argument):cJSON_CreateObject();
                if(!params || !cJSON_IsObject(params)){cJSON_Delete(params);used=0;overflow=false;continue;}
                if(argument && argument[0]!='{')cJSON_AddStringToObject(params,"name",argument);
                cJSON *result=setup?rig_setup_command(line+1,params):rig_robot_command(line+1,params);
                if(result){char *json=cJSON_PrintUnformatted(result);if(json){printf("\n@%s %s\n",setup?"setup":"rig",json);free(json);}cJSON_Delete(result);}
                cJSON_Delete(params);
            }
            memset(line,0,sizeof(line));
            used=0;overflow=false;
        } else if(used<sizeof(line)-1)line[used++]=(char)c;
        else overflow=true;
    }
}
void rig_console_init(void) {
    xTaskCreate(console_worker,"rig_console",8192,NULL,1,NULL);
}
