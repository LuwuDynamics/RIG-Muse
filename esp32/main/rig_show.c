#include "rig_show.h"
#include <math.h>
#include <string.h>
static uint16_t color(unsigned r, unsigned g, unsigned b) {
    uint16_t c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    return (uint16_t)((c << 8) | (c >> 8));
}
void rig_face_render(uint16_t *pixels, int y0, int rows, rig_face_t face, uint32_t ms, bool connected) {
    if (!pixels || y0 < 0 || rows < 0 || y0 + rows > 240) return;
    int breath = (int)(3 * sinf(ms * 0.002f));
    bool blink = face != FACE_SLEEP && ms % 4300 > 4130;
    int gaze = face == FACE_CURIOUS ? (int)(7 * sinf(ms * 0.0015f)) : 0;
    uint16_t ink=color(78,225,255), blush=color(244,113,144);
    for (int y=y0; y<y0+rows; ++y) for (int x=0; x<240; ++x) {
        uint16_t c=0;
        for (int eye=0;eye<2;++eye) {
            int dx=x-(eye ? 165 : 75)-gaze, dy=y-105-breath;
            int h=face==FACE_SURPRISE ? 40 : 29;
            if (face==FACE_CURIOUS && eye) h=19;
            bool on=false;
            if (blink || face==FACE_SLEEP) on=dx*dx<625 && dy>=4 && dy<=9;
            else if (face==FACE_HAPPY || face==FACE_SHY) {
                int curve=dx*dx/50-16;
                on=dx*dx<784 && dy>=curve && dy<curve+8;
            } else on=dx*dx* h*h + dy*dy*625 < 625*h*h;
            if(on)c=ink;
            if (face==FACE_SHY && dx*dx*25+(dy-43)*(dy-43)*225 < 5625) c=blush;
        }
        // Small smile; sleep uses a breathing mouth.
        int mx=x-120, my=y-157;
        if (face==FACE_SLEEP) {
            int radius=5+breath;
            if(mx*mx+my*my<radius*radius)c=ink;
        } else if(mx*mx<289 && my>=6-mx*mx/48 && my<10-mx*mx/48)c=ink;
        int sx=x-120, sy=y-211;
        if(sx*sx+sy*sy<16)c=connected?color(62,210,118):color(242,165,56);
        pixels[(y-y0)*240+x]=c;
    }
}
int16_t rig_sound_sample(rig_sound_t sound, uint32_t sample) {
    // Every sound has finite duration; no stateful RNG or dependence on task timing.
    float t=sample/16000.0f, local=0, length=0, freq=0, phase=0;
    if(sound==SOUND_RECORD && t<0.12f) {
        local=t; length=0.12f; freq=1046.5f; phase=freq*local;
    } else if(sound==SOUND_SEND && t<0.28f) {
        local=fmodf(t,0.16f); length=0.10f; freq=784; phase=freq*local;
    } else if(sound==SOUND_CHIRP && t<0.68f) {
        local=fmodf(t,0.34f); length=0.23f; freq=700; phase=700*local+900*local*local;
    } else if(sound==SOUND_SPARKLE && t<0.96f) {
        static const float notes[]={523.25f,659.25f,783.99f,1046.5f};
        int note=(int)(t/0.24f); if(note>3) return 0;
        local=fmodf(t,0.24f); length=0.20f; freq=notes[note]; phase=freq*local;
    } else if(sound==SOUND_QUESTION && t<0.65f) {
        local=t; length=0.65f; freq=450; phase=450*t+500*t*t;
    } else if(sound==SOUND_BOOP && t<0.60f) {
        local=fmodf(t,0.30f); length=0.14f; freq=400; phase=400*local;
    } else if(sound==SOUND_SNORE && t<4.8f) {
        local=fmodf(t,2.4f); length=1.5f; freq=95; phase=95*local+8*sinf(local*4);
    } else return 0;
    if(local>=length || freq==0) return 0;
    float envelope=sinf(3.14159265f*local/length);
    float wave=sinf(6.2831853f*phase);
    if(sound==SOUND_SNORE)wave=0.7f*wave+0.3f*sinf(6.2831853f*phase*2.03f);
    return (int16_t)(4800*envelope*envelope*wave);
}
