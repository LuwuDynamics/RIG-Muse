"""Board-independent facade dispatches to a different backend without Puppy code."""
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class RigBoardTest(unittest.TestCase):
    def test_backend_owns_all_device_operations(self):
        source=r'''
#include "rig_board.h"
#include "rig_robot.h"
#include <assert.h>
#include <string.h>
static unsigned initialized,stopped,registered;static bool connected;
static void init(void){++initialized;}
static void stop(void){++stopped;}
static void connection(bool v){connected=v;}
static void schema(cJSON *v){assert(v==(cJSON *)1);++registered;}
static cJSON *command(const char *name,const cJSON *v){assert(!strcmp(name,"arm.pose") && v==(cJSON *)2);return (cJSON *)3;}
const rig_board_ops_t rig_selected_board={"test-arm","Test Arm",init,connection,stop,command,schema};
int main(void){rig_robot_init();rig_robot_set_connected(true);rig_robot_stop();rig_robot_register_commands((cJSON *)1);
 assert(initialized==1 && stopped==1 && registered==1 && connected);
 assert(rig_robot_command("arm.pose",(cJSON *)2)==(cJSON *)3);
 rig_robot_set_connected(false);assert(!connected);}
'''
        with tempfile.TemporaryDirectory() as folder:
            p=Path(folder);(p/'test.c').write_text(source)
            subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'main'),str(p/'test.c'),str(ROOT/'main/rig_robot.c'),'-o',str(p/'test')],check=True)
            subprocess.run([str(p/'test')],check=True)
