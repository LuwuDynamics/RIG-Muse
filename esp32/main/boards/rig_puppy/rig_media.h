#pragma once
#include "rig_show.h"
void rig_media_init(void);
bool rig_media_ready(void);
void rig_media_start(const rig_show_t *show);
void rig_media_stop(void);
void rig_media_connected(bool connected);
