"""Actual CONNECT parser/socket code, plus ESP-TLS handoff and failure tests."""
import os
from pathlib import Path
import shlex
import socket
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ProxyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.path = Path(cls.tmp.name)
        driver = cls.path / 'driver.c'
        driver.write_text(r'''
#include "proxy_connect.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
int main(int argc, char **argv) {
    (void)argc;
    int fd=atoi(argv[1]), before=fcntl(fd,F_GETFL), status=-1;
    struct timespec start,end;clock_gettime(CLOCK_MONOTONIC,&start);
    int rc=proxy_http_connect(fd,argv[2],strlen(argv[2]),443,atoi(argv[3]),&status);
    clock_gettime(CLOCK_MONOTONIC,&end);
    int error=errno, after=fcntl(fd,F_GETFL);
    char tail[5]={0};
    if(rc==0) recv(fd,tail,4,0);
    printf("%d %d %d %d %ld %s\n",rc,status,error,before==after,
           (end.tv_sec-start.tv_sec)*1000+(end.tv_nsec-start.tv_nsec)/1000000,tail);
    return 0;
}
''')
        cls.binary = cls.path / 'connect_test'
        cls.cc = shlex.split(os.environ.get('CC', 'cc'))
        subprocess.run([*cls.cc, '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-I', str(ROOT/'main'),
                        str(driver), str(ROOT/'main/proxy_connect.c'), '-o', str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def exchange(self, response, host='api.muse.ai', timeout=500, dribble=False):
        a,b = socket.socketpair()
        received=[]
        def server():
            try:
                b.settimeout(2)
                data=b''
                while not data.endswith(b'\r\n\r\n'):
                    chunk=b.recv(1024)
                    if not chunk: return
                    data+=chunk
                received.append(data)
                if dribble:
                    for byte in response:
                        b.sendall(bytes([byte]));time.sleep(.025)
                else:
                    b.sendall(response)
            except (OSError, TimeoutError):
                pass
            finally:
                b.close()
        thread=threading.Thread(target=server);thread.start()
        try:
            p=subprocess.run([str(self.binary), str(a.fileno()),host,str(timeout)],pass_fds=(a.fileno(),),
                             capture_output=True,text=True,timeout=4)
            self.assertEqual(p.returncode,0,p.stderr)
            parts=p.stdout.strip().split()
            self.assertEqual(parts[3],'1','socket flags not restored')
            self.elapsed_ms=int(parts[4])
            return int(parts[0]),int(parts[1]),parts[5:] ,received
        finally:
            a.close();thread.join(timeout=3)
            self.assertFalse(thread.is_alive())

    def test_response_and_tunnel_boundaries(self):
        for response in (b'HTTP/1.1 200 Connection established\r\n\r\nTLSX',
                         b'HTTP/1.0 200 OK\r\nProxy-Agent: x\r\n\r\nTLSX'):
            rc,status,tail,req=self.exchange(response)
            self.assertEqual((rc,status,tail),(0,200,['TLSX']))
            self.assertEqual(req,[b'CONNECT api.muse.ai:443 HTTP/1.1\r\nHost: api.muse.ai:443\r\n\r\n'])
        rc,status,tail,req=self.exchange(b'HTTP/1.1 200 OK\r\n\r\nTLSX',host='2001:db8::1')
        self.assertEqual(rc,0)
        self.assertIn(b'CONNECT [2001:db8::1]:443 ',req[0])
        for response,status in ((b'HTTP/1.1 407 Authentication required\r\n\r\n',407),
                                (b'HTTP/1.1 502 Bad Gateway\r\n\r\n',502),
                                (b'HTTP/1.1 2000 Invalid\r\n\r\n',0),
                                (b'HTTP/9.9 200 OK\r\n\r\n',0),
                                (b'HTTP/1.1 2x0 OK\r\n\r\n',0),
                                (b'HTTP/1.1 200 OK\x00\r\n\r\n',0),
                                (b'HTTP/1.1 200 OK\r\nX:'+b'x'*4096+b'\r\n\r\n',0),
                                (b'HTTP/1.1 200',0)):
            with self.subTest(response=response[:25]):
                rc,actual,_,_=self.exchange(response)
                self.assertEqual((rc,actual),(-1,status))

    def test_deadlines_and_header_injection(self):
        rc,status,_,_=self.exchange(b'HTTP/1.1 200 OK\r\n\r\nTLSX',timeout=100,dribble=True)
        self.assertEqual((rc,status),(-1,0));self.assertLess(self.elapsed_ms,500)
        for host in ('bad\r\nInjected: true','host/path','host@evil','', 'x'*254):
            rc,status,_,req=self.exchange(b'',host=host)
            self.assertEqual((rc,status,req),(-1,0,[]))

    def test_tls_ownership_and_original_hostname(self):
        (self.path/'sdkconfig.h').write_text('')
        (self.path/'esp_log.h').write_text('#define ESP_LOGI(tag,...) ((void)(tag))\n#define ESP_LOGW(tag,...) ((void)(tag))\n')
        (self.path/'esp_tls.h').write_text('''#include <stdbool.h>
#define ESP_OK 0
typedef enum {ESP_TLS_INIT,ESP_TLS_CONNECTING,ESP_TLS_DONE} esp_tls_conn_state_t;
typedef struct {bool non_block,is_plain_tcp;int timeout_ms;void *crt_bundle_attach;} esp_tls_cfg_t;
typedef struct {esp_tls_conn_state_t state;int fd;} esp_tls_t;
int esp_tls_get_conn_state(esp_tls_t*,esp_tls_conn_state_t*);
int esp_tls_set_conn_state(esp_tls_t*,esp_tls_conn_state_t);
int esp_tls_get_conn_sockfd(esp_tls_t*,int*);
''')
        driver=self.path/'tls_driver.c'
        driver.write_text(r'''
#include "esp_tls.h"
#include <assert.h>
#include <string.h>
#include <stddef.h>
int calls,mode,connect_calls;
int rig_setup_proxy(char*h,size_t cap,int*p){
    assert(cap>=11);if(mode==5)return -1;if(mode==4)return 0;
    strcpy(h,"192.0.2.10");*p=7897;return 1;
}
int __wrap_esp_tls_conn_new_sync(const char*,int,int,const esp_tls_cfg_t*,esp_tls_t*);
int esp_tls_get_conn_state(esp_tls_t*t,esp_tls_conn_state_t*s){*s=t->state;return 0;}
int esp_tls_set_conn_state(esp_tls_t*t,esp_tls_conn_state_t s){t->state=s;return 0;}
int esp_tls_get_conn_sockfd(esp_tls_t*t,int*f){*f=t->fd;return 0;}
int __real_esp_tls_conn_new_sync(const char*h,int n,int p,const esp_tls_cfg_t*c,esp_tls_t*t){
    calls++;
    if(mode==4){assert(calls==1 && n==11 && !memcmp(h,"api.muse.ai",11));assert(p==443 && !c->is_plain_tcp);return 1;}
    if(calls==1){assert(n==10 && !memcmp(h,"192.0.2.10",10));assert(p==7897 && c->is_plain_tcp);t->fd=42;return mode==1?-1:1;}
    assert(calls==2);assert(n==11 && !memcmp(h,"api.muse.ai",11));assert(p==443 && !c->is_plain_tcp);
    assert(c->crt_bundle_attach==(void*)123 && t->fd==42 && t->state==ESP_TLS_CONNECTING);
    return mode==3?-1:1;
}
int proxy_http_connect(int fd,const char*h,size_t n,int p,int timeout,int*s){
    connect_calls++;assert(fd==42 && n==11 && !memcmp(h,"api.muse.ai",11) && p==443 && timeout==15000);
    *s=mode==2?407:200;return mode==2?-1:0;
}
int main(void){
    for(mode=0;mode<4;mode++){
        calls=connect_calls=0;esp_tls_t t={ESP_TLS_INIT,-1};esp_tls_cfg_t c={.timeout_ms=15000,.crt_bundle_attach=(void*)123};
        int rc=__wrap_esp_tls_conn_new_sync("api.muse.ai",11,443,&c,&t);
        assert(rc==(mode==0?1:-1));assert(c.is_plain_tcp==false);
        assert(calls==((mode==1||mode==2)?1:2));assert(connect_calls==(mode==1?0:1));
    }
    mode=0;calls=0;esp_tls_t t={ESP_TLS_INIT,-1};esp_tls_cfg_t c={.non_block=true};
    assert(__wrap_esp_tls_conn_new_sync("api.muse.ai",11,443,&c,&t)==-1 && calls==0);
    c.non_block=false;c.is_plain_tcp=true;
    assert(__wrap_esp_tls_conn_new_sync("api.muse.ai",11,443,&c,&t)==-1 && calls==0);
    mode=4;calls=connect_calls=0;c.is_plain_tcp=false;
    assert(__wrap_esp_tls_conn_new_sync("api.muse.ai",11,443,&c,&t)==1 && calls==1 && connect_calls==0);
    mode=5;calls=0;
    assert(__wrap_esp_tls_conn_new_sync("api.muse.ai",11,443,&c,&t)==-1 && calls==0);
    return 0;
}
''')
        binary=self.path/'tls_test'
        subprocess.run([*self.cc,'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                        '-I',str(self.path),'-I',str(ROOT/'main'),str(driver),str(ROOT/'main/proxy_tls.c'),
                        '-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
