# Source provenance

| Material | Origin | Treatment |
|---|---|---|
| ESP32/Linux Gadget SDK | Meta `facebookincubator/muse-gadget-sdk`, baseline `693cde9a884ad1edc87251b9f8944815f8de4809` | Upstream Apache-2.0 and copyright notices retained |
| Puppy hardware/actions/gait | Luwu RIG-Omni Puppy implementation | Adapted into board-local modules; formula hash and reference vectors retained |
| `minimp3.h` | `lieff/minimp3` | CC0-1.0, original component license retained |
| `pixel_font.c` | Adafruit GFX `glcdfont.c` | BSD-2-Clause notice retained in file |
| Jollybot avatar | Upstream Muse artwork | Outside the upstream Apache license; not relabeled as Apache artwork |
| ESP-IDF and managed components | Dependencies fetched by the build | Their original licenses apply |
| Puppy greeting media | Maintainer-provided real-device recording | Edited crop, silent MP4 and GIF; original phone recordings remain private |

The Puppy face uses project geometry/particle rendering and synthesized sound effects. The upstream avatar is retained as part of the SDK tree, rather than used as the Puppy's face.

Source licensing and access to the Muse service are separate. SDK tokens and account eligibility remain governed by the [Gadget SDK Terms](https://gadgets.muse.ai/sdk-terms).
