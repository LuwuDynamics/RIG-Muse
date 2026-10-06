#include "rig_motion.h"

static void checksum(uint8_t *out, size_t length) {
    unsigned sum = 0;
    for (size_t i = 2; i + 1 < length; ++i) sum += out[i];
    out[length - 1] = (uint8_t)~sum;
}

size_t rig_sync_packet(uint8_t *out, const int positions[5], unsigned mask, unsigned speed) {
    // Preserve the vendor speed field, including zero; board planning bounds
    // the trajectory positions before packets reach the single UART owner.
    if(!mask || mask>31 || speed>RIG_SERVO_SPEED_MAX)return 0;
    unsigned count=0;
    for(unsigned i=0;i<5;++i)if(mask&(1u<<i)) {
        if(positions[i]<RIG_POSITION_MIN || positions[i]>RIG_POSITION_MAX)return 0;
        ++count;
    }
    out[0]=out[1]=255;out[2]=254;out[3]=count*7+4;out[4]=0x83;out[5]=0x35;out[6]=6;
    size_t n=7;
    for(unsigned i=0;i<5;++i)if(mask&(1u<<i)) {
        out[n++]=i+1;out[n++]=(uint8_t)positions[i];out[n++]=(uint8_t)(positions[i]>>8);
        out[n++]=(uint8_t)speed;out[n++]=(uint8_t)(speed>>8);out[n++]=0;out[n++]=0;
    }
    out[n++]=0;checksum(out,n);return n;
}
size_t rig_position_packet(uint8_t *out, int position, unsigned speed) {
    if(speed>RIG_SERVO_SPEED)return 0;
    int positions[5]={0,0,0,0,position};return rig_sync_packet(out,positions,16,speed);
}
size_t rig_read_id_packet(uint8_t *out, unsigned id, bool enable_register) {
    if(id<1 || id>5)return 0;
    const uint8_t frame[]={255,255,id,4,2,enable_register?0x30:0x48,enable_register?1:6,0};
    for(size_t i=0;i<sizeof(frame);++i)out[i]=frame[i];
    checksum(out,sizeof(frame));return sizeof(frame);
}
size_t rig_voltage_packet(uint8_t *out) {
    const uint8_t frame[]={255,255,1,4,2,0x40,1,0};
    for(size_t i=0;i<sizeof(frame);++i)out[i]=frame[i];
    checksum(out,sizeof(frame));return sizeof(frame);
}
size_t rig_read_packet(uint8_t *out) {return rig_read_id_packet(out,5,false);}
size_t rig_enable_read_packet(uint8_t *out) {return rig_read_id_packet(out,5,true);}
size_t rig_torque_id_packet(uint8_t *out, unsigned id, bool enabled) {
    if(id<1 || id>5)return 0;
    const uint8_t frame[]={255,255,id,4,3,0x30,enabled,0};
    for(size_t i=0;i<sizeof(frame);++i)out[i]=frame[i];
    checksum(out,sizeof(frame));return sizeof(frame);
}
size_t rig_torque_packet(uint8_t *out, bool enabled) {return rig_torque_id_packet(out,5,enabled);}
int rig_pose_ramp(int origin, int target, unsigned elapsed_ms, unsigned duration_ms) {
    if(!duration_ms || elapsed_ms>=duration_ms)return target;
    return origin+(int)((int64_t)(target-origin)*elapsed_ms/duration_ms);
}

bool rig_wave_pose(const int zero[5], unsigned phase, int target[5]) {
    // RIG-Omni xgo_action.cc Wave(): shift, lift right foreleg, wave, lower.
    // Restrict the original -600 wave endpoint to -400 to keep this board's
    // stored right-front zero (637) within the 200..2800 working range.
    static const int offsets[RIG_WAVE_PHASES][5]={
        {-100,700,-600,600,200}, {-100,-400,-600,600,400},
        {-100,-200,-600,600,400}, {-100,-400,-600,600,400},
        {-100,-200,-600,600,400}, {-100,-400,-600,600,400},
        {-300,600,-600,600,200}, {-550,550,-550,550,0},
    };
    if(phase>=RIG_WAVE_PHASES)return false;
    for(int i=0;i<5;++i) {
        target[i]=zero[i]+offsets[phase][i];
        if(target[i]<RIG_POSITION_MIN || target[i]>RIG_POSITION_MAX)return false;
    }
    return true;
}

int rig_rx_motor_event(rig_rx_t *rx, uint8_t value, int *id, int *position, int *torque, int *enabled) {
    if (rx->used < 2) {
        rx->used = value == 255 ? rx->used + 1 : 0;
        return false;
    }
    if (rx->used == 2 && value == 255) return false;
    rx->bytes[rx->used++] = value;
    // Five joints: six-byte state or single-byte enable readback, never write ACKs.
    if ((rx->used == 3 && (value < 1 || value > 5)) ||
        (rx->used == 4 && value != 8 && value != 3)) {
        rx->used = 0;
        return false;
    }
    if (rx->used < 4 || rx->used < (size_t)rx->bytes[3] + 4) return 0;
    size_t length=rx->used;
    rx->used = 0;
    unsigned sum = 0;
    for (size_t i = 2; i < length; ++i) sum += rx->bytes[i];
    if ((sum & 255) != 255 || rx->bytes[4] != 0) return false;
    *id=rx->bytes[2];
    if(length==7) {
        if(rx->bytes[5]>1) {
            // One-byte voltage and enable responses share framing. Only ID1
            // 2..20 V is a voltage candidate; owner also correlates its poll.
            if(*id!=1 || rx->bytes[5]<20 || rx->bytes[5]>200)return 0;
            *position=rx->bytes[5]*100;return 3;
        }
        *enabled=rx->bytes[5];return 2;
    }
    int pos = rx->bytes[7] | (rx->bytes[8] << 8);
    if (pos < RIG_POSITION_MIN || pos > RIG_POSITION_MAX) return false;
    *position = pos;
    *torque = (int16_t)(rx->bytes[9] | (rx->bytes[10] << 8));
    return 1;
}

int rig_rx_event(rig_rx_t *rx, uint8_t value, int *position, int *torque, int *enabled) {
    int id, pos, tor, en;
    int event=rig_rx_motor_event(rx,value,&id,&pos,&tor,&en);
    if(!event || event==3 || id!=5)return 0;
    if(event==1){*position=pos;*torque=tor;}else *enabled=en;
    return event;
}

bool rig_rx_byte(rig_rx_t *rx, uint8_t value, int *position, int *torque) {
    int enabled;
    return rig_rx_event(rx,value,position,torque,&enabled)==1;
}

bool rig_gesture_origin_valid(int position) {
    return position >= RIG_POSITION_MIN + RIG_GESTURE_AMPLITUDE &&
           position <= RIG_POSITION_MAX - RIG_GESTURE_AMPLITUDE;
}

int rig_gesture_target(int origin, bool seen_positive, bool seen_negative) {
    if (!seen_positive) return origin + RIG_GESTURE_AMPLITUDE;
    if (!seen_negative) return origin - RIG_GESTURE_AMPLITUDE;
    return origin;
}
