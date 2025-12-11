// Bogus prototypes of ALSA functions  for compiling on non-Linux platforms

#ifndef _asoundlib_h
#define _asoundlib_h

#include <poll.h>

#define SND_PCM_NONBLOCK		0x0001

// No free needed, alloca() frees memory one function exit
#define snd_pcm_hw_params_alloca(ptr) do { \
    *ptr = 0; \
} while (0)

#define snd_pcm_status_alloca(ptr) do { \
    *ptr = 0; \
} while (0)

#ifdef __cplusplus
    extern "C" {
#endif

enum snd_pcm_stream_t { SND_PCM_STREAM_PLAYBACK = 0 , SND_PCM_STREAM_CAPTURE , SND_PCM_STREAM_LAST = SND_PCM_STREAM_CAPTURE };

enum snd_pcm_format_t {
    SND_PCM_FORMAT_UNKNOWN            = -1,
    SND_PCM_FORMAT_S8                 = 0,
    SND_PCM_FORMAT_U8,
    SND_PCM_FORMAT_S16_LE,
    SND_PCM_FORMAT_S16_BE,
    SND_PCM_FORMAT_U16_LE,
    SND_PCM_FORMAT_U16_BE,
    SND_PCM_FORMAT_S24_LE,
    SND_PCM_FORMAT_S24_BE,
    SND_PCM_FORMAT_U24_LE,
    SND_PCM_FORMAT_U24_BE,
    SND_PCM_FORMAT_S32_LE,
    SND_PCM_FORMAT_S32_BE,
    SND_PCM_FORMAT_U32_LE,
    SND_PCM_FORMAT_U32_BE,
    SND_PCM_FORMAT_FLOAT_LE,
    SND_PCM_FORMAT_FLOAT_BE,
    SND_PCM_FORMAT_FLOAT64_LE,
    SND_PCM_FORMAT_FLOAT64_BE,
    SND_PCM_FORMAT_IEC958_SUBFRAME_LE,
    SND_PCM_FORMAT_IEC958_SUBFRAME_BE,
    SND_PCM_FORMAT_MU_LAW,
    SND_PCM_FORMAT_A_LAW,
    SND_PCM_FORMAT_IMA_ADPCM,
    SND_PCM_FORMAT_MPEG,
    SND_PCM_FORMAT_GSM,
    SND_PCM_FORMAT_SPECIAL            = 31,
    SND_PCM_FORMAT_S24_3LE            = 32,
    SND_PCM_FORMAT_S24_3BE,
    SND_PCM_FORMAT_U24_3LE,
    SND_PCM_FORMAT_U24_3BE,
    SND_PCM_FORMAT_S20_3LE,
    SND_PCM_FORMAT_S20_3BE,
    SND_PCM_FORMAT_U20_3LE,
    SND_PCM_FORMAT_U20_3BE,
    SND_PCM_FORMAT_S18_3LE,
    SND_PCM_FORMAT_S18_3BE,
    SND_PCM_FORMAT_U18_3LE,
    SND_PCM_FORMAT_U18_3BE,
    SND_PCM_FORMAT_G723_24,
    SND_PCM_FORMAT_G723_24_1B,
    SND_PCM_FORMAT_G723_40,
    SND_PCM_FORMAT_G723_40_1B,
    SND_PCM_FORMAT_DSD_U8,
    SND_PCM_FORMAT_DSD_U16_LE,
    SND_PCM_FORMAT_DSD_U32_LE,
    SND_PCM_FORMAT_DSD_U16_BE,
    SND_PCM_FORMAT_DSD_U32_BE,
    SND_PCM_FORMAT_LAST               = SND_PCM_FORMAT_DSD_U32_BE,
    SND_PCM_FORMAT_S16                = SND_PCM_FORMAT_S16_LE,
    SND_PCM_FORMAT_U16                = SND_PCM_FORMAT_U16_LE,
    SND_PCM_FORMAT_S24                = SND_PCM_FORMAT_S24_LE,
    SND_PCM_FORMAT_U24                = SND_PCM_FORMAT_U24_LE,
    SND_PCM_FORMAT_S32                = SND_PCM_FORMAT_S32_LE,
    SND_PCM_FORMAT_U32                = SND_PCM_FORMAT_U32_LE,
    SND_PCM_FORMAT_FLOAT              = SND_PCM_FORMAT_FLOAT_LE,
    SND_PCM_FORMAT_FLOAT64            = SND_PCM_FORMAT_FLOAT64_LE,
    SND_PCM_FORMAT_IEC958_SUBFRAME    = SND_PCM_FORMAT_IEC958_SUBFRAME_LE,
};

enum snd_pcm_access_t {
    SND_PCM_ACCESS_MMAP_INTERLEAVED    = 0,
    SND_PCM_ACCESS_MMAP_NONINTERLEAVED,
    SND_PCM_ACCESS_MMAP_COMPLEX,
    SND_PCM_ACCESS_RW_INTERLEAVED,
    SND_PCM_ACCESS_RW_NONINTERLEAVED,
    SND_PCM_ACCESS_LAST                = SND_PCM_ACCESS_RW_NONINTERLEAVED,
};

enum snd_pcm_state_t {
  SND_PCM_STATE_OPEN = 0 , SND_PCM_STATE_SETUP , SND_PCM_STATE_PREPARED , SND_PCM_STATE_RUNNING ,
  SND_PCM_STATE_XRUN , SND_PCM_STATE_DRAINING , SND_PCM_STATE_PAUSED , SND_PCM_STATE_SUSPENDED ,
  SND_PCM_STATE_DISCONNECTED , SND_PCM_STATE_LAST = SND_PCM_STATE_DISCONNECTED , SND_PCM_STATE_PRIVATE1 = 1024
};

enum snd_pcm_subformat_t {
    SND_PCM_SUBFORMAT_STD  = 0,
    SND_PCM_SUBFORMAT_LAST = SND_PCM_SUBFORMAT_STD
};

struct _snd_pcm {
    int a;
};

struct _snd_pcm_hw_params {
    int a;
};

typedef struct _snd_pcm snd_pcm_t;
typedef unsigned long snd_pcm_uframes_t;
typedef long snd_pcm_sframes_t;
typedef struct _snd_pcm_hw_params snd_pcm_hw_params_t;
typedef struct _snd_pcm_status snd_pcm_status_t;

int	snd_pcm_open(snd_pcm_t **pcm, const char *name, enum snd_pcm_stream_t stream, int mode);
int snd_pcm_prepare (snd_pcm_t *pcm);
snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t *pcm, const void *buffer, snd_pcm_uframes_t size);
snd_pcm_sframes_t snd_pcm_readi(snd_pcm_t* pcm, void* buffer,snd_pcm_uframes_t size );	
int snd_pcm_close(snd_pcm_t *pcm);
int	snd_pcm_hw_params_current(snd_pcm_t *pcm, snd_pcm_hw_params_t *params);
int snd_pcm_hw_params(snd_pcm_t* pcm, snd_pcm_hw_params_t* params);
int	snd_pcm_hw_params_set_channels_near (snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val);
int	snd_pcm_hw_params_set_rate_near(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, unsigned int *val, int *dir);
int snd_pcm_hw_params_set_format(snd_pcm_t*	pcm, snd_pcm_hw_params_t* params, enum snd_pcm_format_t format);
int	snd_pcm_hw_params_set_access (snd_pcm_t *pcm, snd_pcm_hw_params_t *params, enum snd_pcm_access_t _access);
int	snd_pcm_hw_params_any (snd_pcm_t *pcm, snd_pcm_hw_params_t *params);

int	snd_pcm_hw_params_set_period_time(snd_pcm_t *pcm, snd_pcm_hw_params_t *params, 
    unsigned int val, int dir);
int snd_pcm_hw_params_set_period_time_max(snd_pcm_t *pcm, snd_pcm_hw_params_t *params,
    unsigned int* val, int* dir);
int snd_pcm_hw_params_set_channels(snd_pcm_t* pcm, snd_pcm_hw_params_t*	params, unsigned int val);
int snd_pcm_hw_params_set_subformat(snd_pcm_t* pcm, snd_pcm_hw_params_t* params, enum snd_pcm_subformat_t subformat);
int snd_pcm_hw_params_set_buffer_time(snd_pcm_t* pcm, snd_pcm_hw_params_t* params,
    unsigned int val, int dir);
int	snd_pcm_recover (snd_pcm_t *pcm, int err, int silent);
enum snd_pcm_state_t	snd_pcm_status_get_state (const snd_pcm_status_t* obj);
int snd_pcm_status(snd_pcm_t* pcm, snd_pcm_status_t* status);
const char*	snd_strerror (int errnum);
int snd_pcm_poll_descriptors(snd_pcm_t*	pcm, struct pollfd*	pfds, unsigned int space);
int snd_pcm_start(snd_pcm_t* pcm);

#ifdef __cplusplus
}
#endif

#endif
