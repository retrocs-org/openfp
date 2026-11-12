/* SPDX-License-Identifier: MIT */
#ifndef OFP_AMIGA_SOUND_H
#define OFP_AMIGA_SOUND_H
int  sound_open(void);              /* audio.device, the four channels, the samples in chip memory (-1: silence) */
void sound_play(int id, int volume);   /* SND_* (sounds_8bit.h), volume 0..64 */
void sound_powerup(int kind);
void sound_engine(int step);        /* our engine: 0 off, 1..3 (faster: higher), looping on the fourth channel */
void sound_close(void);
void sound_volume(int level);
/* Reserve Paula channels for music, leaving the rest for effects. */
int sound_share(int channels);
struct IOAudio;
struct MsgPort;
struct IOAudio *sound_music_request(struct MsgPort *reply_port);
#endif
