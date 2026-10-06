"""Host verification of the real RIG packet parser and bounded gesture planner."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class RigMotionTest(unittest.TestCase):
    def test_packets_feedback_and_bounds(self):
        source = r'''
#include "rig_motion.h"
#include <assert.h>
#include <string.h>

static bool feed(rig_rx_t *rx, const uint8_t *frame, size_t n, int *p, int *t) {
    bool seen = false;
    for (size_t i = 0; i < n; ++i) seen |= rig_rx_byte(rx, frame[i], p, t);
    return seen;
}

int main(void) {
    uint8_t out[43];
    const uint8_t position[] = {255,255,254,11,131,53,6,5,220,5,44,1,0,0,37};
    assert(rig_position_packet(out, 1500, 300) == sizeof(position));
    assert(!memcmp(out, position, sizeof(position)));
    assert(!rig_position_packet(out, 199, 300));
    assert(!rig_position_packet(out, 2801, 300));
    assert(rig_position_packet(out, 1500, 0)==15);
    assert(!rig_position_packet(out, 1500, 1001));
    const uint8_t read[] = {255,255,5,4,2,0x48,6,0xa6};
    assert(rig_read_packet(out) == sizeof(read));
    assert(!memcmp(out, read, sizeof(read)));
    const uint8_t enable[] = {255,255,5,4,3,0x30,1,0xc2};
    assert(rig_torque_packet(out, true) == sizeof(enable));
    assert(!memcmp(out, enable, sizeof(enable)));

    const uint8_t feedback[] = {255,255,5,8,0,0,0,0xdc,5,0,0,0x11};
    rig_rx_t rx = {0};
    int p = -1, t = -1;
    assert(!feed(&rx, feedback, 5, &p, &t));
    assert(feed(&rx, feedback + 5, sizeof(feedback) - 5, &p, &t));
    assert(p == 1500 && t == 0);
    uint8_t corrupt[12];
    memcpy(corrupt, feedback, sizeof(corrupt));
    corrupt[11] ^= 1;
    assert(!feed(&rx, corrupt, sizeof(corrupt), &p, &t));
    memcpy(corrupt, feedback, sizeof(corrupt));
    corrupt[4] = 1; corrupt[11] -= 1;  // valid checksum, servo error
    assert(!feed(&rx, corrupt, sizeof(corrupt), &p, &t));
    memcpy(corrupt, feedback, sizeof(corrupt));
    corrupt[2] = 1; corrupt[11] += 4;  // another servo must not satisfy readiness
    assert(!feed(&rx, corrupt, sizeof(corrupt), &p, &t));
    memcpy(corrupt, feedback, sizeof(corrupt));
    corrupt[3] = 255;  // invalid length cannot overrun parser
    assert(!feed(&rx, corrupt, sizeof(corrupt), &p, &t));
    const uint8_t enable_read[]={255,255,5,4,2,0x30,1,0xc3};
    assert(rig_enable_read_packet(out)==sizeof(enable_read));assert(!memcmp(out,enable_read,sizeof(enable_read)));
    int enabled=-1, event=0;const uint8_t enabled_response[]={255,255,5,3,0,1,0xf6};
    for(size_t i=0;i<sizeof(enabled_response);++i)event=rig_rx_event(&rx,enabled_response[i],&p,&t,&enabled);
    assert(event==2 && enabled==1 && p==1500);
    const uint8_t disabled_response[]={255,255,5,3,0,0,0xf7};
    for(size_t i=0;i<sizeof(disabled_response);++i)event=rig_rx_event(&rx,disabled_response[i],&p,&t,&enabled);
    assert(event==2 && enabled==0);
    uint32_t random = 0x12345678;
    for (int i = 0; i < 100000; ++i) {
        random = random * 1664525 + 1013904223;
        rig_rx_byte(&rx, (uint8_t)(random >> 24), &p, &t);
    }
    // A new clean frame after a bounded noise flush must recover.
    for (int i = 0; i < 24; ++i) rig_rx_byte(&rx, 0, &p, &t);
    assert(feed(&rx, feedback, sizeof(feedback), &p, &t));
    assert(!rig_gesture_origin_valid(279));
    assert(!rig_gesture_origin_valid(2721));
    assert(rig_gesture_origin_valid(280));
    assert(rig_gesture_origin_valid(2720));
    for (int origin = 280; origin <= 2720; ++origin) {
        for (unsigned phase = 0; phase < 3; ++phase) {
            int target = rig_gesture_target(origin, phase >= 1, phase >= 2);
            assert(target >= 200 && target <= 2800);
            assert(target - origin >= -80 && target - origin <= 80);
            if (phase == 2) assert(target == origin);
        }
    }
    const int zero[5]={2376,637,2334,511,1505};
    int pose[5];
    for(unsigned phase=0;phase<RIG_WAVE_PHASES;++phase) {
        assert(rig_wave_pose(zero,phase,pose));
        for(int i=0;i<5;++i)assert(pose[i]>=200 && pose[i]<=2800);
    }
    assert(!rig_wave_pose(zero,RIG_WAVE_PHASES,pose));
    const int invalid_zero[5]={2376,500,2334,511,1505};
    assert(!rig_wave_pose(invalid_zero,1,pose));
    int standing[5]={1850,1150,1850,1150,1500};
    assert(rig_sync_packet(out,standing,31,1000)==43);
    assert(out[2]==254 && out[3]==39 && out[6]==6);
    unsigned sum=0;for(int i=2;i<43;++i)sum+=out[i];assert((sum&255)==255);
    for(int i=0;i<5;++i)assert(out[7+i*7]==i+1);
    assert(rig_sync_packet(out,standing,31,6000)==43);
    assert(out[10]==0x70 && out[11]==0x17);
    assert(!rig_sync_packet(out,standing,31,6001));
    assert(!rig_sync_packet(out,standing,32,1000));
    standing[1]=199;assert(!rig_sync_packet(out,standing,31,1000));
    for(unsigned ms=0;ms<5000;ms+=7) {
        int a=rig_pose_ramp(800,1800,ms,3000),b=rig_pose_ramp(2200,1200,ms,3000);
        assert(a>=800 && a<=1800 && b>=1200 && b<=2200);
        if(ms>=3000)assert(a==1800 && b==1200);
    }
    assert(rig_gesture_target(1500, false, false) == 1580);
    assert(rig_gesture_target(1500, true, false) == 1420);
    assert(rig_gesture_target(1500, true, true) == 1500);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            test = pathlib.Path(directory) / "test.c"
            binary = pathlib.Path(directory) / "test"
            test.write_text(source)
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined", "-I", str(ROOT / "main"), "-I", str(ROOT / "main/boards/rig_puppy"),
                str(test), str(ROOT / "main/boards/rig_puppy/rig_motion.c"), "-o", str(binary)
            ], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
